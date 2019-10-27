/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */

/**
 * @brief : implementation of transaction pool
 * @file: TxPool.cpp
 * @author: yujiechen
 * @date: 2018-09-23
 */
#include "TxPool.h"
#include <libdevcore/Common.h>
#include <libethcore/Exceptions.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_invoke.h>

using namespace std;
using namespace dev::p2p;
using namespace dev::eth;
namespace dev
{
namespace txpool
{
// import transaction to the txPool
std::pair<h256, Address> TxPool::submit(Transaction::Ptr _tx)
{
	m_submitPool->enqueue([this, _tx]() {
    	submitTransactions(_tx);
    });
    return std::make_pair(_tx->sha3(), toAddress(_tx->from(), _tx->nonce()));
}

std::pair<h256, Address> TxPool::submitTransactions(dev::eth::Transaction::Ptr _tx)
{
    ImportResult ret = import(_tx);
    if (ImportResult::Success == ret)
    {
        // only note block sync when submit transactions through RPC
        m_onReady();
        return std::make_pair(_tx->sha3(), toAddress(_tx->from(), _tx->nonce()));
    }

    else if (ret == ImportResult::TransactionNonceCheckFail)
    {
        BOOST_THROW_EXCEPTION(
            TransactionRefused() << errinfo_comment(
                "TransactionNonceCheckFail, txHash=" + toHex(_tx->sha3().abridged())));
    }
    else if (ImportResult::TransactionPoolIsFull == ret)
    {
        BOOST_THROW_EXCEPTION(
            TransactionRefused() << errinfo_comment(
                "TransactionPoolIsFull, txHash=" + toHex(_tx->sha3().abridged())));
    }
    else if (ImportResult::TxPoolNonceCheckFail == ret)
    {
        BOOST_THROW_EXCEPTION(TransactionRefused() << errinfo_comment(
                                  "TxPoolNonceCheckFail, txHash=" + toHex(_tx->sha3().abridged())));
    }
    else if (ImportResult::AlreadyKnown == ret)
    {
        BOOST_THROW_EXCEPTION(
            TransactionRefused() << errinfo_comment(
                "TransactionAlreadyKown, txHash=" + toHex(_tx->sha3().abridged())));
    }
    else if (ImportResult::AlreadyInChain == ret)
    {
        BOOST_THROW_EXCEPTION(
            TransactionRefused() << errinfo_comment(
                "TransactionAlreadyInChain, txHash=" + toHex(_tx->sha3().abridged())));
    }
    else if (ImportResult::InvalidChainIdOrGroupId == ret)
    {
        BOOST_THROW_EXCEPTION(
            TransactionRefused() << errinfo_comment(
                "InvalidChainIdOrGroupId, txHash=" + toHex(_tx->sha3().abridged())));
    }
    else if (ImportResult::BlockLimitCheckFailed == ret)
    {
        BOOST_THROW_EXCEPTION(TransactionRefused() << errinfo_comment(
                                  "BlockLimitCheckFailed, txBlockLimit=" + _tx->blockLimit().str() +
                                  ", txHash=" + toHex(_tx->sha3().abridged())));
    }
    else
    {
        BOOST_THROW_EXCEPTION(
            TransactionRefused() << errinfo_comment(
                "TransactionSubmitFailed, txHash=" + toHex(_tx->sha3().abridged())));
    }
}

/**
 * @brief : veirfy specified transaction (called by libsync)
 *          && insert the valid transaction into the transaction queue
 * @param _txBytes : encoded data of the transaction
 * @param _ik :  Import transaction policy,
 *  1. Ignore: Don't import transaction that was previously dropped
 *  2. Retry: Import transaction even if it was dropped before
 * @return ImportResult : import result, if success, return ImportResult::Success
 */
ImportResult TxPool::import(bytesConstRef _txBytes, IfDropped _ik)
{
    Transaction::Ptr tx = std::make_shared<Transaction>();
    try
    {
        /// only decode, verify transactions later
        tx->decode(_txBytes, CheckTransaction::None);
        /// check sha3
        if (sha3(_txBytes.toBytes()) != tx->sha3())
            return ImportResult::Malformed;
    }
    catch (std::exception& e)
    {
        TXPOOL_LOG(ERROR) << LOG_DESC("import transaction failed")
                          << LOG_KV("EINFO", boost::diagnostic_information(e));
        return ImportResult::Malformed;
    }
    return import(tx, _ik);
}

/**
 * @brief : Verify and add transaction to the queue synchronously.
 *
 * @param _tx : Transaction data.
 * @param _ik : Set to Retry to force re-addinga transaction that was previously dropped.
 * @return ImportResult : Import result code.
 */
ImportResult TxPool::import(Transaction::Ptr _tx, IfDropped)
{
    m_totalTxsNum += 1;
    _tx->setImportTime(u256(utcTime()));
    UpgradableGuard l(m_lock);
    /// check the txpool size
    if (m_txsQueue.size() >= m_limit)
    {
        if (g_BCOSConfig.version() < V2_1_0)
        {
            auto callback = _tx->rpcCallback();
            if (callback)
            {
                dev::eth::LocalisedTransactionReceipt::Ptr receipt =
                    std::make_shared<dev::eth::LocalisedTransactionReceipt>(
                        executive::TransactionException::TxPoolIsFull);

                m_workerPool->enqueue([callback, receipt] { callback(receipt, bytesConstRef()); });
            }
        }

        return ImportResult::TransactionPoolIsFull;
    }
    /// check the verify result(nonce && signature check)
    ImportResult verify_ret = verify(_tx);
    if (verify_ret == ImportResult::Success)
    {
        UpgradeGuard ul(l);
        if (insert(_tx))
        {
            m_txpoolNonceChecker->insertCache(*_tx);
        }
    }
    return verify_ret;
}

void TxPool::verifyAndSetSenderForBlock(dev::eth::Block& block)
{
    auto trans_num = block.getTransactionSize();
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, trans_num), [&](const tbb::blocked_range<size_t>& _r) {
            for (size_t i = _r.begin(); i != _r.end(); i++)
            {
                h256 txHash = (*block.transactions())[i]->sha3();

                /// force sender for the transaction
                ReadGuard l(m_lock);
                auto p_tx = m_txsHash.find(txHash);
                l.unlock();

                if (p_tx != m_txsHash.end())
                {
                    block.setSenderForTransaction(i, (*(p_tx->second))->sender());
                }
                /// verify the transaction
                else
                {
                    block.setSenderForTransaction(i);
                }
            }
        });
}

bool TxPool::txExists(dev::h256 const& txHash)
{
    ReadGuard l(m_lock);
    /// can't submit to the transaction pull, return false
    if (m_txsQueue.size() >= m_limit)
        return true;
    if (m_txsHash.count(txHash))
    {
        return true;
    }
    return false;
}

/**
 * @brief : verify specified transaction, including:
 *  1. whether the transaction is known (refuse repeated transaction)
 *  2. check nonce
 *  3. check block limit
 *  4. check chainId and groupId
 *  TODO: check transaction filter
 *
 * @param trans : the transaction to be verified
 * @param _drop_policy : Import transaction policy
 * @return ImportResult : import result
 */
ImportResult TxPool::verify(Transaction::Ptr trans, IfDropped _drop_policy)
{
    /// check whether this transaction has been existed
    h256 tx_hash = trans->sha3();
    if (m_txsHash.count(tx_hash))
    {
        TXPOOL_LOG(TRACE) << LOG_DESC("Verify: already known tx")
                          << LOG_KV("hash", tx_hash.abridged());
        return ImportResult::AlreadyKnown;
    }
    /// the transaction has been dropped before
    if (m_dropped.count(tx_hash) && _drop_policy == IfDropped::Ignore)
    {
        TXPOOL_LOG(TRACE) << LOG_DESC("Verify: already dropped tx: ")
                          << LOG_KV("hash", tx_hash.abridged());
        return ImportResult::AlreadyInChain;
    }
    /// check nonce
    if (trans->nonce() == Invalid256 || (!m_txNonceCheck->isNonceOk(*trans, false)))
    {
        return ImportResult::TransactionNonceCheckFail;
    }
#if 0
    if (false == m_txNonceCheck->isBlockLimitOk(trans))
    {
        return ImportResult::BlockLimitCheckFailed;
    }
#endif
    try
    {
        /// check transaction signature here when everything is ok
        trans->sender();
    }
    catch (std::exception& e)
    {
        TXPOOL_LOG(ERROR) << "[Verify] invalid signature, tx = " << tx_hash.abridged();
        return ImportResult::Malformed;
    }
    /// nonce related to txpool must be checked at the last, since this will insert nonce of the
    /// valid transaction into the txpool nonce cache
    if (false == txPoolNonceCheck(trans))
    {
        return ImportResult::TxPoolNonceCheckFail;
    }
    /// check chainId and groupId
    if (false == trans->checkChainIdAndGroupId(u256(g_BCOSConfig.chainId()), u256(m_groupId)))
    {
        return ImportResult::InvalidChainIdOrGroupId;
    }
    /// TODO: filter check
    return ImportResult::Success;
}

struct TxCallback
{
    RPCCallback call;
    dev::eth::LocalisedTransactionReceipt::Ptr pReceipt;
};

/**
 * @brief : remove latest transactions from queue after the transaction queue overloaded
 */
bool TxPool::removeTrans(h256 const& _txHash, bool needTriggerCallback,
    std::shared_ptr<dev::eth::Block> block, size_t index)
{
    Transaction::Ptr transaction;
    std::unordered_map<h256, TransactionQueue::iterator>::iterator p_tx;
    {
        p_tx = m_txsHash.find(_txHash);
        if (p_tx == m_txsHash.end())
        {
            return true;
        }

        transaction = *(p_tx->second);
        m_txsQueue.erase(p_tx->second);
        m_txsHash.erase(p_tx);

        // m_delTransactions.unsafe_erase(_txHash);
    }

    if (needTriggerCallback && block && transaction->rpcCallback())
    {
        m_workerPool->enqueue([this, block, index, transaction]() {
            // Not to use bind here, pReceipt wiil be free. So use TxCallback instead.
            // m_callbackPool.enqueue(bind(p_tx->second->rpcCallback(), pReceipt));
            LocalisedTransactionReceipt::Ptr pReceipt = nullptr;
            if (block->transactionReceipts()->size() > index)
            {
                pReceipt = constructTransactionReceipt((*(block->transactions()))[index],
                    (*(block->transactionReceipts()))[index], *block, index);
            }

            auto input = dev::bytesConstRef(transaction->data().data(), transaction->data().size());
            TxCallback callback{transaction->rpcCallback(), pReceipt};

            callback.call(callback.pReceipt, input);
        });
    }

    return true;
}

/**
 * @brief : insert the newest transaction into the transaction queue
 * @param _tx: the give transaction queue can be inserted to the transaction queue
 */
bool TxPool::insert(Transaction::Ptr _tx)
{
    h256 tx_hash = _tx->sha3();
    if (m_txsHash.count(tx_hash))
    {
        return false;
    }
    TransactionQueue::iterator p_tx = m_txsQueue.emplace(_tx).first;
    m_txsHash[tx_hash] = p_tx;
    return true;
}

/**
 * @brief Remove bad transaction from the queue
 * @param _txHash: transaction hash
 */
bool TxPool::drop(h256 const& _txHash)
{
    bool succ = false;
    /// drop transactions
    {
        UpgradableGuard l(m_lock);
        if (!m_txsHash.count(_txHash))
            return false;
        UpgradeGuard ul(l);
        if (m_dropped.size() < m_limit)
            m_dropped.insert(_txHash);
        else
            m_dropped.clear();
        succ = removeTrans(_txHash);
    }
    /// drop information of transactions
    {
        WriteGuard l(x_transactionKnownBy);
        removeTransactionKnowBy(_txHash);
    }
    return succ;
}

dev::eth::LocalisedTransactionReceipt::Ptr TxPool::constructTransactionReceipt(
    Transaction::Ptr tx, TransactionReceipt::Ptr receipt, Block const& block, unsigned index)
{
    dev::eth::LocalisedTransactionReceipt::Ptr pTxReceipt =
        std::make_shared<dev::eth::LocalisedTransactionReceipt>(*receipt, tx->sha3(),
            block.blockHeader().hash(), block.blockHeader().number(), tx->safeSender(),
            tx->receiveAddress(), index, receipt->gasUsed(), receipt->contractAddress());
    return pTxReceipt;
}

bool TxPool::removeBlockKnowTrans(Block const& block)
{
    if (block.getTransactionSize() == 0)
        return true;
    WriteGuard l(x_transactionKnownBy);
    for (auto const& trans : *block.transactions())
    {
        removeTransactionKnowBy(trans->sha3());
    }
    return true;
}

bool TxPool::dropTransactions(std::shared_ptr<Block> block, bool)
{
    if (block->getTransactionSize() == 0)
        return true;
    WriteGuard l(m_lock);
    bool succ = true;

    for (size_t i = 0; i < block->transactions()->size(); i++)
    {
        if (removeTrans((*(block->transactions()))[i]->sha3(), true, block, i) == false)
            succ = false;
    }
    return succ;
}

// TODO: drop a block when it has been committed failed
bool TxPool::handleBadBlock(Block const&)
{
    /// bool ret = dropTransactions(block, false);
    /// remove the nonce check related to txpool
    /// m_txpoolNonceChecker->delCache(block.transactions());
    return true;
}
/// drop a block when it has been committed successfully
bool TxPool::dropBlockTrans(std::shared_ptr<Block> block)
{
    TIME_RECORD("dropBlockTrans, count:" +
                boost::lexical_cast<std::string>(block->transactions()->size()));

    tbb::parallel_invoke(
		[this]() {
			m_txNonceCheck->updateCache(false);
		},
		[this, block]() {
			dropTransactions(block, true);
		},
		[this, block]() {
			removeBlockKnowTrans(*block);
		},
		[this, block]() {
			m_txpoolNonceChecker->delCache(*(block->transactions()));
		}
    );

    return true;
}

/**
 * @brief Get top transactions from the queue
 *
 * @param _limit : _limit Max number of transactions to return.
 * @param _avoid : Transactions to avoid returning.
 * @param _condition : The function return false to avoid transaction to return.
 * @return Transactions : up to _limit transactions
 */
std::shared_ptr<dev::eth::Transactions> TxPool::topTransactions(uint64_t const& _limit)
{
    h256Hash _avoid = h256Hash();
    return topTransactions(_limit, _avoid);
}

std::shared_ptr<Transactions> TxPool::topTransactions(
    uint64_t const& _limit, h256Hash& _avoid, bool _updateAvoid)
{
    uint64_t limit = min(m_limit, _limit);
    uint64_t txCnt = 0;
    auto ret = std::make_shared<Transactions>();
    std::vector<dev::h256> invalidBlockLimitTxs;
    std::vector<dev::eth::NonceKeyType> nonceKeyCache;

    TIME_RECORD("topTransaction");
    // size_t ignoreCount = 0;
    {
        UpgradableGuard l(m_lock);
        for (auto it = m_txsQueue.begin(); txCnt < limit && it != m_txsQueue.end(); it++)
        {
            /// check  nonce again when obtain transactions
            if (!m_txNonceCheck->isNonceOk(*(*it), false))
            {
                invalidBlockLimitTxs.push_back((*it)->sha3());
                nonceKeyCache.push_back((*it)->nonce());
                continue;
            }
#if 0
            /// check block limit and nonce again when obtain transactions
            if (false == m_txNonceCheck->isBlockLimitOk(*(*it)))
            // if (m_delTransactions.find((*it)->sha3()) != m_delTransactions.end())
            {
                invalidBlockLimitTxs.push_back((*it)->sha3());
                nonceKeyCache.push_back((*it)->nonce());
                // ++ignoreCount;
                continue;
            }
#endif
            if (!_avoid.count((*it)->sha3()))
            {
                ret->push_back(*it);
                txCnt++;
                if (_updateAvoid)
                    _avoid.insert((*it)->sha3());
            }
        }
        if (invalidBlockLimitTxs.size() > 0)
        {
            UpgradeGuard ul(l);
            for (auto txHash : invalidBlockLimitTxs)
            {
                removeTrans(txHash);
                m_dropped.insert(txHash);
            }
        }
        /// delete cached invalid nonce
        if (nonceKeyCache.size() > 0)
        {
            for (auto key : nonceKeyCache)
                m_txpoolNonceChecker->delCache(key);
        }
    }

    if (invalidBlockLimitTxs.size() > 0)
    {
        WriteGuard l(x_transactionKnownBy);
        for (auto txHash : invalidBlockLimitTxs)
        {
            removeTransactionKnowBy(txHash);
            TXPOOL_LOG(DEBUG) << LOG_DESC("remove imported tx: the block limit expired")
                              << LOG_KV("hash", txHash.abridged());
        }
    }

    // TXPOOL_LOG(DEBUG) << "topTransaction done, ignore: " << ignoreCount;

    return ret;
}

std::shared_ptr<Transactions> TxPool::topTransactionsCondition(
    uint64_t const& _limit, dev::h512 const& _nodeId)
{
    ReadGuard l(m_lock);
    std::shared_ptr<Transactions> ret = std::make_shared<Transactions>();

    // size_t ignoreCount = 0;
    uint64_t limit = min(m_limit, _limit);
    {
        uint64_t txCnt = 0;
        ReadGuard l_kownTrans(x_transactionKnownBy);
        for (auto it = m_txsQueue.begin(); txCnt < limit && it != m_txsQueue.end(); it++)
        {
            if (!isTransactionKnownBy((*it)->sha3(), _nodeId))
            {
#if 0
                if (m_delTransactions.find((*it)->sha3()) != m_delTransactions.end())
                {
                    ++ignoreCount;
                    continue;
                }
#endif

                ret->push_back(*it);
                txCnt++;
            }
        }
    }

    // TXPOOL_LOG(DEBUG) << "topTransactionCondition done, ignore: " << ignoreCount;

    return ret;
}

/// get all transactions(maybe blocksync module need this interface)
std::shared_ptr<Transactions> TxPool::pendingList() const
{
    ReadGuard l(m_lock);
    std::shared_ptr<Transactions> ret = std::make_shared<Transactions>();
    for (auto t = m_txsQueue.begin(); t != m_txsQueue.end(); ++t)
    {
        ret->push_back(*t);
    }
    return ret;
}

/// get current transaction num
size_t TxPool::pendingSize()
{
    ReadGuard l(m_lock);
    return m_txsQueue.size();
}

/// @returns the status of the transaction queue.
TxPoolStatus TxPool::status() const
{
    TxPoolStatus status;
    ReadGuard l(m_lock);
    status.current = m_txsQueue.size();
    status.dropped = m_dropped.size();
    return status;
}

/// Clear the queue
void TxPool::clear()
{
    WriteGuard l(m_lock);
    m_txsQueue.clear();
    m_txsHash.clear();
    m_dropped.clear();
    WriteGuard l_trans(x_transactionKnownBy);
    m_transactionKnownBy.clear();
}

/// Set transaction is known by a node
void TxPool::setTransactionIsKnownBy(h256 const& _txHash, h512 const& _nodeId)
{
    m_transactionKnownBy[_txHash].insert(_nodeId);
}

/// set transactions is known by a node
void TxPool::setTransactionsAreKnownBy(
    std::vector<dev::h256> const& _txHashVec, h512 const& _nodeId)
{
    WriteGuard l(x_transactionKnownBy);
    for (auto const& tx_hash : _txHashVec)
    {
        m_transactionKnownBy[tx_hash].insert(_nodeId);
    }
}

/// Is the transaction is known by someone
bool TxPool::isTransactionKnownBySomeone(h256 const& _txHash)
{
    auto p = m_transactionKnownBy.find(_txHash);
    if (p == m_transactionKnownBy.end())
        return false;
    return !p->second.empty();
}

// Remove the record of transaction know by some peers
void TxPool::removeTransactionKnowBy(h256 const& _txHash)
{
    auto p = m_transactionKnownBy.find(_txHash);
    if (p != m_transactionKnownBy.end())
        m_transactionKnownBy.erase(p);
}

}  // namespace txpool
}  // namespace dev
