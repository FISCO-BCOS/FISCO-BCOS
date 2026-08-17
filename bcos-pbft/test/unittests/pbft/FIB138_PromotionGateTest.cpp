/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @brief FIB-138: ConsensusConfig::m_asMasterNode (read by isConsensusNode and
 *        gating consensus traffic) MUST NOT flip true until init/recoverState/
 *        restart all succeed. On any failure, both PBFTImpl::m_masterNode and
 *        ConsensusConfig::m_asMasterNode must roll back to false before the
 *        exception propagates.
 * @file FIB138_PromotionGateTest.cpp
 * @date 2026-05-08
 */
#include "test/unittests/pbft/PBFTFixture.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <stdexcept>

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::crypto;
using namespace bcos::protocol;

namespace bcos::test
{
namespace
{

// FIB-suffixed names per UNITY_BUILD ODR convention.

// Test-only PBFTImpl: lets us observe whether m_asMasterNode is set before init()
// runs (the FIB-138 race window) and lets us inject an init-time failure to
// verify rollback discipline.
class FibPromotionPBFTImpl : public FakePBFTImpl
{
public:
    using FakePBFTImpl::FakePBFTImpl;

    bool m_throwOnInit = false;
    // Captured at init() entry — should be FALSE under FIB-138's gating fix.
    bool m_pbftConfigMasterAtInitEntry = false;
    bool m_pbftImplMasterAtInitEntry = false;
    bool m_initWasCalled = false;

    void init() override
    {
        m_initWasCalled = true;
        m_pbftConfigMasterAtInitEntry = m_pbftEngine->pbftConfig()->asMasterNode();
        m_pbftImplMasterAtInitEntry = m_masterNode.load();
        if (m_throwOnInit)
        {
            throw std::runtime_error("FIB-138 simulated init failure");
        }
        // Skip recoverState/start to keep the test fast and deterministic.
    }
};

inline std::shared_ptr<FibPromotionPBFTImpl> makePromotionPBFTFib138(
    PBFTFixture::Ptr const& fixture)
{
    auto pbftEngine = fixture->pbftEngine();
    auto fakedPbft = std::make_shared<FibPromotionPBFTImpl>(pbftEngine);
    fakedPbft->setLedger(fixture->ledger());
    return fakedPbft;
}

inline PBFTFixture::Ptr makePbftFixtureFib138()
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    bcos::crypto::KeyPairInterface::Ptr keyPair = signatureImpl->generateKeyPair();
    auto fixture = std::make_shared<PBFTFixture>(cryptoSuite, keyPair, nullptr, 1000);
    fixture->appendConsensusNode(fixture->nodeID());
    fixture->init();
    return fixture;
}

}  // namespace

BOOST_FIXTURE_TEST_SUITE(FIB138PromotionGateTest, TestPromptFixture)

// FIB-138 invariant 1: at the moment init() runs during promotion, the
// ConsensusConfig master flag (which gates isConsensusNode and message
// acceptance) must still be FALSE. Only after init/recover/restart succeed
// should both flags flip to TRUE.
BOOST_AUTO_TEST_CASE(master_flag_flips_only_after_init_succeeds)
{
    auto fixture = makePbftFixtureFib138();
    auto promotionPbft = makePromotionPBFTFib138(fixture);
    auto pbftConfig = fixture->pbftEngine()->pbftConfig();

    // Demote first to a clean state.
    promotionPbft->enableAsMasterNode(false);
    BOOST_REQUIRE(!promotionPbft->masterNode());
    BOOST_REQUIRE(!pbftConfig->asMasterNode());

    // Promote: under FIB-138 the config flag must remain FALSE during init.
    promotionPbft->enableAsMasterNode(true);
    BOOST_REQUIRE(promotionPbft->m_initWasCalled);
    BOOST_CHECK_MESSAGE(!promotionPbft->m_pbftConfigMasterAtInitEntry,
        "FIB-138: pbftConfig->asMasterNode() must be FALSE at init() entry; was "
            << promotionPbft->m_pbftConfigMasterAtInitEntry);
    BOOST_CHECK_MESSAGE(!promotionPbft->m_pbftImplMasterAtInitEntry,
        "FIB-138: PBFTImpl::m_masterNode must be FALSE at init() entry; was "
            << promotionPbft->m_pbftImplMasterAtInitEntry);

    // After successful promotion, both flags must be TRUE.
    BOOST_CHECK(promotionPbft->masterNode());
    BOOST_CHECK(pbftConfig->asMasterNode());
}

// FIB-138 invariant 2: if init/recover/restart throws, both flags must roll
// back to FALSE before the exception propagates. Otherwise the node remains a
// half-initialized "master" accepting consensus traffic without recovery.
BOOST_AUTO_TEST_CASE(init_failure_rolls_back_master_flag)
{
    auto fixture = makePbftFixtureFib138();
    auto promotionPbft = makePromotionPBFTFib138(fixture);
    auto pbftConfig = fixture->pbftEngine()->pbftConfig();

    promotionPbft->enableAsMasterNode(false);
    BOOST_REQUIRE(!promotionPbft->masterNode());
    BOOST_REQUIRE(!pbftConfig->asMasterNode());

    promotionPbft->m_throwOnInit = true;
    BOOST_CHECK_THROW(promotionPbft->enableAsMasterNode(true), std::exception);

    // Both flags MUST be FALSE after the failed promotion.
    BOOST_CHECK_MESSAGE(!promotionPbft->masterNode(),
        "FIB-138: PBFTImpl::m_masterNode must roll back to FALSE on init failure");
    BOOST_CHECK_MESSAGE(!pbftConfig->asMasterNode(),
        "FIB-138: ConsensusConfig::m_asMasterNode must roll back to FALSE on init failure");
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
