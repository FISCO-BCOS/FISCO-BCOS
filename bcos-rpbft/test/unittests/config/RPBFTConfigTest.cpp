/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @file RPBFTConfigTest.cpp
 * @author: kyonGuo
 * @date 2023/8/30
 */
#include "bcos-framework/bcos-framework/testutils/faker/FakeBlock.h"

#include "bcos-crypto/interfaces/crypto/KeyPairInterface.h"
#include "bcos-rpbft/bcos-rpbft/rpbft/config/RPBFTConfig.h"
#include "bcos-rpbft/bcos-rpbft/rpbft/utilities/RPBFTFactory.h"
#include "test/unittests/pbft/PBFTFixture.h"
#include "test/unittests/protocol/FakePBFTMessage.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::crypto;
using namespace bcos::protocol;
namespace bcos::test
{
class RPBFTConfigFixture : public TestPromptFixture
{
public:
    RPBFTConfigFixture()
    {
        auto hashImpl = std::make_shared<Keccak256>();
        auto signatureImpl = std::make_shared<Secp256k1Crypto>();
        m_cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
        m_keyPair = signatureImpl->generateKeyPair();
        m_nodeId = m_keyPair->publicKey();
        m_blockFactory = createBlockFactory(m_cryptoSuite);

        // create fakeFrontService
        m_frontService = std::make_shared<FakeFrontService>(m_nodeId);

        // create KVStorageHelper
        m_storage =
            std::make_shared<KVStorageHelper>(std::make_shared<StateStorage>(nullptr, false));

        m_ledger = std::make_shared<FakeLedger>(m_blockFactory, 20, 10, 10);
        m_ledger->setSystemConfig(SYSTEM_KEY_TX_COUNT_LIMIT, std::to_string(1000));
        m_ledger->setSystemConfig(SYSTEM_KEY_CONSENSUS_LEADER_PERIOD, std::to_string(1));
        m_ledger->setSystemConfig(SYSTEM_KEY_AUTH_CHECK_STATUS, std::to_string(0));
        m_ledger->setSystemConfig(SYSTEM_KEY_COMPATIBILITY_VERSION, protocol::DEFAULT_VERSION_STR);
        m_ledger->ledgerConfig()->setBlockTxCountLimit(1000);

        // create fakeTxPool
        m_txpool = std::make_shared<FakeTxPool>();
        // create FakeScheduler
        m_scheduler = std::make_shared<FakeScheduler>(m_ledger, m_blockFactory);

        auto txResultFactory = std::make_shared<TransactionSubmitResultFactoryImpl>();

        auto rpbftFactory = std::make_shared<RPBFTFactory>(m_cryptoSuite, m_keyPair, m_frontService,
            m_storage, m_ledger, m_scheduler, m_txpool, m_blockFactory, txResultFactory);
        m_rpbft = rpbftFactory->createRPBFT();
        m_rpbftConfig = std::dynamic_pointer_cast<RPBFTConfig>(m_rpbft->pbftEngine()->pbftConfig());
    }

    LedgerConfig::Ptr buildLedgerConfig(
        size_t consensusNodeNum, size_t observerNodeNum, size_t candidateSealerNodeNum)
    {
        auto ledgerConfig = std::make_shared<LedgerConfig>();

        for (size_t i = 0; i < consensusNodeNum; ++i)
        {
            ledgerConfig->mutableConsensusList()->push_back(
                std::make_shared<consensus::ConsensusNode>(
                    m_cryptoSuite->signatureImpl()->generateKeyPair()->publicKey(), 1));
        }

        for (size_t i = 0; i < observerNodeNum; ++i)
        {
            ledgerConfig->mutableObserverList()->push_back(
                std::make_shared<consensus::ConsensusNode>(
                    m_cryptoSuite->signatureImpl()->generateKeyPair()->publicKey(), 0));
        }

        for (size_t i = 0; i < candidateSealerNodeNum; ++i)
        {
            ledgerConfig->mutableCandidateSealerNodeList()->push_back(
                std::make_shared<consensus::ConsensusNode>(
                    m_cryptoSuite->signatureImpl()->generateKeyPair()->publicKey(), 1));
        }
        return ledgerConfig;
    }

    CryptoSuite::Ptr m_cryptoSuite;
    KeyPairInterface::Ptr m_keyPair;
    PublicPtr m_nodeId;
    BlockFactory::Ptr m_blockFactory;

    FakeFrontService::Ptr m_frontService;
    std::shared_ptr<KVStorageHelper> m_storage;
    FakeLedger::Ptr m_ledger;
    FakeTxPool::Ptr m_txpool;
    FakeScheduler::Ptr m_scheduler;
    PBFTImpl::Ptr m_rpbft;
    RPBFTConfig::Ptr m_rpbftConfig;
};
BOOST_FIXTURE_TEST_SUITE(RPBFTConfigTest, RPBFTConfigFixture)
BOOST_AUTO_TEST_CASE(testRPBFTConfig)
{
    // nothing happened
    BlockNumber numer = 1;
    auto ledgerConfig = buildLedgerConfig(4, 0, 0);
    ledgerConfig->setBlockNumber(numer++);
    m_rpbftConfig->resetConfig(ledgerConfig);
    BOOST_CHECK(!m_rpbftConfig->shouldRotateSealers(numer));

    // use rpbft feature, should rotate
    ledgerConfig->setBlockNumber(numer++);
    Features features;
    features.set(Features::Flag::feature_rpbft);
    ledgerConfig->setFeatures(features);
    m_rpbftConfig->resetConfig(ledgerConfig);
    BOOST_CHECK(m_rpbftConfig->shouldRotateSealers(numer));

    // update sealer num, should rotate
    ledgerConfig->setBlockNumber(numer++);
    ledgerConfig->setEpochSealerNum({2, 0});
    m_rpbftConfig->resetConfig(ledgerConfig);
    BOOST_CHECK(m_rpbftConfig->shouldRotateSealers(numer));

    // rotate tx
    for (auto i : {0, 1})
    {
        ledgerConfig->mutableCandidateSealerNodeList()->push_back(
            ledgerConfig->consensusNodeList()[i]);
    }
    ledgerConfig->mutableConsensusList()->erase(ledgerConfig->mutableConsensusList()->begin(),
        ledgerConfig->mutableConsensusList()->begin() + 1);
    // the next block should not rotate
    ledgerConfig->setBlockNumber(numer++);
    m_rpbftConfig->resetConfig(ledgerConfig);
    BOOST_CHECK(!m_rpbftConfig->shouldRotateSealers(numer));

    // update block num, should rotate
    {
        ledgerConfig->setBlockNumber(numer++);
        ledgerConfig->setEpochBlockNum({3, 0});
        m_rpbftConfig->resetConfig(ledgerConfig);
        BOOST_CHECK(m_rpbftConfig->shouldRotateSealers(numer));

        ledgerConfig->setBlockNumber(numer++);
        m_rpbftConfig->resetConfig(ledgerConfig);
        BOOST_CHECK(!m_rpbftConfig->shouldRotateSealers(numer));

        ledgerConfig->setBlockNumber(numer++);
        m_rpbftConfig->resetConfig(ledgerConfig);
        BOOST_CHECK(!m_rpbftConfig->shouldRotateSealers(numer));

        ledgerConfig->setBlockNumber(numer++);
        m_rpbftConfig->resetConfig(ledgerConfig);
        BOOST_CHECK(m_rpbftConfig->shouldRotateSealers(numer));
    }

    // notify rotate flag enable
    ledgerConfig->setNotifyRotateFlagInfo(1);
    ledgerConfig->setBlockNumber(numer++);
    m_rpbftConfig->resetConfig(ledgerConfig);
    BOOST_CHECK(m_rpbftConfig->shouldRotateSealers(numer));

    // working sealer exist, rotate
    ledgerConfig->setNotifyRotateFlagInfo(0);
    ledgerConfig->setEpochSealerNum({3, 0});
    ledgerConfig->setBlockNumber(numer++);
    ledgerConfig->mutableCandidateSealerNodeList()->pop_back();
    m_rpbftConfig->resetConfig(ledgerConfig);
    BOOST_CHECK(m_rpbftConfig->shouldRotateSealers(numer));
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test