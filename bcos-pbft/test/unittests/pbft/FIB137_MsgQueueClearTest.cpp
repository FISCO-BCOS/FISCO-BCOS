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
 * @brief FIB-137: PBFTEngine::m_msgQueue must be cleared on role demotion AND
 *        before resuming on role promotion. Stale messages enqueued under a
 *        previous role/term must not be replayed against fresh state after a
 *        demote->promote cycle.
 * @file FIB137_MsgQueueClearTest.cpp
 * @date 2026-05-08
 */
#include "bcos-pbft/pbft/protocol/PB/PBFTMessage.h"
#include "test/unittests/pbft/PBFTFixture.h"
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
namespace
{

// FIB-suffixed names per UNITY_BUILD ODR convention.
inline PBFTFixture::Ptr makePbftFixtureFib137()
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

// Create a stub PBFT message we can enqueue to drive the queue-size assertion.
// We don't need the message to be semantically valid - clearMsgQueue just drains.
inline PBFTBaseMessageInterface::Ptr makeStubMsgFib137()
{
    return std::make_shared<PBFTMessage>();
}

}  // namespace

BOOST_FIXTURE_TEST_SUITE(FIB137MsgQueueClearTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(demotion_clears_msg_queue)
{
    auto fixture = makePbftFixtureFib137();
    auto pbft = fixture->pbft();
    auto engine = fixture->pbftEngine();

    // After fixture construction, FakePBFTImpl sets m_masterNode=true. Confirm.
    BOOST_REQUIRE(pbft->masterNode());

    // Seed m_msgQueue with stale messages.
    constexpr int kSeedCount = 10;
    for (int i = 0; i < kSeedCount; ++i)
    {
        engine->msgQueue().push(makeStubMsgFib137());
    }
    BOOST_CHECK_EQUAL(engine->msgQueue().unsafe_size(), kSeedCount);

    // Demote: should drain m_msgQueue.
    pbft->enableAsMasterNode(false);
    BOOST_CHECK(!pbft->masterNode());
    BOOST_CHECK_EQUAL(engine->msgQueue().unsafe_size(), 0);
}

BOOST_AUTO_TEST_CASE(promotion_clears_stale_queue_before_resuming)
{
    auto fixture = makePbftFixtureFib137();
    auto pbft = fixture->pbft();
    auto engine = fixture->pbftEngine();

    // Demote first (clean state).
    pbft->enableAsMasterNode(false);
    BOOST_REQUIRE(!pbft->masterNode());

    // Inject stale messages while demoted (mimics queue-fill while node is backup).
    constexpr int kSeedCount = 7;
    for (int i = 0; i < kSeedCount; ++i)
    {
        engine->msgQueue().push(makeStubMsgFib137());
    }
    BOOST_CHECK_EQUAL(engine->msgQueue().unsafe_size(), kSeedCount);

    // Promote: should drain m_msgQueue before resuming.
    pbft->enableAsMasterNode(true);
    BOOST_CHECK(pbft->masterNode());
    BOOST_CHECK_EQUAL(engine->msgQueue().unsafe_size(), 0);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
