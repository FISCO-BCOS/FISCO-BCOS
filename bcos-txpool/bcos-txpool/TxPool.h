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
#include "bcos-txpool/TxPoolConfig.h"
#include "bcos-txpool/sync/interfaces/TransactionSyncInterface.h"
#include "bcos-txpool/txpool/interfaces/TxPoolStorageInterface.h"
#include <bcos-framework/interfaces/txpool/TxPoolInterface.h>
#include <bcos-utilities/ThreadPool.h>
#include <thread>
namespace bcos
{
namespace txpool
{
class TxPool : public TxPoolInterface, public std::enable_shared_from_this<TxPool>
{
public:
    using Ptr = std::shared_ptr<TxPool>;
    TxPool(TxPoolConfig::Ptr _config, TxPoolStorageInterface::Ptr _txpoolStorage,
        bcos::sync::TransactionSyncInterface::Ptr _transactionSync, size_t _verifierWorkerNum = 1)
      : m_config(_config), m_txpoolStorage(_txpoolStorage), m_transactionSync(_transactionSync)
    {
        // threadpool for submit txs
        m_worker = std::make_shared<ThreadPool>("submitter", _verifierWorkerNum);
        // threadpool for verify block
        m_verifier = std::make_shared<ThreadPool>("verifier", 4);
        m_sealer = std::make_shared<ThreadPool>("txsSeal", 1);
        m_filler = std::make_shared<ThreadPool>("txsFiller", std::thread::hardware_concurrency());
        TXPOOL_LOG(INFO) << LOG_DESC("create TxPool")
                         << LOG_KV("submitterWorkerNum", _verifierWorkerNum);
    }

    ~TxPool() override { stop(); }

    void start() override;
    void stop() override;

    void asyncSubmit(
        bytesPointer _txData, bcos::protocol::TxSubmitCallback _txSubmitCallback) override;

    void asyncSealTxs(uint64_t _txsLimit, TxsHashSetPtr _avoidTxs,
        std::function<void(Error::Ptr, bcos::protocol::Block::Ptr, bcos::protocol::Block::Ptr)>
            _sealCallback) override;

    void asyncNotifyBlockResult(bcos::protocol::BlockNumber _blockNumber,
        bcos::protocol::TransactionSubmitResultsPtr _txsResult,
        std::function<void(Error::Ptr)> _onNotifyFinished) override;

    void asyncVerifyBlock(bcos::crypto::PublicPtr _generatedNodeID, bytesConstRef const& _block,
        std::function<void(Error::Ptr, bool)> _onVerifyFinished) override;

    void asyncNotifyTxsSyncMessage(bcos::Error::Ptr _error, std::string const& _uuid,
        bcos::crypto::NodeIDPtr _nodeID, bytesConstRef _data,
        std::function<void(Error::Ptr _error)> _onRecv) override;

    void notifyConsensusNodeList(bcos::consensus::ConsensusNodeList const& _consensusNodeList,
        std::function<void(Error::Ptr)> _onRecvResponse) override;

    void asyncFillBlock(bcos::crypto::HashListPtr _txsHash,
        std::function<void(Error::Ptr, bcos::protocol::TransactionsPtr)> _onBlockFilled) override;

    void notifyObserverNodeList(bcos::consensus::ConsensusNodeList const& _observerNodeList,
        std::function<void(Error::Ptr)> _onRecvResponse) override;

    void asyncMarkTxs(bcos::crypto::HashListPtr _txsHash, bool _sealedFlag,
        bcos::protocol::BlockNumber _batchId, bcos::crypto::HashType const& _batchHash,
        std::function<void(Error::Ptr)> _onRecvResponse) override;

    void asyncResetTxPool(std::function<void(Error::Ptr)> _onRecvResponse) override;

    TxPoolConfig::Ptr txpoolConfig() { return m_config; }
    TxPoolStorageInterface::Ptr txpoolStorage() { return m_txpoolStorage; }

    bcos::sync::TransactionSyncInterface::Ptr transactionSync() { return m_transactionSync; }
    void setTransactionSync(bcos::sync::TransactionSyncInterface::Ptr _transactionSync)
    {
        m_transactionSync = _transactionSync;
    }

    virtual void init();
    virtual void registerUnsealedTxsNotifier(
        std::function<void(size_t, std::function<void(Error::Ptr)>)> _unsealedTxsNotifier)
    {
        m_txpoolStorage->registerUnsealedTxsNotifier(_unsealedTxsNotifier);
    }

    void asyncGetPendingTransactionSize(
        std::function<void(Error::Ptr, uint64_t)> _onGetTxsSize) override
    {
        if (!_onGetTxsSize)
        {
            return;
        }
        auto pendingTxsSize = m_txpoolStorage->size();
        _onGetTxsSize(nullptr, pendingTxsSize);
    }

    void notifyConnectedNodes(bcos::crypto::NodeIDSet const& _connectedNodes,
        std::function<void(Error::Ptr)> _onResponse) override
    {
        m_transactionSync->config()->notifyConnectedNodes(_connectedNodes, _onResponse);
        if (m_txpoolStorage->size() > 0)
        {
            return;
        }
        // try to broadcast empty txsStatus and request txs from the connected nodes when the txpool
        // is empty
        m_transactionSync->onEmptyTxs();
    }

    // for UT
    void setTxPoolStorage(TxPoolStorageInterface::Ptr _txpoolStorage)
    {
        m_txpoolStorage = _txpoolStorage;
        m_transactionSync->config()->setTxPoolStorage(_txpoolStorage);
    }

protected:
    virtual bool checkExistsInGroup(bcos::protocol::TxSubmitCallback _txSubmitCallback);
    virtual void getTxsFromLocalLedger(bcos::crypto::HashListPtr _txsHash,
        bcos::crypto::HashListPtr _missedTxs,
        std::function<void(Error::Ptr, bcos::protocol::TransactionsPtr)> _onBlockFilled);

    virtual void fillBlock(bcos::crypto::HashListPtr _txsHash,
        std::function<void(Error::Ptr, bcos::protocol::TransactionsPtr)> _onBlockFilled,
        bool _fetchFromLedger = true);

    void initSendResponseHandler();

private:
    TxPoolConfig::Ptr m_config;
    TxPoolStorageInterface::Ptr m_txpoolStorage;
    bcos::sync::TransactionSyncInterface::Ptr m_transactionSync;

    std::function<void(std::string const& _id, int _moduleID, bcos::crypto::NodeIDPtr _dstNode,
        bytesConstRef _data)>
        m_sendResponseHandler;

    ThreadPool::Ptr m_worker;
    ThreadPool::Ptr m_verifier;
    ThreadPool::Ptr m_sealer;
    ThreadPool::Ptr m_filler;
    std::atomic_bool m_running = {false};
};
}  // namespace txpool
}  // namespace bcos
