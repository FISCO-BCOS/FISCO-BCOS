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
#include "bcos-protocol/protobuf/PBTransactionMetaData.h"
#include "bcos-txpool/TxPoolConfig.h"
#include "bcos-txpool/TxPoolFactory.h"
#include "bcos-txpool/sync/TransactionSync.h"
#include "bcos-txpool/txpool/storage/MemoryStorage.h"
#include "bcos-txpool/txpool/validator/TxValidator.h"
#include <bcos-framework/interfaces/consensus/ConsensusNode.h>
#include <bcos-framework/testutils/faker/FakeFrontService.h>
#include <bcos-framework/testutils/faker/FakeLedger.h>
#include <bcos-framework/testutils/faker/FakeSealer.h>
#include <bcos-protocol/TransactionSubmitResultFactoryImpl.h>
#include <bcos-protocol/protobuf/PBBlockFactory.h>
#include <bcos-protocol/protobuf/PBBlockHeaderFactory.h>
#include <bcos-protocol/protobuf/PBTransactionFactory.h>
#include <bcos-protocol/protobuf/PBTransactionReceiptFactory.h>
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

namespace bcos
{
namespace test
{
class FakeTransactionSync1 : public TransactionSync
{
public:
    explicit FakeTransactionSync1(TransactionSyncConfig::Ptr _config) : TransactionSync(_config) {}
    ~FakeTransactionSync1() override {}
    void start() override {}
};

class FakeTransactionSync : public FakeTransactionSync1
{
public:
    explicit FakeTransactionSync(TransactionSyncConfig::Ptr _config) : FakeTransactionSync1(_config)
    {}
    ~FakeTransactionSync() override {}

    // only broadcast txsStatus
    void maintainTransactions() override
    {
        auto txs = config()->txpoolStorage()->fetchNewTxs(10000);
        if (txs->size() == 0)
        {
            return;
        }
        auto connectedNodeList = m_config->connectedNodeList();
        auto consensusNodeList = m_config->consensusNodeList();

        forwardTxsFromP2P(connectedNodeList, consensusNodeList, txs);
    }
};

class FakeMemoryStorage : public MemoryStorage
{
public:
    FakeMemoryStorage(TxPoolConfig::Ptr _config, size_t _notifyWorkerNum = 2)
      : MemoryStorage(_config, _notifyWorkerNum)
    {}

    bool shouldNotifyTx(bcos::protocol::Transaction::ConstPtr _tx,
        bcos::protocol::TransactionSubmitResult::Ptr _txSubmitResult) override
    {
        if (!_txSubmitResult)
        {
            return false;
        }
        if (!_tx->submitCallback())
        {
            return false;
        }
        return true;
    }
};

class TxPoolFixture
{
public:
    using Ptr = std::shared_ptr<TxPoolFixture>;
    TxPoolFixture(NodeIDPtr _nodeId, CryptoSuite::Ptr _cryptoSuite, std::string const& _groupId,
        std::string const& _chainId, int64_t _blockLimit, FakeGateWay::Ptr _fakeGateWay)
      : m_nodeId(_nodeId),
        m_cryptoSuite(_cryptoSuite),
        m_groupId(_groupId),
        m_chainId(_chainId),
        m_blockLimit(_blockLimit),
        m_fakeGateWay(_fakeGateWay)
    {
        auto blockHeaderFactory = std::make_shared<PBBlockHeaderFactory>(_cryptoSuite);
        auto txFactory = std::make_shared<PBTransactionFactory>(_cryptoSuite);
        auto receiptFactory = std::make_shared<PBTransactionReceiptFactory>(_cryptoSuite);
        m_blockFactory =
            std::make_shared<PBBlockFactory>(blockHeaderFactory, txFactory, receiptFactory);
        m_txResultFactory = std::make_shared<TransactionSubmitResultFactoryImpl>();
        m_ledger = std::make_shared<FakeLedger>(m_blockFactory, 20, 10, 10);

        m_frontService = std::make_shared<FakeFrontService>(_nodeId);
        auto txPoolFactory =
            std::make_shared<TxPoolFactory>(_nodeId, _cryptoSuite, m_txResultFactory,
                m_blockFactory, m_frontService, m_ledger, m_groupId, m_chainId, m_blockLimit);
        m_txpool = txPoolFactory->createTxPool();
        auto fakeMemoryStorage = std::make_shared<FakeMemoryStorage>(m_txpool->txpoolConfig());
        m_txpool->setTxPoolStorage(fakeMemoryStorage);

        m_sync = std::dynamic_pointer_cast<TransactionSync>(m_txpool->transactionSync());
        auto syncConfig = m_sync->config();
        m_sync = std::make_shared<FakeTransactionSync1>(syncConfig);
        m_txpool->setTransactionSync(m_sync);
        m_txpool->start();

        m_fakeGateWay->addTxPool(_nodeId, m_txpool);
        m_frontService->setGateWay(m_fakeGateWay);
    }
    virtual ~TxPoolFixture() {}

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

private:
    void updateConnectedNodeList()
    {
        NodeIDSet nodeIdSet;
        for (auto node : m_ledger->ledgerConfig()->consensusNodeList())
        {
            nodeIdSet.insert(node->nodeID());
        }
        m_txpool->transactionSync()->config()->setConnectedNodeList(nodeIdSet);
        m_txpool->transactionSync()->config()->notifyConnectedNodes(nodeIdSet, nullptr);
    }

private:
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
};

inline void checkTxSubmit(TxPoolInterface::Ptr _txpool, TxPoolStorageInterface::Ptr _storage,
    Transaction::Ptr _tx, HashType const& _expectedTxHash, uint32_t _expectedStatus,
    size_t expectedTxSize, bool _needWaitResult = true, bool _waitNothing = false,
    bool _maybeExpired = false)
{
    std::shared_ptr<bool> verifyFinish = std::make_shared<bool>(false);
    auto encodedData = _tx->encode();
    auto txData = std::make_shared<bytes>(encodedData.begin(), encodedData.end());
    _txpool->asyncSubmit(txData, [verifyFinish, _expectedTxHash, _expectedStatus, _maybeExpired](
                                     Error::Ptr, TransactionSubmitResult::Ptr _result) {
        std::cout << "#### expectedTxHash:" << _expectedTxHash.abridged() << std::endl;
        std::cout << "##### receipt txHash:" << _result->txHash().abridged() << std::endl;
        BOOST_CHECK(_result->txHash() == _expectedTxHash);
        std::cout << "##### _expectedStatus: " << std::to_string(_expectedStatus) << std::endl;
        std::cout << "##### receiptStatus:" << std::to_string(_result->status()) << std::endl;
        if (_maybeExpired)
        {
            BOOST_CHECK((_result->status() == _expectedStatus) ||
                        (_result->status() == (int32_t)TransactionStatus::BlockLimitCheckFail));
        }
        *verifyFinish = true;
    });
    if (_waitNothing)
    {
        return;
    }
    while (!*verifyFinish && _needWaitResult)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
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