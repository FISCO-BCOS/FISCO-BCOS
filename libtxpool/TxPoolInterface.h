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
 * @brief : interface of transaction pool
 * @file : TxPoolInterface.h
 * @author: yujiechen
 * @date: 2018-09-21
 */
#pragma once
#include <libethcore/Block.h>
#include <libethcore/Common.h>
#include <libethcore/Protocol.h>
#include <libethcore/Transaction.h>
namespace bcos
{
namespace txpool
{
struct TxPoolStatus;
class TxPoolInterface
{
public:
    using Ptr = std::shared_ptr<TxPoolInterface>;
    TxPoolInterface() = default;
    virtual ~TxPoolInterface(){};
    void setMaxBlockLimit(unsigned const&) {}
    /**
     * @brief Remove transaction from the queue
     * @param _txHash: transaction hash
     */
    virtual bool drop(h256 const& _txHash) = 0;
    virtual bool dropBlockTrans(std::shared_ptr<bcos::eth::Block> block) = 0;
    virtual bool handleBadBlock(bcos::eth::Block const& block) = 0;
    /**
     * @brief Get top transactions from the queue
     *
     * @param _limit : _limit Max number of transactions to return.
     * @param _avoid : Transactions to avoid returning.
     * @param _condition : The function return false to avoid transaction to return.
     * @return Transactions : up to _limit transactions
     */
    virtual std::shared_ptr<bcos::eth::Transactions> topTransactions(uint64_t const& _limit) = 0;
    virtual std::shared_ptr<bcos::eth::Transactions> topTransactions(
        uint64_t const& _limit, h256Hash& _avoid, bool _updateAvoid = false) = 0;

    /// param 1: the transaction limit
    /// param 2: the node id
    virtual std::shared_ptr<bcos::eth::Transactions> topTransactionsCondition(
        uint64_t const&, bcos::h512 const&)
    {
        return std::make_shared<bcos::eth::Transactions>();
    };

    /// get all current transactions(maybe blocksync module need this interface)
    virtual std::shared_ptr<bcos::eth::Transactions> pendingList() const = 0;
    /// get current transaction num
    virtual size_t pendingSize() = 0;

    /**
     * @brief submit a transaction through RPC
     * @param _t : transaction
     * @return std::pair<h256, Address>: maps from transaction hash to contract address
     */
    virtual std::pair<h256, Address> submit(std::shared_ptr<bcos::eth::Transaction> _tx) = 0;
    virtual std::pair<h256, Address> submitTransactions(bcos::eth::Transaction::Ptr)
    {
        return std::make_pair(h256(), Address());
    };

    /**
     * @brief : submit a transaction through p2p, Verify and add transaction to the queue
     * synchronously.
     * @param _tx : Trasaction data.
     * @param _ik : Set to Retry to force re-addinga transaction that was previously dropped.
     * @return ImportResult : Import result code.
     */
    virtual bcos::eth::ImportResult import(
        bcos::eth::Transaction::Ptr, bcos::eth::IfDropped _ik = bcos::eth::IfDropped::Ignore) = 0;
    /// @returns the status of the transaction queue.
    virtual TxPoolStatus status() const = 0;

    /// protocol id used when register handler to p2p module
    virtual PROTOCOL_ID const& getProtocolId() const = 0;

    /// Register a handler that will be called once there is a new transaction imported
    template <class T>
    bcos::eth::Handler<> onReady(T const& _t)
    {
        return m_onReady.add(_t);
    }

    /// param: transaction hash
    /// determine the given transaction hash exists in the transaction pool or not
    virtual bool txExists(bcos::h256 const&) { return false; }

    /// param: the block that should be verified and set sender according to transactions of local
    /// transaction pool
    virtual void verifyAndSetSenderForBlock(bcos::eth::Block&) {}

    virtual bool isFull() { return false; }
    virtual void start() {}
    virtual void stop() {}

    virtual std::shared_ptr<bcos::eth::Transactions> obtainTransactions(
        std::vector<bcos::h256> const&)
    {
        return nullptr;
    }
    virtual std::shared_ptr<std::vector<bcos::h256>> filterUnknownTxs(
        std::set<bcos::h256> const&, bcos::h512 const&)
    {
        return nullptr;
    }

    virtual bool initPartiallyBlock(std::shared_ptr<bcos::eth::Block>) { return true; }
    virtual void registerSyncStatusChecker(std::function<bool()>) {}

    // when the node is changed from sealer/observer node to free-node,
    // Update the status of all transactions,  so that when the node becomes an observer or a
    // sealer, broadcast the remaining transactions in the transaction pool to other nodes
    virtual void freshTxsStatus() {}

    virtual unsigned maxBlockLimit() const { return 1000; }

protected:
    ///< Called when a subsequent call to import transactions will return a non-empty container. Be
    ///< nice and exit fast.
    bcos::eth::Signal<> m_onReady;
};
}  // namespace txpool
}  // namespace bcos
