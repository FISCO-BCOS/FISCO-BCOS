/**
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
 * @brief implementation for txpool
 * @file TxPool.h
 * @author: yujiechen
 * @date 2021-05-10
 */
#pragma once
#include "TxPoolConfig.h"
#include "bcos-framework/front/FrontServiceInterface.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionFactory.h"
#include "sync/interfaces/TransactionSyncInterface.h"
#include "txpool/interfaces/TxPoolStorageInterface.h"
#include <bcos-framework/txpool/TxPoolInterface.h>
#include <bcos-tool/TreeTopology.h>
#include <bcos-utilities/ThreadPool.h>
#include <thread>
namespace bcos::txpool
{
class TxPool : public TxPoolInterface, public std::enable_shared_from_this<TxPool>
{
public:
    using Ptr = std::shared_ptr<TxPool>;
    TxPool(TxPoolConfig::Ptr config, TxPoolStorageInterface::Ptr txpoolStorage,
        bcos::sync::TransactionSyncInterface::Ptr transactionSync, size_t verifierWorkerNum = 1);

    ~TxPool() noexcept override;
    void start() override;
    void stop() override;

    // New interfaces ==================
    task::Task<protocol::TransactionSubmitResult::Ptr> submitTransaction(
        protocol::Transaction::Ptr transaction) override;

    task::Task<protocol::TransactionSubmitResult::Ptr> submitTransactionWithHook(
        protocol::Transaction::Ptr transaction, std::function<void()> onTxSubmitted) override;

    void broadcastTransaction(const protocol::Transaction& transaction) override;
    void broadcastTransactionBuffer(const bytesConstRef& _data) override;
    void broadcastTransactionBufferByTree(const bcos::bytesConstRef& _data,
        bool isStartNode = false, bcos::crypto::NodeIDPtr fromNode = nullptr) override;

    task::Task<std::vector<protocol::Transaction::ConstPtr>> getTransactions(
        RANGES::any_view<bcos::h256, RANGES::category::mask | RANGES::category::sized> hashes)
        override;
    // ===============================

    // sealer module call this method for seal a block
    void asyncSealTxs(uint64_t _txsLimit, TxsHashSetPtr _avoidTxs,
        std::function<void(Error::Ptr, bcos::protocol::Block::Ptr, bcos::protocol::Block::Ptr)>
            _sealCallback) override;

    // hook for scheduler, invoke notify when block execute finished
    void asyncNotifyBlockResult(bcos::protocol::BlockNumber _blockNumber,
        bcos::protocol::TransactionSubmitResultsPtr _txsResult,
        std::function<void(Error::Ptr)> _onNotifyFinished) override;

    // for consensus module, to verify whether block's txs in txpool or not, invoke when consensus
    // receive proposal
    void asyncVerifyBlock(bcos::crypto::PublicPtr _generatedNodeID, bytesConstRef const& _block,
        std::function<void(Error::Ptr, bool)> _onVerifyFinished) override;

    // hook for tx/consensus sync message receive
    void asyncNotifyTxsSyncMessage(bcos::Error::Ptr _error, std::string const& _uuid,
        bcos::crypto::NodeIDPtr _nodeID, bytesConstRef _data,
        std::function<void(Error::Ptr)> _onRecv) override;

    // hook for validator, update consensus nodes info in config
    // Note: txpool should use this interface to update consensus nodes info
    // use this nodes info to check whether tx is from group member or not
    void notifyConsensusNodeList(bcos::consensus::ConsensusNodeList const& _consensusNodeList,
        std::function<void(Error::Ptr)> _onRecvResponse) override;

    // hook for validator, update observer nodes info in config
    // Note: txpool should use this interface to update observer nodes info
    // use this nodes info to check whether tx is from group member or not
    void notifyObserverNodeList(bcos::consensus::ConsensusNodeList const& _observerNodeList,
        std::function<void(Error::Ptr)> _onRecvResponse) override;

    // for scheduler to fetch txs
    void asyncFillBlock(bcos::crypto::HashListPtr _txsHash,
        std::function<void(Error::Ptr, bcos::protocol::TransactionsPtr)> _onBlockFilled) override;

    // for consensus and sealer, for batch mark txs sealed flag
    // trigger scene such as view change, submit proposal, etc.
    void asyncMarkTxs(bcos::crypto::HashListPtr _txsHash, bool _sealedFlag,
        bcos::protocol::BlockNumber _batchId, bcos::crypto::HashType const& _batchHash,
        std::function<void(Error::Ptr)> _onRecvResponse) override;

    void asyncResetTxPool(std::function<void(Error::Ptr)> _onRecvResponse) override;

    TxPoolConfig::Ptr txpoolConfig();
    TxPoolStorageInterface::Ptr txpoolStorage();

    bcos::sync::TransactionSyncInterface::Ptr& transactionSync();
    void setTransactionSync(bcos::sync::TransactionSyncInterface::Ptr _transactionSync);

    virtual void init();
    virtual void registerUnsealedTxsNotifier(
        std::function<void(size_t, std::function<void(Error::Ptr)>)> _unsealedTxsNotifier);

    void asyncGetPendingTransactionSize(
        std::function<void(Error::Ptr, uint64_t)> _onGetTxsSize) override;

    void notifyConnectedNodes(bcos::crypto::NodeIDSet const& _connectedNodes,
        std::function<void(Error::Ptr)> _onResponse) override;

    void tryToSyncTxsFromPeers() override;

    // for UT
    void setTxPoolStorage(TxPoolStorageInterface::Ptr _txpoolStorage);

    void registerTxsCleanUpSwitch(std::function<bool()> _txsCleanUpSwitch) override;

    void clearAllTxs() override;

    void setTreeRouter(bcos::tool::TreeTopology::Ptr _treeRouter)
    {
        m_treeRouter = std::move(_treeRouter);
    }

    auto treeRouter() const { return m_treeRouter; }

protected:
    virtual bool checkExistsInGroup(bcos::protocol::TxSubmitCallback _txSubmitCallback);
    virtual void getTxsFromLocalLedger(bcos::crypto::HashListPtr _txsHash,
        bcos::crypto::HashListPtr _missedTxs,
        std::function<void(Error::Ptr, bcos::protocol::TransactionsPtr)> _onBlockFilled);

    virtual void fillBlock(bcos::crypto::HashListPtr _txsHash,
        std::function<void(Error::Ptr, bcos::protocol::TransactionsPtr)> _onBlockFilled,
        bool _fetchFromLedger = true);

    void initSendResponseHandler();

    virtual void storeVerifiedBlock(bcos::protocol::Block::Ptr _block);

private:
    TxPoolConfig::Ptr m_config;
    TxPoolStorageInterface::Ptr m_txpoolStorage;
    bcos::sync::TransactionSyncInterface::Ptr m_transactionSync;
    bcos::protocol::TransactionFactory::Ptr m_transactionFactory;
    bcos::ledger::LedgerInterface::Ptr m_ledger;

    std::function<void(std::string const&, int, bcos::crypto::NodeIDPtr, bytesConstRef)>
        m_sendResponseHandler;

    ThreadPool::Ptr m_worker;
    ThreadPool::Ptr m_verifier;
    ThreadPool::Ptr m_sealer;
    ThreadPool::Ptr m_txsPreStore;
    tool::TreeTopology::Ptr m_treeRouter = nullptr;
    std::atomic_bool m_running = {false};

    // Note: This x_markTxsMutex is used for locking asyncSealTxs() during sealBlock
    // because memory storage is not contain a big lock now
    mutable bcos::SharedMutex x_markTxsMutex;
};
}  // namespace bcos::txpool
