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
 * @brief fixture for the txpool
 * @file TxPoolFixture.h
 * @author: yujiechen
 * @date 2021-05-25
 */
#pragma once
#include "bcos-framework/bcos-framework/testutils/faker/FakeBlock.h"
#include "bcos-framework/bcos-framework/testutils/faker/FakeBlockHeader.h"
#include "bcos-framework/protocol/GlobalConfig.h"

#include "bcos-tool/TreeTopology.h"
#include "bcos-txpool/TxPoolConfig.h"
#include "bcos-txpool/TxPoolFactory.h"
#include "bcos-txpool/sync/TransactionSync.h"
#include "bcos-txpool/txpool/storage/MemoryStorage.h"
#include "bcos-txpool/txpool/validator/TxValidator.h"
#include <bcos-framework/consensus/ConsensusNode.h>
#include <bcos-framework/testutils/faker/FakeFrontService.h>
#include <bcos-framework/testutils/faker/FakeLedger.h>
#include <bcos-framework/testutils/faker/FakeSealer.h>
#include <bcos-protocol/TransactionSubmitResultFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <thread>

using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::crypto;
using namespace bcos::ledger;
using namespace bcos::consensus;
using namespace bcos::front;
using namespace bcos::sync;
using namespace bcos::tool;

namespace bcos
{
namespace test
{
class FakeTransactionSync1 : public TransactionSync
{
public:
    explicit FakeTransactionSync1(TransactionSyncConfig::Ptr _config) : TransactionSync(_config) {}
    ~FakeTransactionSync1() override = default;
};

class FakeTransactionSync : public FakeTransactionSync1
{
public:
    explicit FakeTransactionSync(TransactionSyncConfig::Ptr _config) : FakeTransactionSync1(_config)
    {}
    ~FakeTransactionSync() override {}
};

class FakeMemoryStorage : public MemoryStorage
{
public:
    FakeMemoryStorage(TxPoolConfig::Ptr _config, size_t _notifyWorkerNum = 2)
      : MemoryStorage(_config, _notifyWorkerNum)
    {}
};

class TxPoolFixture
{
public:
    using Ptr = std::shared_ptr<TxPoolFixture>;
    TxPoolFixture()
      : TxPoolFixture(std::make_shared<Secp256k1Crypto>()->generateKeyPair()->publicKey(),
            std::make_shared<CryptoSuite>(
                std::make_shared<Keccak256>(), std::make_shared<Secp256k1Crypto>(), nullptr),
            "groupId", "chainId", 100000000, std::make_shared<FakeGateWay>(), true)
    {}
    TxPoolFixture(NodeIDPtr _nodeId, CryptoSuite::Ptr _cryptoSuite, std::string _groupId,
        std::string _chainId, int64_t _blockLimit, FakeGateWay::Ptr _fakeGateWay,
        bool enableTree = false)
      : m_nodeId(_nodeId),
        m_cryptoSuite(_cryptoSuite),
        m_groupId(_groupId),
        m_chainId(std::move(_chainId)),
        m_blockLimit(_blockLimit),
        m_fakeGateWay(std::move(_fakeGateWay))
    {
        if (enableTree)
        {
            m_nodeId = std::make_shared<KeyImpl>(
                h256("1110000000000000000000000000000000000000000000000000000000000000").asBytes());
        }
        m_nodeId->hex();
        auto blockHeaderFactory =
            std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(_cryptoSuite);
        auto txFactory = std::make_shared<bcostars::protocol::TransactionFactoryImpl>(_cryptoSuite);
        auto receiptFactory =
            std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(_cryptoSuite);
        m_fakeGateWay->m_cryptoSuite = _cryptoSuite;
        m_blockFactory = std::make_shared<bcostars::protocol::BlockFactoryImpl>(
            _cryptoSuite, blockHeaderFactory, txFactory, receiptFactory);
        m_txResultFactory = std::make_shared<TransactionSubmitResultFactoryImpl>();
        m_ledger = std::make_shared<FakeLedger>(m_blockFactory, 20, 10, 10);
        m_ledger->setSystemConfig(ledger::SYSTEM_KEY_TX_COUNT_LIMIT, "1000");

        m_frontService = std::make_shared<FakeFrontService>(m_nodeId);
        if (enableTree)
        {
            auto protocol = std::make_shared<protocol::ProtocolInfo>();
            protocol->setVersion(V2);
            auto gInfo = std::make_shared<FakeGroupInfo>();
            gInfo->appendProtocol(protocol);
            m_frontService->setGroupInfo(gInfo);
        }
        auto txPoolFactory =
            std::make_shared<TxPoolFactory>(m_nodeId, _cryptoSuite, m_txResultFactory,
                m_blockFactory, m_frontService, m_ledger, m_groupId, m_chainId, m_blockLimit);
        m_txpool = txPoolFactory->createTxPool();

        m_sync = std::dynamic_pointer_cast<TransactionSync>(m_txpool->transactionSync());
        auto syncConfig = m_sync->config();
        m_sync = std::make_shared<FakeTransactionSync1>(syncConfig);
        m_txpool->setTransactionSync(m_sync);

        if (enableTree)
        {
            auto router = std::make_shared<TreeTopology>(m_nodeId);
            m_txpool->setTreeRouter(std::move(router));
        }
        m_txpool->init();
        m_txpool->start();

        m_fakeGateWay->addTxPool(m_nodeId, m_txpool);
        m_frontService->setGateWay(m_fakeGateWay);
        bcos::crypto::NodeIDs allNode;
        for (int i = 0; i < 10; ++i)
        {
            auto key = m_cryptoSuite->signatureImpl()->generateKeyPair();
            auto nodeId = std::make_shared<KeyImpl>(
                h256(std::to_string(i) +
                     "000000000000000000000000000000000000000000000000000000000000000")
                    .asBytes());
            if (enableTree)
            {
                m_nodeIdList.push_back(nodeId);
            }
            else
            {
                m_nodeIdList.push_back(key->publicKey());
            }
            allNode.push_back(nodeId);
        }
        allNode.push_back(m_nodeId);
        for (const auto& nodeId : m_nodeIdList)
        {
            TxPool::Ptr txpool;
            if (enableTree)
            {
                nodeId->hex();
                auto frontService = std::make_shared<FakeFrontService>(nodeId);
                frontService->setGateWay(m_fakeGateWay);
                auto protocol = std::make_shared<protocol::ProtocolInfo>();
                protocol->setVersion(V2);
                auto gInfo = std::make_shared<FakeGroupInfo>();
                gInfo->appendProtocol(protocol);
                frontService->setGroupInfo(gInfo);
                auto txPoolFactoryTemp =
                    std::make_shared<TxPoolFactory>(nodeId, _cryptoSuite, m_txResultFactory,
                        m_blockFactory, frontService, m_ledger, m_groupId, m_chainId, m_blockLimit);
                txpool = txPoolFactoryTemp->createTxPool();
            }
            else
            {
                txpool = txPoolFactory->createTxPool();
                auto sync = std::dynamic_pointer_cast<TransactionSync>(m_txpool->transactionSync());
                sync = std::make_shared<FakeTransactionSync1>(sync->config());
                txpool->setTransactionSync(sync);
            }


            if (enableTree)
            {
                auto router = std::make_shared<TreeTopology>(nodeId);
                router->updateConsensusNodeInfo(allNode);
                BCOS_LOG(TRACE) << LOG_DESC("updateRouter")
                                << LOG_KV("consIndex", router->consIndex());
                txpool->setTreeRouter(std::move(router));
            }
            m_fakeGateWay->addTxPool(nodeId, txpool);
            txpool->init();
            txpool->start();
        }
    }
    virtual ~TxPoolFixture()
    {
        std::cout << "#### TxPoolFixture de-constructor" << std::endl;
        if (m_txpool)
        {
            m_txpool->stop();
        }
        if (m_ledger)
        {
            m_ledger->stop();
        }
        if (m_frontService)
        {
            m_frontService->stop();
        }
        if (m_sync)
        {
            m_sync->stop();
        }
        if (m_fakeGateWay)
        {
            for (auto& txpool : m_fakeGateWay->m_nodeId2TxPool)
            {
                txpool.second->stop();
            }
        }
    }

    BlockFactory::Ptr blockFactory() { return m_blockFactory; }
    TxPool::Ptr txpool() { return m_txpool; }
    FakeLedger::Ptr ledger() { return m_ledger; }
    NodeIDPtr nodeID() { return m_nodeId; }
    std::string const& chainId() { return m_chainId; }
    std::string const& groupId() { return m_groupId; }
    FakeFrontService::Ptr frontService() { return m_frontService; }
    TransactionSync::Ptr sync() { return m_sync; }
    void appendSealer(NodeIDPtr _nodeId)
    {
        auto consensusNode = std::make_shared<ConsensusNode>(_nodeId);
        m_ledger->ledgerConfig()->mutableConsensusNodeList().emplace_back(consensusNode);
        m_txpool->notifyConsensusNodeList(m_ledger->ledgerConfig()->consensusNodeList(), nullptr);
        for (const auto& item : m_fakeGateWay->m_nodeId2TxPool)
        {
            item.second->notifyConsensusNodeList(
                m_ledger->ledgerConfig()->consensusNodeList(), nullptr);
        }
        updateConnectedNodeList();
    }
    void appendObserver(NodeIDPtr _nodeId)
    {
        auto consensusNode = std::make_shared<ConsensusNode>(_nodeId);
        m_ledger->ledgerConfig()->mutableObserverList()->emplace_back(consensusNode);
        m_txpool->notifyObserverNodeList(m_ledger->ledgerConfig()->observerNodeList(), nullptr);
        for (const auto& item : m_fakeGateWay->m_nodeId2TxPool)
        {
            item.second->notifyObserverNodeList(
                m_ledger->ledgerConfig()->observerNodeList(), nullptr);
        }
        updateConnectedNodeList();
    }
    void init()
    {
        // init the txpool
        m_txpool->init();
    }

    void resetToFakeTransactionSync()
    {
        auto syncConfig = m_sync->config();
        syncConfig->setForwardPercent(100);
        m_sync = std::make_shared<FakeTransactionSync>(syncConfig);
        m_txpool->setTransactionSync(m_sync);
    }

    void asyncNotifyBlockResult(BlockNumber _blockNumber, TransactionSubmitResultsPtr _txsResult,
        std::function<void(Error::Ptr)> _onNotifyFinished)
    {
        m_txpool->txpoolStorage()->batchRemove(_blockNumber, *_txsResult);
        if (_onNotifyFinished)
        {
            _onNotifyFinished(nullptr);
        }
    }

private:
    void updateConnectedNodeList()
    {
        NodeIDSet nodeIdSet;
        for (const auto& node : m_ledger->ledgerConfig()->consensusNodeList())
        {
            nodeIdSet.insert(node->nodeID());
        }
        for (const auto& node : m_ledger->ledgerConfig()->observerNodeList())
        {
            nodeIdSet.insert(node->nodeID());
        }
        m_txpool->transactionSync()->config()->setConnectedNodeList(nodeIdSet);
        m_txpool->transactionSync()->config()->notifyConnectedNodes(nodeIdSet, nullptr);
        m_frontService->setNodeIDList(nodeIdSet);
        for (const auto& item : m_fakeGateWay->m_nodeId2TxPool)
        {
            item.second->transactionSync()->config()->setConnectedNodeList(nodeIdSet);
            item.second->transactionSync()->config()->notifyConnectedNodes(nodeIdSet, nullptr);
            auto fakeFront = std::dynamic_pointer_cast<FakeFrontService>(
                item.second->transactionSync()->config()->frontService());
            fakeFront->setNodeIDList(nodeIdSet);
        }
    }

public:
    NodeIDPtr m_nodeId;
    CryptoSuite::Ptr m_cryptoSuite;
    BlockFactory::Ptr m_blockFactory;
    TransactionSubmitResultFactory::Ptr m_txResultFactory;
    std::string m_groupId;
    std::string m_chainId;
    int64_t m_blockLimit;

    FakeLedger::Ptr m_ledger;
    FakeFrontService::Ptr m_frontService;
    FakeGateWay::Ptr m_fakeGateWay;
    TxPool::Ptr m_txpool;
    TransactionSync::Ptr m_sync;

    NodeIDs m_nodeIdList;
};

inline void checkTxSubmit(TxPoolInterface::Ptr _txpool, TxPoolStorageInterface::Ptr _storage,
    Transaction::Ptr _tx, HashType const& _expectedTxHash, uint32_t _expectedStatus,
    size_t expectedTxSize, bool _needWaitResult = true, bool _waitNothing = false,
    bool _maybeExpired = false)
{
    struct Defer
    {
        ~Defer()
        {
            for (auto& it : m_futures)
            {
                it.get();
            }
        }

        void addFuture(std::future<void> future)
        {
            std::unique_lock lock(m_mutex);
            m_futures.emplace_back(std::move(future));
        }

        std::mutex m_mutex;
        std::list<std::future<void>> m_futures;
    };

    static Defer defer;

    auto promise = std::make_unique<std::promise<void>>();
    auto future = promise->get_future();

    bcos::task::wait([](decltype(_txpool) txpool, decltype(_tx) transaction,
                         decltype(promise) promise, bool _maybeExpired, HashType _expectedTxHash,
                         uint32_t _expectedStatus) -> bcos::task::Task<void> {
        try
        {
            auto submitResult = co_await txpool->submitTransaction(std::move(transaction));
            if (submitResult->txHash() != _expectedTxHash)
            {
                // do something
                std::cout << "Mismatch!" << std::endl;
            }
            BOOST_CHECK_EQUAL(submitResult->txHash(), _expectedTxHash);
            std::cout << "##### _expectedStatus: " << std::to_string(_expectedStatus) << std::endl;
            std::cout << "##### receiptStatus:" << std::to_string(submitResult->status())
                      << std::endl;
            if (_maybeExpired)
            {
                BOOST_CHECK(
                    (submitResult->status() == _expectedStatus) ||
                    (submitResult->status() == (int32_t)TransactionStatus::TransactionPoolTimeout));
            }
        }
        catch (std::exception& e)
        {
            std::cout << "Submit transaction exception! " << boost::diagnostic_information(e);
        }

        promise->set_value();
    }(_txpool, _tx, std::move(promise), _maybeExpired, _expectedTxHash, _expectedStatus));

    if (_waitNothing)
    {
        defer.addFuture(std::move(future));
        return;
    }

    future.get();

    if (!_needWaitResult)
    {
        while (_storage->size() != expectedTxSize)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    BOOST_CHECK(_storage->size() == expectedTxSize);
}
}  // namespace test
}  // namespace bcos