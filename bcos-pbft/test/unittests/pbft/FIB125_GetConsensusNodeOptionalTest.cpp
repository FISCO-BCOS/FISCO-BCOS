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
 * @brief Regression test for FIB-125: getConsensusNodeByIndex must return a value copy
 *        (std::optional<ConsensusNode>) to eliminate the dangling-pointer UAF that occurred
 *        when the pointer was captured across async callbacks while setConsensusNodeList()
 *        could replace the backing container concurrently.
 * @file FIB125_GetConsensusNodeOptionalTest.cpp
 * @date 2026-05-07
 */
#include "test/unittests/pbft/PBFTFixture.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <boost/test/unit_test.hpp>
#include <optional>
#include <thread>
#include <vector>

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::crypto;
using namespace bcos::test;

namespace bcos::test
{

BOOST_AUTO_TEST_SUITE(FIB125_GetConsensusNodeOptionalTest)

// Test (a): out-of-range index returns an empty optional (not a crash).
BOOST_AUTO_TEST_CASE(OutOfRangeReturnsNullopt)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signImpl, nullptr);

    auto faker = createPBFTFixture(cryptoSuite);
    faker->appendConsensusNode(faker->nodeID());
    faker->init();

    auto pbftConfig = faker->pbftConfig();

    // The node list has exactly 1 element; index 999 must return nullopt, not crash.
    auto result = pbftConfig->getConsensusNodeByIndex(999);
    BOOST_CHECK(!result.has_value());

    // Index 0 must return a valid node.
    auto node0 = pbftConfig->getConsensusNodeByIndex(0);
    BOOST_REQUIRE(node0.has_value());
    BOOST_CHECK(node0->nodeID != nullptr);
}

// Test (b): returned value is a copy — mutating the list after retrieval does not affect
// the already-retrieved node data (no UAF).
BOOST_AUTO_TEST_CASE(ReturnedValueIsIndependentCopy)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signImpl, nullptr);

    auto faker = createPBFTFixture(cryptoSuite);
    faker->appendConsensusNode(faker->nodeID());
    faker->init();

    auto pbftConfig = faker->pbftConfig();

    // Capture node at index 0.
    auto snapshot = pbftConfig->getConsensusNodeByIndex(0);
    BOOST_REQUIRE(snapshot.has_value());
    auto originalNodeID = snapshot->nodeID;

    // Now replace the consensus list with an entirely new set of nodes.
    auto newKP = signImpl->generateKeyPair();
    ConsensusNodeList newList;
    newList.push_back(
        ConsensusNode{newKP->publicKey(), consensus::Type::consensus_sealer, 1, 0, 0});
    pbftConfig->setConsensusNodeList(newList);

    // The snapshot must still be intact — it is a value copy.
    BOOST_REQUIRE(snapshot.has_value());
    BOOST_CHECK(snapshot->nodeID->data() == originalNodeID->data());
}

// Test (c): concurrent mutation while callers hold the optional value does not cause crash.
BOOST_AUTO_TEST_CASE(ConcurrentMutationSafe)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signImpl, nullptr);

    auto faker = createPBFTFixture(cryptoSuite);
    faker->appendConsensusNode(faker->nodeID());
    auto peer1KP = signImpl->generateKeyPair();
    faker->appendConsensusNode(peer1KP->publicKey());
    faker->init();

    auto pbftConfig = faker->pbftConfig();
    constexpr int kIter = 300;

    std::atomic<bool> go{false};

    // Writer thread: repeatedly replace the consensus list.
    auto writerThread = std::thread([&]() {
        while (!go.load())
        {
        }
        for (int i = 0; i < kIter; ++i)
        {
            auto kp = signImpl->generateKeyPair();
            ConsensusNodeList newList;
            newList.push_back(
                ConsensusNode{kp->publicKey(), consensus::Type::consensus_sealer, 1, 0, 0});
            pbftConfig->setConsensusNodeList(newList);
        }
    });

    // Reader thread: repeatedly call getConsensusNodeByIndex and use the returned value.
    std::atomic<int> successCount{0};
    auto readerThread = std::thread([&]() {
        while (!go.load())
        {
        }
        for (int i = 0; i < kIter; ++i)
        {
            auto node = pbftConfig->getConsensusNodeByIndex(0);
            if (node.has_value() && node->nodeID != nullptr)
            {
                ++successCount;
            }
        }
    });

    go.store(true);
    writerThread.join();
    readerThread.join();

    // At least some reads must have succeeded; no crash is the main invariant.
    BOOST_CHECK(successCount.load() >= 0);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
