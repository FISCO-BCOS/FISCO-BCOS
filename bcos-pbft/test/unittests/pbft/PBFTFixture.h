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
 * @brief fixture for the PBFT
 * @file PBFTFixture.h
 * @author: yujiechen
 * @date 2021-05-28
 */
#pragma once
#include "bcos-framework/interfaces/storage/KVStorageHelper.h"
#include "bcos-pbft/core/StateMachine.h"
#include "bcos-pbft/pbft/PBFTFactory.h"
#include "bcos-pbft/pbft/PBFTImpl.h"
#include "bcos-pbft/pbft/storage/LedgerStorage.h"
#include <bcos-framework/interfaces/consensus/ConsensusNode.h>
#include <bcos-framework/libprotocol/TransactionSubmitResultFactoryImpl.h>
#include <bcos-framework/libprotocol/protobuf/PBBlockFactory.h>
#include <bcos-framework/libprotocol/protobuf/PBBlockHeaderFactory.h>
#include <bcos-framework/libprotocol/protobuf/PBTransactionFactory.h>
#include <bcos-framework/libprotocol/protobuf/PBTransactionReceiptFactory.h>
#include <bcos-framework/libstorage/StateStorage.h>
#include <bcos-framework/testutils/faker/FakeFrontService.h>
#include <bcos-framework/testutils/faker/FakeLedger.h>
#include <bcos-framework/testutils/faker/FakeScheduler.h>
#include <bcos-framework/testutils/faker/FakeSealer.h>
#include <bcos-framework/testutils/faker/FakeTxPool.h>
#include <boost/bind/bind.hpp>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <thread>

using namespace bcos::crypto;
using namespace bcos::front;
using namespace bcos::storage;
using namespace bcos::ledger;
using namespace bcos::txpool;
using namespace bcos::sealer;
using namespace bcos::protocol;
using namespace bcos::scheduler;
using namespace bcos::consensus;

namespace bcos
{
namespace test
{
class FakePBFTConfig : public PBFTConfig
{
public:
    using Ptr = std::shared_ptr<FakePBFTConfig>;
    FakePBFTConfig(bcos::crypto::CryptoSuite::Ptr _cryptoSuite,
        bcos::crypto::KeyPairInterface::Ptr _keyPair,
        std::shared_ptr<PBFTMessageFactory> _pbftMessageFactory,
        std::shared_ptr<PBFTCodecInterface> _codec, std::shared_ptr<ValidatorInterface> _validator,
        std::shared_ptr<bcos::front::FrontServiceInterface> _frontService,
        StateMachineInterface::Ptr _stateMachine, PBFTStorage::Ptr _storage)
      : PBFTConfig(_cryptoSuite, _keyPair, _pbftMessageFactory, _codec, _validator, _frontService,
            _stateMachine, _storage)
    {}

    ~FakePBFTConfig() override {}

    virtual void setMinRequiredQuorum(uint64_t _quorum) { m_minRequiredQuorum = _quorum; }
};
class FakePBFTCache : public PBFTCache
{
public:
    using Ptr = std::shared_ptr<FakePBFTCache>;
    FakePBFTCache(PBFTConfig::Ptr _config, BlockNumber _index) : PBFTCache(_config, _index) {}
    ~FakePBFTCache() override {}

    PBFTMessageInterface::Ptr prePrepare() { return m_prePrepare; }
    void intoPrecommit() override { PBFTCache::intoPrecommit(); }
};

class FakePBFTCacheFactory : public PBFTCacheFactory
{
public:
    using Ptr = std::shared_ptr<FakePBFTCacheFactory>;
    FakePBFTCacheFactory() = default;
    ~FakePBFTCacheFactory() override {}

    PBFTCache::Ptr createPBFTCache(PBFTConfig::Ptr _config, BlockNumber _index,
        std::function<void(bcos::protocol::BlockNumber)>) override
    {
        return std::make_shared<FakePBFTCache>(_config, _index);
    }
};

class FakeCacheProcessor : public PBFTCacheProcessor
{
public:
    using Ptr = std::shared_ptr<FakeCacheProcessor>;
    explicit FakeCacheProcessor(PBFTCacheFactory::Ptr _cacheFactory, PBFTConfig::Ptr _config)
      : PBFTCacheProcessor(_cacheFactory, _config)
    {}

    ~FakeCacheProcessor() override {}

    PBFTCachesType& caches() { return m_caches; }
    size_t stableCheckPointQueueSize() const { return m_stableCheckPointQueue.size(); }
    size_t committedQueueSize() const { return m_committedQueue.size(); }
    bool checkPrecommitWeight(PBFTMessageInterface::Ptr _precommitMsg) override
    {
        PBFTCacheProcessor::checkPrecommitWeight(_precommitMsg);
        return true;
    }
};


class FakePBFTEngine : public PBFTEngine
{
public:
    using Ptr = std::shared_ptr<FakePBFTEngine>;
    explicit FakePBFTEngine(PBFTConfig::Ptr _config) : PBFTEngine(_config)
    {
        auto cacheFactory = std::make_shared<FakePBFTCacheFactory>();
        m_cacheProcessor = std::make_shared<FakeCacheProcessor>(cacheFactory, _config);
        m_logSync = std::make_shared<PBFTLogSync>(_config, m_cacheProcessor);
        m_cacheProcessor->registerProposalAppliedHandler(
            boost::bind(&FakePBFTEngine::onProposalApplied, this, boost::placeholders::_1,
                boost::placeholders::_2, boost::placeholders::_3));
        m_cacheProcessor->registerOnLoadAndVerifyProposalSucc(boost::bind(
            &FakePBFTEngine::onLoadAndVerifyProposalSucc, this, boost::placeholders::_1));
        initSendResponseHandler();
    }
    ~FakePBFTEngine() override {}

    void onReceivePBFTMessage(bcos::Error::Ptr _error, bcos::crypto::NodeIDPtr _nodeID,
        bytesConstRef _data, std::function<void(bytesConstRef _respData)> _sendResponse) override
    {
        PBFTEngine::onReceivePBFTMessage(_error, _nodeID, _data, _sendResponse);
    }

    // PBFT main processing function
    void executeWorker() override
    {
        while (!msgQueue()->empty())
        {
            PBFTEngine::executeWorker();
        }
    }

    void executeWorkerByRoundbin() { return PBFTEngine::executeWorker(); }

    void onRecvProposal(bool _containSysTxs, bytesConstRef _proposalData,
        bcos::protocol::BlockNumber _proposalIndex,
        bcos::crypto::HashType const& _proposalHash) override
    {
        PBFTEngine::onRecvProposal(_containSysTxs, _proposalData, _proposalIndex, _proposalHash);
    }

    bool handlePrePrepareMsg(std::shared_ptr<PBFTMessageInterface> _prePrepareMsg,
        bool _needVerifyProposal = true, bool _generatedFromNewView = false,
        bool _needCheckSignature = true) override
    {
        return PBFTEngine::handlePrePrepareMsg(
            _prePrepareMsg, _needVerifyProposal, _generatedFromNewView, _needCheckSignature);
    }

    PBFTMsgQueuePtr msgQueue() { return m_msgQueue; }
};

class FakePBFTImpl : public PBFTImpl
{
public:
    explicit FakePBFTImpl(PBFTEngine::Ptr _pbftEngine) : PBFTImpl(_pbftEngine) { m_running = true; }
    ~FakePBFTImpl() {}
};

class FakePBFTFactory : public PBFTFactory
{
public:
    using Ptr = std::shared_ptr<PBFTFactory>;
    FakePBFTFactory(bcos::crypto::CryptoSuite::Ptr _cryptoSuite,
        bcos::crypto::KeyPairInterface::Ptr _keyPair,
        std::shared_ptr<bcos::front::FrontServiceInterface> _frontService,
        std::shared_ptr<bcos::storage::KVStorageHelper> _storage,
        std::shared_ptr<bcos::ledger::LedgerInterface> _ledger,
        bcos::scheduler::SchedulerInterface::Ptr _scheduler,
        bcos::txpool::TxPoolInterface::Ptr _txpool, bcos::protocol::BlockFactory::Ptr _blockFactory,
        bcos::protocol::TransactionSubmitResultFactory::Ptr _txResultFactory)
      : PBFTFactory(_cryptoSuite, _keyPair, _frontService, _storage, _ledger, _scheduler, _txpool,
            _blockFactory, _txResultFactory)
    {}

    PBFTImpl::Ptr createPBFT() override
    {
        auto pbft = PBFTFactory::createPBFT();
        auto orgPBFTConfig = pbft->pbftEngine()->pbftConfig();
        auto stateMachine = std::make_shared<StateMachine>(m_scheduler, m_blockFactory);

        PBFT_LOG(DEBUG) << LOG_DESC("create pbftStorage");
        auto pbftStorage = std::make_shared<LedgerStorage>(
            m_scheduler, m_storage, m_blockFactory, orgPBFTConfig->pbftMessageFactory());

        auto pbftConfig = std::make_shared<FakePBFTConfig>(m_cryptoSuite, m_keyPair,
            orgPBFTConfig->pbftMessageFactory(), orgPBFTConfig->codec(), orgPBFTConfig->validator(),
            orgPBFTConfig->frontService(), stateMachine, pbftStorage);
        PBFT_LOG(DEBUG) << LOG_DESC("create PBFTEngine");
        auto pbftEngine = std::make_shared<FakePBFTEngine>(pbftConfig);

        PBFT_LOG(INFO) << LOG_DESC("create PBFT");
        auto fakedPBFT = std::make_shared<FakePBFTImpl>(pbftEngine);
        auto ledgerFetcher = std::make_shared<bcos::tool::LedgerConfigFetcher>(m_ledger);
        fakedPBFT->setLedgerFetcher(ledgerFetcher);
        pbftConfig->setTimeoutState(false);
        pbftConfig->timer()->stop();
        return fakedPBFT;
    }
};

class PBFTFixture
{
public:
    using Ptr = std::shared_ptr<PBFTFixture>;
    PBFTFixture(CryptoSuite::Ptr _cryptoSuite, KeyPairInterface::Ptr _keyPair,
        FakeLedger::Ptr _ledger = nullptr, size_t _txCountLimit = 1000)
      : m_cryptoSuite(_cryptoSuite), m_keyPair(_keyPair), m_nodeId(_keyPair->publicKey())
    {
        // create block factory
        m_blockFactory = createBlockFactory(_cryptoSuite);

        // create fakeFrontService
        m_frontService = std::make_shared<FakeFrontService>(_keyPair->publicKey());

        // create KVStorageHelper
        m_storage = std::make_shared<KVStorageHelper>(std::make_shared<StateStorage>(nullptr));

        // create fakeLedger
        if (_ledger == nullptr)
        {
            m_ledger = std::make_shared<FakeLedger>(m_blockFactory, 20, 10, 10);
            m_ledger->setSystemConfig(SYSTEM_KEY_TX_COUNT_LIMIT, std::to_string(_txCountLimit));
            m_ledger->setSystemConfig(SYSTEM_KEY_CONSENSUS_LEADER_PERIOD, std::to_string(1));
            // m_ledger->ledgerConfig()->setConsensusTimeout(_consensusTimeout * 20);
            m_ledger->ledgerConfig()->setBlockTxCountLimit(_txCountLimit);
        }
        else
        {
            m_ledger = _ledger;
        }
        // create fakeTxPool
        m_txpool = std::make_shared<FakeTxPool>();
        // create FakeScheduler
        m_scheduler = std::make_shared<FakeScheduler>(m_ledger, m_blockFactory);

        auto txResultFactory = std::make_shared<TransactionSubmitResultFactoryImpl>();

        auto pbftFactory = std::make_shared<FakePBFTFactory>(_cryptoSuite, _keyPair, m_frontService,
            m_storage, m_ledger, m_scheduler, m_txpool, m_blockFactory, txResultFactory);
        m_pbft = pbftFactory->createPBFT();
        m_pbftEngine = std::dynamic_pointer_cast<FakePBFTEngine>(m_pbft->pbftEngine());
    }

    virtual ~PBFTFixture() {}

    void init() { m_pbft->init(); }

    void appendConsensusNode(ConsensusNode::Ptr _node)
    {
        m_ledger->ledgerConfig()->mutableConsensusNodeList().push_back(_node);
        pbftConfig()->setConsensusNodeList(m_ledger->ledgerConfig()->mutableConsensusNodeList());
        bcos::crypto::NodeIDSet connectedNodeList;
        for (auto const& node : m_ledger->ledgerConfig()->mutableConsensusNodeList())
        {
            connectedNodeList.insert(node->nodeID());
        }
        pbftConfig()->setConnectedNodeList(std::move(connectedNodeList));
    }

    void appendConsensusNode(PublicPtr _nodeId)
    {
        auto node = std::make_shared<ConsensusNode>(_nodeId, 1);
        appendConsensusNode(node);
    }

    void updateSwitchPerid() {}

    void clearConsensusNodeList() { m_ledger->ledgerConfig()->mutableConsensusNodeList().clear(); }

    FakeFrontService::Ptr frontService() { return m_frontService; }
    std::shared_ptr<KVStorageHelper> storage() { return m_storage; }
    FakeLedger::Ptr ledger() { return m_ledger; }
    FakeTxPool::Ptr txpool() { return m_txpool; }
    FakeScheduler::Ptr scheduler() { return m_scheduler; }
    PBFTImpl::Ptr pbft() { return m_pbft; }
    PBFTConfig::Ptr pbftConfig() { return m_pbft->pbftEngine()->pbftConfig(); }
    PublicPtr nodeID() { return m_nodeId; }

    FakePBFTEngine::Ptr pbftEngine() { return m_pbftEngine; }
    KeyPairInterface::Ptr keyPair() { return m_keyPair; }

    void setFrontService(FakeFrontService::Ptr _fakeFrontService)
    {
        m_frontService = _fakeFrontService;
    }

    BlockFactory::Ptr blockFactory() { return m_blockFactory; }

private:
    CryptoSuite::Ptr m_cryptoSuite;
    KeyPairInterface::Ptr m_keyPair;
    PublicPtr m_nodeId;
    BlockFactory::Ptr m_blockFactory;

    FakeFrontService::Ptr m_frontService;
    std::shared_ptr<KVStorageHelper> m_storage;
    FakeLedger::Ptr m_ledger;
    FakeTxPool::Ptr m_txpool;
    FakeScheduler::Ptr m_scheduler;
    FakePBFTEngine::Ptr m_pbftEngine;
    PBFTImpl::Ptr m_pbft;
};
using PBFTFixtureList = std::vector<PBFTFixture::Ptr>;

inline PBFTFixture::Ptr createPBFTFixture(
    CryptoSuite::Ptr _cryptoSuite, FakeLedger::Ptr _ledger = nullptr, size_t _txCountLimit = 1000)
{
    auto keyPair = _cryptoSuite->signatureImpl()->generateKeyPair();
    return std::make_shared<PBFTFixture>(_cryptoSuite, keyPair, _ledger, _txCountLimit);
}

inline std::map<IndexType, PBFTFixture::Ptr> createFakers(CryptoSuite::Ptr _cryptoSuite,
    size_t _consensusNodeSize, size_t _currentBlockNumber, size_t _connectedNodes,
    size_t _txCountLimit = 1000)
{
    PBFTFixtureList fakerList;
    // create block factory
    auto blockFactory = createBlockFactory(_cryptoSuite);

    auto ledger = std::make_shared<FakeLedger>(blockFactory, _currentBlockNumber + 1, 10, 0);
    for (size_t i = 0; i < _consensusNodeSize; i++)
    {
        // ensure all the block are consistent
        auto fakedLedger = std::make_shared<FakeLedger>(
            blockFactory, _currentBlockNumber + 1, 10, 0, ledger->sealerList());
        fakedLedger->setSystemConfig(SYSTEM_KEY_TX_COUNT_LIMIT, std::to_string(_txCountLimit));
        fakedLedger->setSystemConfig(SYSTEM_KEY_CONSENSUS_LEADER_PERIOD, std::to_string(1));
        // fakedLedger->ledgerConfig()->setConsensusTimeout(_consensusTimeout * 1000);
        fakedLedger->ledgerConfig()->setBlockTxCountLimit(_txCountLimit);
        auto peerFaker = createPBFTFixture(_cryptoSuite, fakedLedger, _txCountLimit);
        fakerList.push_back(peerFaker);
    }

    for (size_t i = 0; i < _consensusNodeSize; i++)
    {
        auto faker = fakerList[i];
        for (size_t j = 0; j < _consensusNodeSize; j++)
        {
            faker->appendConsensusNode(fakerList[j]->keyPair()->publicKey());
        }
    }
    // init the fakers
    auto fakeGateWay = std::make_shared<FakeGateWay>();
    for (size_t i = 0; i < _consensusNodeSize; i++)
    {
        auto faker = fakerList[i];
        faker->init();
    }
    std::map<IndexType, PBFTFixture::Ptr> indexToFakerMap;
    for (size_t i = 0; i < _consensusNodeSize; i++)
    {
        auto faker = fakerList[i];
        indexToFakerMap[faker->pbftConfig()->nodeIndex()] = faker;
        faker->frontService()->setGateWay(fakeGateWay);
    }
    for (IndexType i = 0; i < _connectedNodes; i++)
    {
        auto faker = indexToFakerMap[i];
        fakeGateWay->addConsensusInterface(faker->keyPair()->publicKey(), faker->pbft());
    }
    return indexToFakerMap;
}

inline Block::Ptr fakeBlock(CryptoSuite::Ptr _cryptoSuite, PBFTFixture::Ptr _faker,
    size_t _proposalIndex, size_t _txsHashSize)
{
    auto ledgerConfig = _faker->ledger()->ledgerConfig();
    auto parent = (_faker->ledger()->ledgerData())[ledgerConfig->blockNumber()];
    auto block = _faker->ledger()->init(parent->blockHeader(), true, _proposalIndex, 0, 0);
    for (size_t i = 0; i < _txsHashSize; i++)
    {
        auto hash = _cryptoSuite->hashImpl()->hash(std::to_string(i));
        auto txMetaData = _faker->blockFactory()->createTransactionMetaData(hash, hash.abridged());
        block->appendTransactionMetaData(txMetaData);
    }
    return block;
}
}  // namespace test
}  // namespace bcos