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
#include <libethcore/Exceptions.h>
using namespace std;
using namespace dev::p2p;
using namespace dev::eth;
namespace dev
{
namespace txpool
{
/**
 * @brief submit a transaction through RPC/web3sdk
 *
 * @param _t : transaction
 * @return std::pair<h256, Address> : maps from transaction hash to contract address
 */
std::pair<h256, Address> TxPool::submit(Transaction& _tx)
{
    ImportResult ret = import(_tx);
    if (ImportResult::Success == ret)
        return make_pair(_tx.sha3(), toAddress(_tx.from(), _tx.nonce()));
    else if (ret == ImportResult::TransactionNonceCheckFail)
    {
        BOOST_THROW_EXCEPTION(
            TransactionRefused() << errinfo_comment(
                "ImportResult::TransactionNonceCheckFail, txHash: " + toHex(_tx.sha3())));
    }
    else if (ImportResult::TransactionPoolIsFull == ret)
    {
        BOOST_THROW_EXCEPTION(
            TransactionRefused() << errinfo_comment(
                "ImportResult::TransactionPoolIsFull, txHash: " + toHex(_tx.sha3())));
    }
    else if (ImportResult::TxPoolNonceCheckFail == ret)
    {
        BOOST_THROW_EXCEPTION(
            TransactionRefused() << errinfo_comment(
                "ImportResult::TxPoolNonceCheckFail, txHash: " + toHex(_tx.sha3())));
    }
    else if (ImportResult::AlreadyKnown == ret)
    {
        BOOST_THROW_EXCEPTION(
            TransactionRefused() << errinfo_comment(
                "ImportResult::TransactionAlreadyKown, txHash: " + toHex(_tx.sha3())));
    }
    else if (ImportResult::AlreadyInChain == ret)
    {
        BOOST_THROW_EXCEPTION(
            TransactionRefused() << errinfo_comment(
                "ImportResult::TransactionAlreadyInChain, txHash: " + toHex(_tx.sha3())));
    }
    else
        BOOST_THROW_EXCEPTION(
            TransactionRefused() << errinfo_comment(
                "ImportResult::TransactionSubmitFailed, txHash: " + toHex(_tx.sha3())));
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
    Transaction tx;
    try
    {
        tx.decode(_txBytes, CheckTransaction::Everything);
        /// check sha3
        if (sha3(_txBytes.toBytes()) != tx.sha3())
            return ImportResult::Malformed;
    }
    catch (std::exception& e)
    {
        TXPOOL_LOG(ERROR) << "[#import] import transaction failed, [EINFO]:  "
                          << boost::diagnostic_information(e) << std::endl;
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
ImportResult TxPool::import(Transaction& _tx, IfDropped _ik)
{
    _tx.setImportTime(u256(utcTime()));
    UpgradableGuard l(m_lock);
    /// check the txpool size
    if (m_txsQueue.size() >= m_limit)
        return ImportResult::TransactionPoolIsFull;
    /// check the verify result(nonce && signature check)
    ImportResult verify_ret = verify(_tx);
    if (verify_ret == ImportResult::Success)
    {
        UpgradeGuard ul(l);
        if (insert(_tx))
        {
            m_commonNonceCheck->insertCache(_tx);
            m_onReady();
        }
    }
    return verify_ret;
}

/**
 * @brief : verify specified transaction, including:
 *  1. whether the transaction is known (refuse repeated transaction)
 *  2. check nonce
 *  3. check block limit
 *  TODO: check transaction filter
 *
 * @param trans : the transaction to be verified
 * @param _drop_policy : Import transaction policy
 * @return ImportResult : import result
 */
ImportResult TxPool::verify(Transaction const& trans, IfDropped _drop_policy, bool _needinsert)
{
    /// check whether this transaction has been existed
    h256 tx_hash = trans.sha3();
    if (m_known.count(tx_hash))
    {
        TXPOOL_LOG(DEBUG) << "[#Verify] already known tx: " << tx_hash.abridged() << std::endl;
        return ImportResult::AlreadyKnown;
    }
    /// the transaction has been dropped before
    if (m_dropped.count(tx_hash) && _drop_policy == IfDropped::Ignore)
    {
        TXPOOL_LOG(DEBUG) << "[#Verify] already dropped tx: " << tx_hash.abridged() << std::endl;
        return ImportResult::AlreadyInChain;
    }
    /// check nonce
    if (false == isBlockLimitOrNonceOk(trans, _needinsert))
        return ImportResult::TransactionNonceCheckFail;
    if (false == txPoolNonceCheck(trans))
        return ImportResult::TxPoolNonceCheckFail;
    /// TODO: filter check
    return ImportResult::Success;
}

/**
 * @brief: check the nonce
 * @param _tx : the transaction to be checked
 * @param _needInsert : insert transaction nonce to cache after checked
 * @return true : valid nonce
 * @return false : invalid nonce
 */
bool TxPool::isBlockLimitOrNonceOk(Transaction const& _tx, bool _needInsert) const
{
    if (_tx.nonce() == Invalid256 || (!m_txNonceCheck->ok(_tx, _needInsert)))
    {
        return false;
    }
    return true;
}

/**
 * @brief : remove latest transactions from queue after the transaction queue overloaded
 */
bool TxPool::removeTrans(h256 const& _txHash, bool needTriggerCallback,
    dev::eth::LocalisedTransactionReceipt::Ptr pReceipt)
{
    auto p_tx = m_txsHash.find(_txHash);
    if (p_tx == m_txsHash.end())
    {
        return false;
    }
    /// trigger callback from RPC
    if (needTriggerCallback && pReceipt)
    {
        p_tx->second->tiggerRpcCallback(pReceipt);
    }
    m_txsQueue.erase(p_tx->second);
    m_txsHash.erase(p_tx);
    if (m_known.count(_txHash))
        m_known.erase(_txHash);
    removeTransactionKnowBy(_txHash);  // Remove the record of transaction know by some peers
    return true;
}

/**
 * @brief : insert the newest transaction into the transaction queue
 * @param _tx: the give transaction queue can be inserted to the transaction queue
 */
bool TxPool::insert(Transaction const& _tx)
{
    h256 tx_hash = _tx.sha3();
    if (m_txsHash.count(tx_hash))
    {
        TXPOOL_LOG(DEBUG) << "[#Insert] Already known tx:  " << tx_hash.abridged() << std::endl;
        return false;
    }
    m_known.insert(tx_hash);
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
    UpgradableGuard l(m_lock);
    if (!m_known.count(_txHash))
        return false;
    UpgradeGuard ul(l);
    if (m_dropped.size() < m_limit)
        m_dropped.insert(_txHash);
    else
        m_dropped.clear();
    return removeTrans(_txHash);
}

dev::eth::LocalisedTransactionReceipt::Ptr TxPool::constructTransactionReceipt(
    Transaction const& tx, TransactionReceipt const& receipt, Block const& block, unsigned index)
{
    dev::eth::LocalisedTransactionReceipt::Ptr pTxReceipt =
        std::make_shared<LocalisedTransactionReceipt>(receipt, tx.sha3(),
            block.blockHeader().hash(), block.blockHeader().number(), tx.safeSender(),
            tx.receiveAddress(), index, receipt.gasUsed(), receipt.contractAddress());
    return pTxReceipt;
}

bool TxPool::dropTransactions(Block const& block, bool needNotify)
{
    if (block.getTransactionSize() == 0)
        return true;
    WriteGuard l(m_lock);
    bool succ = true;
    for (size_t i = 0; i < block.transactions().size(); i++)
    {
        LocalisedTransactionReceipt::Ptr pReceipt = nullptr;
        if (block.transactionReceipts().size() > i)
        {
            pReceipt = constructTransactionReceipt(
                block.transactions()[i], block.transactionReceipts()[i], block, i);
        }
        if (removeTrans(block.transactions()[i].sha3(), true, pReceipt) == false)
            succ = false;
    }
    return succ;
}

/// drop a block when it has been committed failed
bool TxPool::handleBadBlock(Block const& block)
{
    /// bool ret = dropTransactions(block, false);
    /// remove the nonce check related to txpool
    /// m_commonNonceCheck->delCache(block.transactions());
    return true;
}
/// drop a block when it has been committed successfully
bool TxPool::dropBlockTrans(Block const& block)
{
    /// update the nonce check related to block chain
    m_txNonceCheck->updateCache(false);
    bool ret = dropTransactions(block, true);
    /// remove the nonce check related to txpool
    m_commonNonceCheck->delCache(block.transactions());
    return ret;
}

/**
 * @brief Get top transactions from the queue
 *
 * @param _limit : _limit Max number of transactions to return.
 * @param _avoid : Transactions to avoid returning.
 * @param _condition : The function return false to avoid transaction to return.
 * @return Transactions : up to _limit transactions
 */
dev::eth::Transactions TxPool::topTransactions(uint64_t const& _limit)
{
    h256Hash _avoid = h256Hash();
    return topTransactions(_limit, _avoid);
}

Transactions TxPool::topTransactions(uint64_t const& _limit, h256Hash& _avoid, bool _updateAvoid)
{
    UpgradableGuard l(m_lock);
    uint64_t limit = min(m_limit, _limit);
    uint64_t txCnt = 0;
    Transactions ret;
    std::vector<dev::h256> invalidBlockLimitTxs;
    std::vector<dev::eth::NonceKeyType> nonceKeyCache;
    for (auto it = m_txsQueue.begin(); txCnt < limit && it != m_txsQueue.end(); it++)
    {
        /// check block limit and nonce again when obtain transactions
        if (false == isBlockLimitOrNonceOk(*it, false))
        {
            invalidBlockLimitTxs.push_back(it->sha3());
            nonceKeyCache.push_back(m_commonNonceCheck->generateKey(*it));
            continue;
        }
        if (!_avoid.count(it->sha3()))
        {
            ret.push_back(*it);
            txCnt++;
            if (_updateAvoid)
                _avoid.insert(it->sha3());
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
            m_commonNonceCheck->delCache(key);
    }
    return ret;
}

Transactions TxPool::topTransactionsCondition(
    uint64_t const& _limit, std::function<bool(Transaction const&)> const& _condition)
{
    ReadGuard l(m_lock);
    Transactions ret;
    uint64_t limit = min(m_limit, _limit);
    uint64_t txCnt = 0;
    for (auto it = m_txsQueue.begin(); txCnt < limit && it != m_txsQueue.end(); it++)
    {
        if (_condition(*it))
        {
            ret.push_back(*it);
            txCnt++;
        }
    }
    return ret;
}

/// get all transactions(maybe blocksync module need this interface)
Transactions TxPool::pendingList() const
{
    ReadGuard l(m_lock);
    Transactions ret;
    for (auto t = m_txsQueue.begin(); t != m_txsQueue.end(); ++t)
    {
        ret.push_back(*t);
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
    m_known.clear();
    m_txsQueue.clear();
    m_txsHash.clear();
    m_dropped.clear();
    WriteGuard l_trans(x_transactionKnownBy);
    m_transactionKnownBy.clear();
}

/// Set transaction is known by a node
void TxPool::transactionIsKnownBy(h256 const& _txHash, h512 const& _nodeId)
{
    WriteGuard l(x_transactionKnownBy);
    m_transactionKnownBy[_txHash].insert(_nodeId);
}

/// Is the transaction is known by the node ?
bool TxPool::isTransactionKnownBy(h256 const& _txHash, h512 const& _nodeId)
{
    ReadGuard l(x_transactionKnownBy);
    auto p = m_transactionKnownBy.find(_txHash);
    if (p == m_transactionKnownBy.end())
        return false;
    return p->second.find(_nodeId) != p->second.end();
}

/// Is the transaction is known by someone
bool TxPool::isTransactionKnownBySomeone(h256 const& _txHash)
{
    ReadGuard l(x_transactionKnownBy);
    auto p = m_transactionKnownBy.find(_txHash);
    if (p == m_transactionKnownBy.end())
        return false;
    return !p->second.empty();
}

void TxPool::removeTransactionKnowBy(h256 const& _txHash)
{
    WriteGuard l(x_transactionKnownBy);
    auto p = m_transactionKnownBy.find(_txHash);
    if (p != m_transactionKnownBy.end())
        m_transactionKnownBy.erase(p);
}

}  // namespace txpool
}  // namespace dev
