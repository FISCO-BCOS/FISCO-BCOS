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
using namespace dev::p2p;
using namespace dev::eth;
namespace dev
{
namespace txpool
{
/**
 * @brief : 1. obtain a transaction from the lower network
 *  2. verify the transaction, including repeated transaction && nonce check && block limit check
 *  3. insert the transaction to transaction queue (m_txsQueue)
 * @param pMessage : p2p handler returned by p2p module to obtain messages
 */
void TxPool::enqueue(
    dev::p2p::P2PException exception, std::shared_ptr<Session> session, Message::Ptr pMessage)
{
    if (exception.errorCode() != 0)
        BOOST_THROW_EXCEPTION(P2pObtainTransactionFailed()
                              << errinfo_comment("obtain transaction from lower network failed"));
    bytesConstRef tx_data = bytesConstRef(pMessage->buffer().get());
    import(tx_data);
}

/**
 * @brief submit a transaction through RPC/web3sdk
 *
 * @param _t : transaction
 * @return std::pair<h256, Address> : maps from transaction hash to contract address
 */
std::pair<h256, Address> TxPool::submit(Transaction& _tx)
{
    ImportResult ret = import(_tx);
    if (ret == ImportResult::NonceCheckFail)
    {
        return make_pair(_tx.sha3(), Address(1));
    }
    else if (ImportResult::BlockLimitCheckFail == ret)
    {
        return make_pair(_tx.sha3(), Address(2));
    }
    return make_pair(_tx.sha3(), toAddress(_tx.from(), _tx.nonce()));
}

/**
 * @brief : veirfy specified transaction
 *          && insert the valid transaction into the transaction queue
 * @param _txBytes : encoded data of the transaction
 * @param _ik :  Import transaction policy,
 *  1. Ignore: Don't import transaction that was previously dropped
 *  2. Retry: Import transaction even if it was dropped before
 * @return ImportResult : import result, if success, return ImportResult::Success
 */
ImportResult TxPool::import(bytesConstRef _txBytes, IfDropped _ik)
{
    try
    {
        Transaction tx(_txBytes, CheckTransaction::Everything);
        /// check sha3
        if (sha3(_txBytes) != tx.sha3())
            BOOST_THROW_EXCEPTION(InconsistentTransactionSha3()
                                  << errinfo_comment("Transaction sha3 is inconsistent"));
        return import(tx, _ik);
    }
    catch (std::exception& err)
    {
        LOG(ERROR) << "Ignoring invalid transaction" << err.what();
        return ImportResult::Malformed;
    }
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
    _tx.setImportTime(utcTime());
    UpgradableGuard l(m_lock);
    ImportResult verify_ret = verify(_tx);
    if (verify_ret == ImportResult::Success)
    {
        UpgradeGuard ul(l);
        insert(_tx);
        /// drop the oversized transactions
        while (m_txsQueue.size() > m_limit)
        {
            removeTrans(m_txsQueue.rbegin()->sha3());
        }
    }
    return ImportResult::Success;
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
        return ImportResult::AlreadyKnown;
    if (m_dropped.count(tx_hash) && _drop_policy == IfDropped::Ignore)
        return ImportResult::AlreadyInChain;
    /// check nonce
    if (false == isNonceOk(trans, _needinsert))
        return ImportResult::NonceCheckFail;
    /// check block limit
    if (false == isBlockLimitOk(trans))
        return ImportResult::BlockLimitCheckFail;
    /// TODO: filter check
    return ImportResult::Success;
}

/**
 * @brief : check the block limit
 * @param _tx : specified transaction to be checked
 * @return true : block limit of the given transaction is valid
 * @return false : block limit of the given transaction is invalid
 */
bool inline TxPool::isBlockLimitOk(Transaction const& _tx) const
{
    if (_tx.blockLimit() == Invalid256 || m_blockManager->number() >= _tx.blockLimit() ||
        _tx.blockLimit() > (m_blockManager->number() + m_maxBlockLimit))
    {
        LOG(ERROR) << "TxPool::isBlockLimitOk Fail! t.sha3()=" << _tx.sha3()
                   << ", t.blockLimit=" << _tx.blockLimit()
                   << ", number() = " << m_blockManager->number()
                   << ", maxBlockLimit = " << m_maxBlockLimit;
        return false;
    }
    return true;
}

/**
 * @brief: check the nonce
 * @param _tx : the transaction to be checked
 * @param _needInsert : insert transaction nonce to cache after checked
 * @return true : valid nonce
 * @return false : invalid nonce
 */
bool inline TxPool::isNonceOk(Transaction const& _tx, bool _needInsert) const
{
    if (_tx.nonce() == Invalid256 || (!m_blockManager->isNonceOk(_tx, _needInsert)))
    {
        LOG(ERROR) << "TxPool::isNonceOk Fail!  t.sha3()= " << _tx.sha3()
                   << ", t.nonce = " << _tx.nonce();
        return false;
    }
    return true;
}

/**
 * @brief : remove latest transactions from queue after the transaction queue overloaded
 */
bool TxPool::removeTrans(h256 const& _txHash)
{
    auto p_tx = m_txsHash.find(_txHash);
    if (p_tx == m_txsHash.end())
        return false;
    m_txsHash.erase(p_tx);
    if (m_known.count(_txHash))
        m_known.erase(_txHash);
    m_txsQueue.erase(p_tx->second);
    LOG(WARNING) << "Dropping out of bounds transaction, TransactionQueue::removeOutofBoundsTrans: "
                 << toString(_txHash);
    return true;
}

/**
 * @brief : insert the newest transaction into the transaction queue
 * @param _tx: the give transaction queue can be inserted to the transaction queue
 */
void TxPool::insert(Transaction const& _tx)
{
    h256 tx_hash = _tx.sha3();
    if (m_txsHash.count(tx_hash))
    {
        LOG(WARNING) << "Transaction " << tx_hash << " already in transcation queue";
        return;
    }
    m_known.insert(tx_hash);
    PriorityQueue::iterator p_tx = m_txsQueue.emplace(_tx);
    m_txsHash[tx_hash] = p_tx;
    LOG(INFO) << " insert tx.sha3 = " << (tx_hash) << ", Randomid= " << (_tx.nonce())
              << " , time = " << utcTime();
}

/**
 * @brief Remove transaction from the queue
 * @param _txHash: transaction hash
 */
bool TxPool::drop(h256 const& _txHash)
{
    UpgradableGuard l(m_lock);
    if (!m_known.count(_txHash))
        return false;
    UpgradeGuard ul(l);
    m_dropped.insert(_txHash);
    return removeTrans(_txHash);
}

/**
 * @brief Get top transactions from the queue
 *
 * @param _limit : _limit Max number of transactions to return.
 * @param _avoid : Transactions to avoid returning.
 * @return Transactions : up to _limit transactions
 */
Transactions TxPool::topTransactions(unsigned _limit, h256Hash const& _avoid) const
{
    ReadGuard l(m_lock);
    Transactions ret;
    for (auto it = m_txsQueue.begin(); ret.size() < m_limit && it != m_txsQueue.end(); it++)
    {
        if (!_avoid.count(it->sha3()))
        {
            ret.push_back(*it);
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
}
}  // namespace txpool
}  // namespace dev
