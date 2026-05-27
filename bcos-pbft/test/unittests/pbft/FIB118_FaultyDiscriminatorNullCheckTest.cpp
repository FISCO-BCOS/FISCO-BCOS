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
 * @brief Regression test for FIB-118: tryTriggerFastViewChange must not invoke
 *        m_faultyDiscriminator when it has not been registered (std::bad_function_call).
 * @file FIB118_FaultyDiscriminatorNullCheckTest.cpp
 * @date 2026-05-07
 */
#include "test/unittests/pbft/PBFTFixture.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::crypto;
using namespace bcos::test;

namespace bcos::test
{

BOOST_AUTO_TEST_SUITE(FIB118_FaultyDiscriminatorNullCheckTest)

// Test: calling tryTriggerFastViewChange without registering faultyDiscriminator
// must not throw std::bad_function_call; it should return false gracefully.
BOOST_AUTO_TEST_CASE(UnregisteredDiscriminatorNoThrow)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signImpl, nullptr);

    auto faker = createPBFTFixture(cryptoSuite);

    // Add own node first, then a peer so the consensus list is non-empty and this node
    // is index 0 (a consensus node).
    faker->appendConsensusNode(faker->nodeID());
    auto peerKP = signImpl->generateKeyPair();
    faker->appendConsensusNode(peerKP->publicKey());
    faker->init();

    auto pbftConfig = faker->pbftConfig();

    // Explicitly clear the faultyDiscriminator to simulate "not registered" scenario.
    pbftConfig->registerFaultyDiscriminator(std::function<bool(bcos::crypto::NodeIDPtr)>{});

    // Register a fast-view-change handler so tryTriggerFastViewChange doesn't bail early.
    pbftConfig->registerFastViewChangeHandler([]() {});

    // Must not throw; before the fix this would throw std::bad_function_call.
    // Use the non-self index so the check proceeds past the self-leader guard.
    IndexType selfIdx = pbftConfig->nodeIndex();
    IndexType peerIdx = (selfIdx == 0) ? 1 : 0;
    BOOST_CHECK_NO_THROW(pbftConfig->tryTriggerFastViewChange(peerIdx));
}

// Test: registered (returns false) discriminator works normally and is actually called.
BOOST_AUTO_TEST_CASE(RegisteredDiscriminatorReturnsFalse)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signImpl, nullptr);

    auto faker = createPBFTFixture(cryptoSuite);
    faker->appendConsensusNode(faker->nodeID());
    auto peerKP = signImpl->generateKeyPair();
    faker->appendConsensusNode(peerKP->publicKey());
    faker->init();

    auto pbftConfig = faker->pbftConfig();

    bool discriminatorCalled = false;
    pbftConfig->registerFaultyDiscriminator([&](bcos::crypto::NodeIDPtr) -> bool {
        discriminatorCalled = true;
        return false;
    });
    pbftConfig->registerFastViewChangeHandler([]() {});

    // Find an index that is NOT self so the self-leader guard doesn't bail.
    IndexType selfIdx = pbftConfig->nodeIndex();
    IndexType peerIdx = (selfIdx == 0) ? 1 : 0;

    // tryTriggerFastViewChange returns false when discriminator returns false.
    bool result = pbftConfig->tryTriggerFastViewChange(peerIdx);
    BOOST_CHECK(!result);
    // Discriminator must have been called.
    BOOST_CHECK(discriminatorCalled);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
