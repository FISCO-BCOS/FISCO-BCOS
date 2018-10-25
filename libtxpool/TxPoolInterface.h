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
namespace dev
{
namespace txpool
{
class TxPoolStatus;
class TxPoolInterface
{
public:
    TxPoolInterface() = default;
    virtual ~TxPoolInterface(){};

    /**
     * @brief Remove transaction from the queue
     * @param _txHash: transaction hash
     */
    virtual bool drop(h256 const& _txHash) = 0;
    virtual bool dropBlockTrans(dev::eth::Block const& block) = 0;
    /**
     * @brief Get top transactions from the queue
     *
     * @param _limit : _limit Max number of transactions to return.
     * @param _avoid : Transactions to avoid returning.
     * @param _condition : The function return false to avoid transaction to return.
     * @return Transactions : up to _limit transactions
     */
    virtual dev::eth::Transactions topTransactions(uint64_t const& _limit) = 0;
    virtual dev::eth::Transactions topTransactions(
        uint64_t const& _limit, h256Hash& _avoid, bool _updateAvoid = false) = 0;
    virtual dev::eth::Transactions topTransactionsCondition(uint64_t const& _limit,
        std::function<bool(dev::eth::Transaction const&)> const& _condition = nullptr)
    {
        return dev::eth::Transactions();
    };

    /// get all current transactions(maybe blocksync module need this interface)
    virtual dev::eth::Transactions pendingList() const = 0;
    /// get current transaction num
    virtual size_t pendingSize() = 0;

    /**
     * @brief submit a transaction through RPC
     * @param _t : transaction
     * @return std::pair<h256, Address>: maps from transaction hash to contract address
     */
    virtual std::pair<h256, Address> submit(dev::eth::Transaction& _tx) = 0;

    /**
     * @brief : submit a transaction through p2p, Verify and add transaction to the queue
     * synchronously.
     * @param _tx : Trasaction data.
     * @param _ik : Set to Retry to force re-addinga transaction that was previously dropped.
     * @return ImportResult : Import result code.
     */
    virtual dev::eth::ImportResult import(
        dev::eth::Transaction& _tx, dev::eth::IfDropped _ik = dev::eth::IfDropped::Ignore) = 0;
    virtual dev::eth::ImportResult import(
        bytesConstRef _txBytes, dev::eth::IfDropped _ik = dev::eth::IfDropped::Ignore) = 0;
    /// @returns the status of the transaction queue.
    virtual TxPoolStatus status() const = 0;

    /// protocol id used when register handler to p2p module
    virtual PROTOCOL_ID const& getProtocolId() const = 0;

    /// Get transaction in TxPool, return nullptr when not found
    virtual std::shared_ptr<dev::eth::Transaction const> transactionInPool(h256 const& _txHash)
    {
        return nullptr;
    };

    /// Set transaction is known by a node
    virtual void transactionIsKonwnBy(h256 const& _txHash, h512 const& _nodeId){};

    /// Is the transaction is known by the node ?
    virtual bool isTransactionKonwnBy(h256 const& _txHash, h512 const& _nodeId) { return false; };

    /// Is the transaction is known by someone
    virtual bool isTransactionKonwnBySomeone(h256 const& _txHash) { return false; };

    /// Register a handler that will be called once there is a new transaction imported
    template <class T>
    dev::eth::Handler<> onReady(T const& _t)
    {
        return m_onReady.add(_t);
    }

protected:
    ///< Called when a subsequent call to import transactions will return a non-empty container. Be
    ///< nice and exit fast.
    dev::eth::Signal<> m_onReady;
};
}  // namespace txpool
}  // namespace dev
