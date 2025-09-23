/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file TxPoolInterface.h
 * @author: yujiechen
 * @date: 2021-04-07
 */
#pragma once
#include "../consensus/ConsensusNode.h"
#include "../protocol/Block.h"
#include "../protocol/Transaction.h"
#include "../protocol/TransactionSubmitResult.h"
#include "TxPoolTypeDef.h"
#include <bcos-task/Task.h>
#include <bcos-utilities/Error.h>
#include <boost/throw_exception.hpp>
#include <stdexcept>

namespace bcos::txpool
{
class TxPoolInterface
{
public:
    using Ptr = std::shared_ptr<TxPoolInterface>;

    TxPoolInterface() = default;
    virtual ~TxPoolInterface() = default;

    virtual void start() = 0;
    virtual void stop() = 0;

    /**
     * @brief submit a transaction
     *
     * @param transaction the transaction to be submitted
     * @return protocol::TransactionSubmitResult::Ptr
     */
    virtual task::Task<protocol::TransactionSubmitResult::Ptr> submitTransaction(
        [[maybe_unused]] protocol::Transaction::Ptr transaction)
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("Unimplemented!"));
    }

    virtual task::Task<protocol::TransactionSubmitResult::Ptr> submitTransactionWithoutReceipt(
        [[maybe_unused]] protocol::Transaction::Ptr transaction)
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("Unimplemented!"));
    }

    virtual task::Task<protocol::TransactionSubmitResult::Ptr> submitTransactionWithHook(
        [[maybe_unused]] protocol::Transaction::Ptr transaction,
        [[maybe_unused]] std::function<void()> afterInsertHook)
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("Unimplemented!"));
    }

    virtual task::Task<void> broadcastTransaction(
        [[maybe_unused]] const protocol::Transaction& transaction)
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("Unimplemented!"));
    }

    virtual task::Task<void> broadcastTransactionBuffer([[maybe_unused]] bytesConstRef _data)
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("Unimplemented!"));
    }

    virtual task::Task<void> broadcastTransactionBufferByTree([[maybe_unused]] bytesConstRef _data,
        bool isStartNode = false, bcos::crypto::NodeIDPtr fromNode = nullptr)
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("Unimplemented!"));
    }

    virtual task::Task<std::vector<protocol::Transaction::ConstPtr>> getTransactions(
        RANGES::any_view<bcos::h256, RANGES::category::mask | RANGES::category::sized> hashes)
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("Unimplemented!"));
    }

    virtual task::Task<std::optional<u256>> getWeb3PendingNonce(std::string_view address)
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("Unimplemented!"));
    }

    virtual bool existsInGroup(bcos::crypto::NodeIDPtr _nodeId)
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("Unimplemented!"));
    }

    /**
     * @brief fetch transactions from the txpool
     *
     * @param _txsLimit the max number of the transactions to be fetch
     * @param _avoidTxs list of transactions that need to be filtered
     * @param _sealCallback after the  txpool responds to the sealed txs, the callback is
     * triggered
     */
    virtual void asyncSealTxs(uint64_t _txsLimit, TxsHashSetPtr _avoidTxs,
        std::function<void(Error::Ptr, bcos::protocol::Block::Ptr, bcos::protocol::Block::Ptr)>
            _sealCallback) = 0;

    virtual void asyncMarkTxs(const bcos::crypto::HashList& _txsHash, bool _sealedFlag,
        bcos::protocol::BlockNumber _batchId, bcos::crypto::HashType const& _batchHash,
        std::function<void(Error::Ptr)> _onRecvResponse) = 0;
    /**
     * @brief verify transactions in Block for the consensus module
     *
     * @param _generatedNodeID the NodeID of the leader(When missing transactions, need to obtain
     * the missing transactions from Leader)
     * @param _blocks the block to be verified
     * @param _onVerifyFinished callback to be called after the block verification is over
     */
    virtual void asyncVerifyBlock(bcos::crypto::PublicPtr _generatedNodeID,
        bytesConstRef const& _block, std::function<void(Error::Ptr, bool)> _onVerifyFinished) = 0;

    /**
     * @brief The dispatcher obtains the transaction list corresponding to the block from the
     * transaction pool
     *
     * @param _block the block to be filled with transactions
     * @param _onBlockFilled callback to be called after the block has been filled
     */
    virtual void asyncFillBlock(bcos::crypto::HashListPtr _txsHash,
        std::function<void(Error::Ptr, bcos::protocol::ConstTransactionsPtr)> _onBlockFilled) = 0;

    /**
     * @brief After the blockchain is on-chain, the interface is called to notify the transaction
     * receipt and other information
     *
     * @param _onChainBlock Including transaction receipt, on-chain transaction hash list, block
     * header information
     * @param _onChainCallback
     */
    virtual void asyncNotifyBlockResult(bcos::protocol::BlockNumber _blockNumber,
        bcos::protocol::TransactionSubmitResultsPtr _txsResult,
        std::function<void(Error::Ptr)> _onNotifyFinished) = 0;

    // called by frontService to dispatch message
    virtual void asyncNotifyTxsSyncMessage(bcos::Error::Ptr _error, std::string const& _id,
        bcos::crypto::NodeIDPtr _nodeID, bytesConstRef _data,
        std::function<void(Error::Ptr _error)> _onRecv) = 0;
    virtual void notifyConsensusNodeList(
        bcos::consensus::ConsensusNodeList const& _consensusNodeList,
        std::function<void(Error::Ptr)> _onRecvResponse) = 0;
    virtual void notifyObserverNodeList(bcos::consensus::ConsensusNodeList const& _observerNodeList,
        std::function<void(Error::Ptr)> _onRecvResponse) = 0;

    // for RPC to get pending transactions
    virtual void asyncGetPendingTransactionSize(
        std::function<void(Error::Ptr, uint64_t)> _onGetTxsSize) = 0;

    // notify to reset the txpool when the consensus module startup
    virtual void asyncResetTxPool(std::function<void(Error::Ptr)> _onRecvResponse) = 0;

    virtual void notifyConnectedNodes(bcos::crypto::NodeIDSet const& _connectedNodes,
        std::function<void(Error::Ptr)> _onResponse) = 0;

    // determine to clean up txs periodically or not
    virtual void registerTxsCleanUpSwitch(std::function<bool()>) {}
    virtual void clearAllTxs() {}

    virtual void tryToSyncTxsFromPeers() {}
};

}  // namespace bcos::txpool
