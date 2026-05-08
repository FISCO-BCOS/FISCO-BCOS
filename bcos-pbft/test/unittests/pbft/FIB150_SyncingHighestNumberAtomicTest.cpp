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
 * @brief FIB-150: ConsensusConfig::m_syncingHighestNumber is read by the
 *        consensus layer (executeWorker etc.) and written by the sync layer
 *        with no synchronization. The plain BlockNumber field is neither atomic
 *        nor mutex-protected, which is a C++ data race (UB).
 *
 *        Test uses FakePBFTConfig (PBFTFixture.h) since ConsensusConfig itself
 *        is abstract (virtual updateQuorum() = 0).
 *
 * @file FIB150_SyncingHighestNumberAtomicTest.cpp
 */

#include "bcos-crypto/interfaces/crypto/KeyPairInterface.h"
#include "test/unittests/pbft/PBFTFixture.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <thread>

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::crypto;
using namespace bcos::protocol;

namespace bcos
{
namespace test
{
namespace
{
// FIB-suffixed helper for UNITY_BUILD safety.
inline PBFTFixture::Ptr makePbftFixtureFib150()
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    return createPBFTFixture(cryptoSuite, nullptr, /*_txCountLimit=*/1000);
}
}  // namespace

BOOST_FIXTURE_TEST_SUITE(FIB150SyncingHighestNumberAtomicTest, TestPromptFixture)

// FIB-150: smoke check — getter returns what setter wrote.
BOOST_AUTO_TEST_CASE(setterGetterRoundTrip)
{
    auto fixture = makePbftFixtureFib150();
    auto config = fixture->pbftConfig();
    BOOST_REQUIRE(config != nullptr);

    config->setSyncingHighestNumber(42);
    BOOST_CHECK_EQUAL(config->syncingHighestNumber(), 42);

    config->setSyncingHighestNumber(123456789);
    BOOST_CHECK_EQUAL(config->syncingHighestNumber(), 123456789);
}

// FIB-150: concurrent writer + reader. With m_syncingHighestNumber non-atomic,
// TSan flags the writer / reader pair as a data race. After the fix
// (std::atomic<BlockNumber>), this test is TSan-clean.
//
// We also validate readers see only values previously written (monotonic
// non-decreasing series 0..N) — torn 64-bit values on aarch64 would surface
// as values larger than the most recent write.
BOOST_AUTO_TEST_CASE(concurrentSetGetIsRaceFree)
{
    auto fixture = makePbftFixtureFib150();
    auto config = fixture->pbftConfig();
    BOOST_REQUIRE(config != nullptr);

    constexpr int64_t kIters = 200000;
    std::atomic<bool> stop{false};

    std::thread writer([&] {
        for (int64_t i = 0; i < kIters; ++i)
        {
            config->setSyncingHighestNumber(i);
        }
        stop.store(true);
    });

    int64_t lastSeen = -1;
    while (!stop.load())
    {
        auto val = config->syncingHighestNumber();
        // monotonic non-decreasing: must never observe a value smaller than
        // a value previously read (would imply a torn read on aarch64 or
        // reordered visibility on x86)
        BOOST_CHECK_GE(val, lastSeen);
        BOOST_CHECK_LE(val, kIters);
        lastSeen = val;
    }
    writer.join();

    // final state: writer wrote up to kIters - 1
    BOOST_CHECK_EQUAL(config->syncingHighestNumber(), kIters - 1);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
