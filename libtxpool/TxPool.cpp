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
#include <libethcore/PartiallyBlock.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_invoke.h>

using namespace std;
using namespace dev::p2p;
using namespace dev::eth;
using namespace dev::executive;
namespace dev
{
namespace txpool
{
// import transaction to the txPool
std::pair<h256, Address> TxPool::submit(Transaction::Ptr _tx)
{
    m_submitPool->enqueue([this, _tx]() {
        // RequestNotBelongToTheGroup: 10004
        ImportResult verifyRet = ImportResult::NotBelongToTheGroup;
        if (isSealerOrObserver())
        {
            // check sync status failed
            if (m_syncStatusChecker && !m_syncStatusChecker())
            {
                TXPOOL_LOG(WARNING)
                    << LOG_DESC("submitTransaction async failed for checkSyncStatus failed")
                    << LOG_KV("groupId", m_groupId);
                verifyRet = ImportResult::TransactionRefused;
            }
            // check sync status succ
            else
            {
                verifyRet = import(_tx);
                if (ImportResult::Success == verifyRet)
                {
                    return;
                }
            }
        }
        notifyReceipt(_tx, verifyRet);
    });
    return std::make_pair(_tx->sha3(), toAddress(_tx->from(), _tx->nonce()));
}

// create receipt
void TxPool::notifyReceipt(dev::eth::Transaction::Ptr _tx, ImportResult const& _verifyRet)
{
    auto callback = _tx->rpcCallback();
    if (!callback)
    {
        return;
    }
    TransactionException txException;
    switch (_verifyRet)
    {
    case ImportResult::NotBelongToTheGroup:
        txException = TransactionException::RequestNotBelongToTheGroup;
        break;
    // 16
    case ImportResult::BlockLimitCheckFailed:
        txException = TransactionException::BlockLimitCheckFail;
        break;
    // 15
    case ImportResult::TransactionNonceCheckFail:
        txException = TransactionException::NonceCheckFail;
        break;
    // 28
    case ImportResult::TransactionPoolIsFull:
        txException = TransactionException::TxPoolIsFull;
        break;
    // 15
    case ImportResult::TxPoolNonceCheckFail:
        txException = TransactionException::NonceCheckFail;
        break;
    // 10000
    case ImportResult::AlreadyKnown:
        txException = TransactionException::AlreadyKnown;
        break;
    // 10001
    case ImportResult::AlreadyInChain:
        txException = TransactionException::AlreadyInChain;
        break;
    // 10002
    case ImportResult::InvalidChainId:
        txException = TransactionException::InvalidChainId;
        break;
    // 10003
    case ImportResult::InvalidGroupId:
        txException = TransactionException::InvalidGroupId;
        break;
    // invalid transaction: 10005
    case ImportResult::Malformed:
        txException = TransactionException ::MalformedTx;
        break;
    default:
        txException = TransactionException::TransactionRefused;
    }
    TXPOOL_LOG(ERROR) << LOG_DESC("notifyReceipt: txException")
                      << LOG_KV("hash", _tx->sha3().abridged())
                      << LOG_KV("exception", int(txException));
    dev::eth::LocalisedTransactionReceipt::Ptr receipt =
        std::make_shared<dev::eth::LocalisedTransactionReceipt>(txException);
    m_workerPool->enqueue([callback, receipt] { callback(receipt, bytesConstRef()); });
}

std::pair<h256, Address> TxPool::submitTransactions(dev::eth::Transaction::Ptr _tx)
{
    ImportResult ret = import(_tx);
    if (ImportResult::Success == ret)
    {
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
    else if (ImportResult::InvalidChainId == ret)
    {
        BOOST_THROW_EXCEPTION(TransactionRefused() << errinfo_comment(
                                  "InvalidChainId, txHash=" + toHex(_tx->sha3().abridged())));
    }
    else if (ImportResult::InvalidGroupId == ret)
    {
        BOOST_THROW_EXCEPTION(TransactionRefused() << errinfo_comment(
                                  "InvalidGroupId, txHash=" + toHex(_tx->sha3().abridged())));
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


bool TxPool::isSealerOrObserver()
{
    auto _nodeList = m_blockChain->sealerList() + m_blockChain->observerList();
    auto it = std::find(_nodeList.begin(), _nodeList.end(), m_service->id());
    if (it == _nodeList.end())
    {
        return false;
    }
    return true;
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
    _tx->setImportTime(u256(utcTime()));
    UpgradableGuard l(m_lock);
    /// check the txpool size
    if (m_txsQueue.size() >= m_limit)
    {
        return ImportResult::TransactionPoolIsFull;
    }
    /// check the verify result(nonce && signature check)
    ImportResult verify_ret = verify(_tx);
    if (verify_ret == ImportResult::Success)
    {
        {
            UpgradeGuard ul(l);
            if (insert(_tx))
            {
                m_txpoolNonceChecker->insertCache(*_tx);
            }
        }
        {
            WriteGuard txsLock(x_txsHashFilter);
            m_txsHashFilter->insert(_tx->sha3());
        }
        m_onReady();
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
    if (false == m_txNonceCheck->isBlockLimitOk(*trans))
    {
        return ImportResult::BlockLimitCheckFailed;
    }
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
    if (false == trans->checkChainId(u256(g_BCOSConfig.chainId())))
    {
        return ImportResult::InvalidChainId;
    }
    if (false == trans->checkGroupId(u256(m_groupId)))
    {
        return ImportResult::InvalidGroupId;
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
bool TxPool::removeTrans(h256 const& _txHash, bool _needTriggerCallback,
    std::shared_ptr<dev::eth::Block> _block, size_t _index)
{
    Transaction::Ptr transaction = nullptr;
    // remove transaction from txPool
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
    // call transaction callback
    if (_needTriggerCallback && transaction->rpcCallback())
    {
        m_workerPool->enqueue([this, _block, _index, transaction]() {
            // Not to use bind here, pReceipt wiil be free. So use TxCallback instead.
            // m_callbackPool.enqueue(bind(p_tx->second->rpcCallback(), pReceipt));
            LocalisedTransactionReceipt::Ptr pReceipt = nullptr;

            dev::bytesConstRef input = dev::bytesConstRef();
            if (_block && _block->transactionReceipts()->size() > _index)
            {
                input = dev::bytesConstRef(transaction->data().data(), transaction->data().size());
                pReceipt = constructTransactionReceipt((*(_block->transactions()))[_index],
                    (*(_block->transactionReceipts()))[_index], *_block, _index);
            }
            // fake transaction receipt
            else
            {
                // transaction refused
                pReceipt = std::make_shared<LocalisedTransactionReceipt>(
                    TransactionException::TransactionRefused);
                TXPOOL_LOG(WARNING) << LOG_DESC(
                    "NotifyReceipt: TransactionRefused, maybe invalid blocklimit or nonce");
            }
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
    bool succ = true;
    {
        WriteGuard wl(x_invalidTxs);
        WriteGuard l(m_lock);
        for (size_t i = 0; i < block->transactions()->size(); i++)
        {
            if (removeTrans((*(block->transactions()))[i]->sha3(), true, block, i) == false)
                succ = false;
            if (m_invalidTxs->count((*(block->transactions()))[i]->sha3()))
            {
                m_invalidTxs->erase((*(block->transactions()))[i]->sha3());
            }
        }
    }
    m_workerPool->enqueue([this, block]() { dropBlockTxsFilter(block); });
    // remove InvalidTxs
    m_workerPool->enqueue([this]() { removeInvalidTxs(); });
    return succ;
}

void TxPool::dropBlockTxsFilter(std::shared_ptr<dev::eth::Block> _block)
{
    if (!_block)
    {
        return;
    }
    WriteGuard l(x_txsHashFilter);
    for (auto tx : (*_block->transactions()))
    {
        if (m_txsHashFilter->count(tx->sha3()))
        {
            m_txsHashFilter->erase(tx->sha3());
        }
    }
}

// this function should be called after remove
void TxPool::removeInvalidTxs()
{
    UpgradableGuard l(x_invalidTxs);
    if (m_invalidTxs->size() == 0)
    {
        return;
    }

    tbb::parallel_invoke(
        [this]() {
            // remove invalid txs
            WriteGuard l(m_lock);
            for (auto const& item : *m_invalidTxs)
            {
                removeTrans(item.first);
                m_dropped.insert(item.first);
            }
        },
        [this]() {
            // remove invalid nonce
            for (auto const& item : *m_invalidTxs)
            {
                m_txpoolNonceChecker->delCache(item.second);
            }
        },
        [this]() {
            WriteGuard l(x_transactionKnownBy);
            // remove transaction knownBy
            for (auto const& item : *m_invalidTxs)
            {
                removeTransactionKnowBy(item.second);
            }
        },
        [this]() {
            WriteGuard txsLock(x_txsHashFilter);
            for (auto const& item : *m_invalidTxs)
            {
                m_txsHashFilter->erase(item.second);
            }
        });

    UpgradeGuard wl(l);
    m_invalidTxs->clear();
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
    TIME_RECORD(
        "dropBlockTrans, count:" + boost::lexical_cast<std::string>(block->transactions()->size()));

    tbb::parallel_invoke([this, block]() { removeBlockKnowTrans(*block); },
        [this, block]() {
            // update the Nonces of txs
            // (must be updated before dropTransactions to in case of sealing the same txs)
            m_txNonceCheck->updateCache(false);
            dropTransactions(block, true);
            // delete the nonce cache
            m_txpoolNonceChecker->delCache(*(block->transactions()));
        });

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

    {
        WriteGuard wl(x_invalidTxs);
        ReadGuard l(m_lock);
        for (auto it = m_txsQueue.begin(); txCnt < limit && it != m_txsQueue.end(); it++)
        {
            if (m_invalidTxs->count((*it)->sha3()))
            {
                continue;
            }
            /// check nonce again when obtain transactions
            // since the invalid nonce has already been checked before the txs import into the
            // txPool the txs with duplicated nonce here are already-committed, but have not been
            // dropped, so no need to insert the already-committed transaction into m_invalidTxs
            if (!m_txNonceCheck->isNonceOk(*(*it), false))
            {
                TXPOOL_LOG(DEBUG) << LOG_DESC(
                                         "Duplicated nonce: transaction maybe already-committed")
                                  << LOG_KV("nonce", (*it)->nonce())
                                  << LOG_KV("hash", (*it)->sha3().abridged());
                continue;
            }
            // check block limit(only insert txs with invalid blockLimit into m_invalidTxs)
            if (!m_txNonceCheck->isBlockLimitOk(*(*it)))
            {
                m_invalidTxs->insert(std::pair<h256, u256>((*it)->sha3(), (*it)->nonce()));
                TXPOOL_LOG(WARNING)
                    << LOG_DESC("Invalid blocklimit") << LOG_KV("hash", (*it)->sha3().abridged())
                    << LOG_KV("blockLimit", (*it)->blockLimit())
                    << LOG_KV("blockNumber", m_blockChain->number());
                ;
                continue;
            }
            if (!_avoid.count((*it)->sha3()))
            {
                ret->push_back(*it);
                txCnt++;
                if (_updateAvoid)
                    _avoid.insert((*it)->sha3());
            }
        }
    }
    // TXPOOL_LOG(DEBUG) << "topTransaction done, ignore: " << ignoreCount;
    m_workerPool->enqueue([this]() { removeInvalidTxs(); });
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

std::shared_ptr<Transactions> TxPool::obtainTransactions(std::vector<dev::h256> const& _reqTxs)
{
    std::shared_ptr<Transactions> ret = std::make_shared<Transactions>();
    ReadGuard l(m_lock);
    for (auto const& txHash : _reqTxs)
    {
        if (m_txsHash.count(txHash))
        {
            ret->push_back(*(m_txsHash[txHash]));
        }
    }
    return ret;
}

std::shared_ptr<std::vector<dev::h256>> TxPool::filterUnknownTxs(
    std::set<dev::h256> const& _txsHashSet)
{
    std::shared_ptr<std::vector<dev::h256>> unknownTxs = std::make_shared<std::vector<dev::h256>>();
    WriteGuard l(x_txsHashFilter);
    for (auto const& txHash : _txsHashSet)
    {
        if (!m_txsHashFilter->count(txHash))
        {
            unknownTxs->push_back(txHash);
            m_txsHashFilter->insert(txHash);
        }
    }
    return unknownTxs;
}

// init the PartiallyBlock
bool TxPool::initPartiallyBlock(dev::eth::Block::Ptr _block)
{
    PartiallyBlock::Ptr partiallyBlock = std::dynamic_pointer_cast<PartiallyBlock>(_block);
    assert(partiallyBlock);
    auto txsHash = partiallyBlock->txsHash();
    auto transactions = partiallyBlock->transactions();
    auto missedTxs = partiallyBlock->missedTxs();
    // fetch all the hitted transactions
    {
        ReadGuard l(m_lock);
        int64_t index = 0;
        for (auto const& hash : *txsHash)
        {
            if (m_txsHash.count(hash))
            {
                (*transactions)[index] = *(m_txsHash[hash]);
            }
            else
            {
                missedTxs->push_back(std::make_pair(hash, index));
            }
            index++;
        }
    }
    // missed some transactions
    if (missedTxs->size() > 0)
    {
        return false;
    }
    // hit all the transactions
    return true;
}
}  // namespace txpool
}  // namespace dev
