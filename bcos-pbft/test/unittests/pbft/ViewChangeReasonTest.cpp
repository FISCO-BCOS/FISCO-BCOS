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
 * @brief every path into PBFTEngine::triggerTimeout carries its ViewChangeReason
 * @file ViewChangeReasonTest.cpp
 */
#include "test/unittests/pbft/PBFTFixture.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::crypto;

namespace bcos::test
{
BOOST_FIXTURE_TEST_SUITE(ViewChangeReasonTest, TestPromptFixture)

namespace
{
std::shared_ptr<PBFTFixture> makeFixture()
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signImpl, nullptr);
    auto fixture = createPBFTFixture(cryptoSuite);
    fixture->appendConsensusNode(fixture->nodeID());
    // loads the committed proposal from the fake ledger; triggerTimeout dereferences it
    fixture->init();
    return fixture;
}
}  // namespace

BOOST_AUTO_TEST_CASE(reasonNameTable)
{
    BOOST_CHECK_EQUAL(
        viewChangeReasonName(ViewChangeReason::ConsensusTimeout), "consensus_timeout");
    BOOST_CHECK_EQUAL(
        viewChangeReasonName(ViewChangeReason::FPlusOneHigherView), "f_plus_one_higher_view");
    BOOST_CHECK_EQUAL(viewChangeReasonName(ViewChangeReason::FaultyLeader), "faulty_leader");
    BOOST_CHECK_EQUAL(viewChangeReasonName(ViewChangeReason::StartupRecovery), "startup_recovery");
    BOOST_CHECK_EQUAL(viewChangeReasonName(ViewChangeReason::Restart), "restart");
}

BOOST_AUTO_TEST_CASE(noTriggerBeforeAnyPath)
{
    auto fixture = makeFixture();
    BOOST_CHECK(!fixture->pbftEngine()->lastViewChangeReason().has_value());
}

BOOST_AUTO_TEST_CASE(restartPathCarriesRestart)
{
    auto fixture = makeFixture();
    fixture->pbftEngine()->restart();
    BOOST_REQUIRE(fixture->pbftEngine()->lastViewChangeReason().has_value());
    BOOST_CHECK(*fixture->pbftEngine()->lastViewChangeReason() == ViewChangeReason::Restart);
}

BOOST_AUTO_TEST_CASE(startupPathCarriesStartupRecovery)
{
    auto fixture = makeFixture();
    // start() triggers the recovery view-change only when the node has not recovered yet
    BOOST_REQUIRE(!fixture->pbftConfig()->startRecovered());
    fixture->pbftEngine()->start();
    BOOST_REQUIRE(fixture->pbftEngine()->lastViewChangeReason().has_value());
    BOOST_CHECK(
        *fixture->pbftEngine()->lastViewChangeReason() == ViewChangeReason::StartupRecovery);
}

BOOST_AUTO_TEST_CASE(faultyLeaderPathFlowsThroughHandler)
{
    auto fixture = makeFixture();
    // the engine registered its handler in the constructor; the config forwards the reason
    std::optional<ViewChangeReason> seen;
    fixture->pbftConfig()->registerFastViewChangeHandler(
        [&seen](ViewChangeReason _reason) { seen = _reason; });
    fixture->pbftConfig()->registerFaultyDiscriminator(
        [](bcos::crypto::NodeIDPtr) { return true; });
    // make another node the leader so the loop runs at least once
    auto hashImpl = std::make_shared<Keccak256>();
    auto signImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signImpl, nullptr);
    auto peer = cryptoSuite->signatureImpl()->generateKeyPair()->publicKey();
    fixture->appendConsensusNode(peer);  // also refreshes the connected-node set
    IndexType otherIndex = fixture->pbftConfig()->nodeIndex() == 0 ? 1 : 0;
    bool triggered = fixture->pbftConfig()->tryTriggerFastViewChange(otherIndex);
    BOOST_REQUIRE(triggered);
    BOOST_REQUIRE(seen.has_value());
    BOOST_CHECK(*seen == ViewChangeReason::FaultyLeader);
}

BOOST_AUTO_TEST_CASE(timeoutAndFPlusOnePathsCarryTheirReason)
{
    auto fixture = makeFixture();
    auto engine = fixture->pbftEngine();
    engine->triggerTimeout(true, ViewChangeReason::ConsensusTimeout, 3000);
    BOOST_CHECK(*engine->lastViewChangeReason() == ViewChangeReason::ConsensusTimeout);
    engine->triggerTimeout(false, ViewChangeReason::FPlusOneHigherView);
    BOOST_CHECK(*engine->lastViewChangeReason() == ViewChangeReason::FPlusOneHigherView);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
