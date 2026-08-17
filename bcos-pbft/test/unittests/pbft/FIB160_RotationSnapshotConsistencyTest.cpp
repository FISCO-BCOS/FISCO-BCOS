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
 * @brief FIB-160: ConsensusConfig::features() / setFeatures() were fully
 *        unsynchronized; concurrent reads/writes from VRFBasedSealer (which
 *        reads multiple flags via separate calls) could observe inconsistent
 *        feature snapshots, leading to wrong VRF mode selection under
 *        concurrent feature updates. This test verifies the new
 *        getRotationSnapshot() returns an internally consistent view
 *        (all-set or all-unset) under a concurrent flag flipper.
 * @file FIB160_RotationSnapshotConsistencyTest.cpp
 * @date 2026-05-08
 */
#include "test/unittests/pbft/PBFTFixture.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/ledger/Features.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <thread>

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::crypto;
using namespace bcos::protocol;

namespace bcos::test
{
namespace
{

inline PBFTFixture::Ptr makePbftFixtureFib160()
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

BOOST_FIXTURE_TEST_SUITE(FIB160RotationSnapshotConsistencyTest, TestPromptFixture)

// Single-threaded sanity: getRotationSnapshot returns the current features and
// the rotate decision under one shared lock. The rotate decision in the base
// ConsensusConfig is the virtual stub (returns false); we verify the features
// field round-trips.
BOOST_AUTO_TEST_CASE(snapshot_returns_current_features)
{
    auto fixture = makePbftFixtureFib160();
    auto config = fixture->pbftConfig();

    ledger::Features featuresWithCurve;
    featuresWithCurve.set(ledger::Features::Flag::feature_rpbft_vrf_type_secp256k1);
    config->setFeatures(featuresWithCurve);

    auto snap = config->getRotationSnapshot(/*blockNumber=*/-1);
    BOOST_CHECK(snap.features.get(ledger::Features::Flag::feature_rpbft_vrf_type_secp256k1));
    BOOST_CHECK(!snap.features.get(ledger::Features::Flag::bugfix_rpbft_vrf_blocknumber_input));

    ledger::Features featuresWithBugfix;
    featuresWithBugfix.set(ledger::Features::Flag::bugfix_rpbft_vrf_blocknumber_input);
    config->setFeatures(featuresWithBugfix);

    snap = config->getRotationSnapshot(/*blockNumber=*/-1);
    BOOST_CHECK(!snap.features.get(ledger::Features::Flag::feature_rpbft_vrf_type_secp256k1));
    BOOST_CHECK(snap.features.get(ledger::Features::Flag::bugfix_rpbft_vrf_blocknumber_input));
}

// Concurrency: a writer thread alternates between two distinct feature sets;
// the main thread calls getRotationSnapshot many times. Each snapshot must be
// internally consistent (one of f1 or f2, never a tear). With the FIB-160
// shared_mutex protection, this holds; without it, races on m_features
// (which is a std::bitset internal storage) could in principle produce torn
// reads (UB under TSan).
BOOST_AUTO_TEST_CASE(snapshot_is_internally_consistent_under_writer)
{
    auto fixture = makePbftFixtureFib160();
    auto config = fixture->pbftConfig();

    // f1 = {curve=on, blockNumberInput=off}
    ledger::Features f1;
    f1.set(ledger::Features::Flag::feature_rpbft_vrf_type_secp256k1);

    // f2 = {curve=off, blockNumberInput=on}
    ledger::Features f2;
    f2.set(ledger::Features::Flag::bugfix_rpbft_vrf_blocknumber_input);

    // Seed with f1 so the very first snapshot already satisfies the invariant
    // (otherwise the initial default-empty m_features would count as a
    // false-positive "violation" before the writer thread gets a chance to run).
    config->setFeatures(f1);

    // f1 and f2 differ only in those two flags; in either snapshot the two
    // flags should be opposite (curve XOR blockNumberInput == true).
    std::atomic<bool> stop{false};
    std::thread writer([&] {
        while (!stop.load(std::memory_order_relaxed))
        {
            config->setFeatures(f1);
            config->setFeatures(f2);
        }
    });

    // Sample many snapshots; each must satisfy the invariant.
    constexpr int kSamples = 5000;
    int violations = 0;
    for (int i = 0; i < kSamples; ++i)
    {
        auto snap = config->getRotationSnapshot(/*blockNumber=*/-1);
        bool curveOn = snap.features.get(ledger::Features::Flag::feature_rpbft_vrf_type_secp256k1);
        bool blockNumberOn =
            snap.features.get(ledger::Features::Flag::bugfix_rpbft_vrf_blocknumber_input);
        // Each snapshot is f1 or f2: exactly one of (curveOn, blockNumberOn) true.
        if ((curveOn && blockNumberOn) || (!curveOn && !blockNumberOn))
        {
            ++violations;
        }
    }
    stop.store(true, std::memory_order_relaxed);
    writer.join();

    BOOST_CHECK_MESSAGE(violations == 0, "FIB-160 snapshot consistency violations: " << violations);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
