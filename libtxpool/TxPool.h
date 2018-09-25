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
 * @brief : transaction pool
 * @file: TxPool.h
 * @author: yujiechen
 * @date: 2018-09-23
 */
#pragma once
#include "TxPoolInterface.h"
#include <libblockmanager/BlockManagerInterface.h>
#include <libblockmanager/NonceCheck.h>
#include <libdevcore/easylog.h>
#include <libethcore/Common.h>
#include <libethcore/Transaction.h>
#include <libp2p/Service.h>
using namespace dev::eth;
using namespace dev::p2p;
namespace dev
{
namespace txpool
{
class TxPool;

struct TxPoolStatus
{
    size_t current;
    size_t dropped;
};

struct PriorityCompare
{
    /// Compare transaction by nonce height and gas price.
    bool operator()(Transaction const& _first, Transaction const& _second) const
    {
        return _first.importTime() <= _second.importTime();
    }
};
class TxPool : public TxPoolInterface, public std::enable_shared_from_this<TxPool>
{
public:
    TxPool(std::shared_ptr<dev::p2p::Service> _p2pService,
        std::shared_ptr<dev::blockmanager::BlockManagerInterface> _blockManager,
        uint64_t const& _limit = 102400, int32_t const& _protocolId = 4)
      : m_service(_p2pService),
        m_blockManager(_blockManager),
        m_limit(_limit),
        m_protocolId(_protocolId)
    {
        if (!m_service)
        {
            LOG(ERROR) << "p2p service hasn't been inited, exit now!";
            exit(-1);
        }
        if (!m_blockManager)
        {
            LOG(ERROR) << "BlockManager hasn't been setted, exit now!";
            exit(-1);
        }
        /// register enqueue interface to p2p by protocalID
        m_service->registerHandlerByProtoclID(
            m_protocolId, boost::bind(&TxPool::enqueue, this, _1, _2, _3));
    }

    virtual ~TxPool() { clear(); }

    /**
     * @brief submit a transaction through RPC/web3sdk
     *
     * @param _t : transaction
     * @return std::pair<h256, Address> : maps from transaction hash to contract address
     */
    std::pair<h256, Address> submit(Transaction& _tx) override;

    /**
     * @brief Remove transaction from the queue
     * @param _txHash: transaction hash
     */
    bool drop(h256 const& _txHash) override;
    /**
     * @brief Get top transactions from the queue
     *
     * @param _limit : _limit Max number of transactions to return.
     * @param _avoid : Transactions to avoid returning.
     * @return Transactions : up to _limit transactions
     */
    Transactions topTransactions(
        unsigned _limit, h256Hash const& _avoid = h256Hash()) const override;

    /// get all transactions(maybe blocksync module need this interface)
    Transactions pendingList() const override;
    /// get current transaction num
    size_t pendingSize() override;

    /// @returns the status of the transaction queue.
    TxPoolStatus status() const override;

    /// protocol id used when register handler to p2p module
    virtual int32_t const& getProtocolId() const { return m_protocolId; }
    virtual void setProtocolId(uint32_t const& _protocolId) { m_protocolId = _protocolId; }
    virtual void setMaxBlockLimit(u256 const& _maxBlockLimit) { m_maxBlockLimit = _maxBlockLimit; }
    virtual const u256 maxBlockLimit() const { return m_maxBlockLimit; }

    uint64_t number() const { return m_blockManager->number(); }

protected:
    /**
     * @brief : submit a transaction through p2p, Verify and add transaction to the queue
     * synchronously.
     *
     * @param _tx : Trasnaction data.
     * @param _ik : Set to Retry to force re-addinga transaction that was previously dropped.
     * @return ImportResult : Import result code.
     */
    ImportResult import(Transaction& _tx, IfDropped _ik = IfDropped::Ignore) override;
    ImportResult import(bytesConstRef _txBytes, IfDropped _ik = IfDropped::Ignore) override;
    /// obtain a transaction from lower network
    void enqueue(
        dev::p2p::P2PException exception, std::shared_ptr<Session> session, Message::Ptr pMessage);
    /// verify transcation
    virtual ImportResult verify(
        Transaction const& trans, IfDropped _ik = IfDropped::Ignore, bool _needinsert = false);
    /// check block limit
    virtual bool inline isBlockLimitOk(Transaction const& _ts) const;
    /// check nonce
    virtual bool inline isNonceOk(Transaction const& _ts, bool _needinsert) const;
    /// interface for filter check
    virtual u256 filterCheck(const Transaction& _t) const { return u256(0); };
    void clear();

private:
    bool removeTrans(h256 const& _txHash);
    void insert(Transaction const& _tx);

private:
    /// p2p module
    std::shared_ptr<dev::p2p::Service> m_service;
    std::shared_ptr<dev::blockmanager::BlockManagerInterface> m_blockManager;
    /// Max number of pending transactions
    uint64_t m_limit;
    mutable SharedMutex m_lock;
    /// protocolId
    int32_t m_protocolId;
    /// max block limit
    u256 m_maxBlockLimit = 1000;
    /// transaction queue
    using PriorityQueue = std::multiset<Transaction, PriorityCompare>;
    PriorityQueue m_txsQueue;
    std::unordered_map<h256, PriorityQueue::iterator> m_txsHash;
    /// hash of imported transactions
    h256Hash m_known;
    /// hash of dropped transactions
    h256Hash m_dropped;
};  // namespace txpool
}  // namespace txpool
}  // namespace dev
