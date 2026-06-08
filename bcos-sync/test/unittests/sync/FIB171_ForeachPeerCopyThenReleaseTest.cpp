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
 * @brief FIB-171: SyncPeerStatus::foreachPeer holds x_peersStatus shared lock
 *        across the entire iteration AND across the external callback _f.
 *        A slow callback stalls peer add/remove; a reentrant callback that
 *        needs x_peersStatus exclusively (deletePeer / updatePeerStatus) can
 *        deadlock or hit lock-order liveness problems.
 *
 *        The fix copies the peer list under the lock, releases, then iterates
 *        the snapshot calling _f without the lock held — same pattern already
 *        in use by foreachPeerRandom().
 *
 * @file FIB171_ForeachPeerCopyThenReleaseTest.cpp
 */

#include "SyncFixture.h"
#include "bcos-sync/protocol/PB/BlockSyncMsgFactoryImpl.h"
#include "bcos-sync/state/SyncPeerStatus.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <thread>

using namespace bcos;
using namespace bcos::sync;
using namespace bcos::crypto;

namespace bcos
{
namespace test
{
namespace
{
inline CryptoSuite::Ptr makeCryptoSuiteFib171()
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    return std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
}
}  // namespace

BOOST_FIXTURE_TEST_SUITE(FIB171ForeachPeerCopyThenReleaseTest, TestPromptFixture)

// FIB-171: smoke check — foreachPeer visits every inserted peer.
BOOST_AUTO_TEST_CASE(foreachPeerVisitsAllPeers)
{
    auto cryptoSuite = makeCryptoSuiteFib171();
    auto gateWay = std::make_shared<FakeGateWay>();
    auto faker = std::make_shared<SyncFixture>(cryptoSuite, gateWay);
    faker->init();
    auto syncStatus = faker->sync()->syncStatus();

    constexpr int kPeers = 5;
    std::vector<KeyPairInterface::Ptr> keys;
    for (int i = 0; i < kPeers; ++i)
    {
        keys.push_back(cryptoSuite->signatureImpl()->generateKeyPair());
        syncStatus->insertEmptyPeer(keys.back()->publicKey());
    }
    BOOST_CHECK_EQUAL(syncStatus->peersSize(), kPeers);

    std::atomic<int> visitCount{0};
    syncStatus->foreachPeer([&](PeerStatus::Ptr _peer) {
        if (_peer)
        {
            ++visitCount;
        }
        return true;
    });
    BOOST_CHECK_EQUAL(visitCount.load(), kPeers);
}

// FIB-171: callback can early-exit by returning false (preserved semantic).
BOOST_AUTO_TEST_CASE(foreachPeerEarlyExitOnFalse)
{
    auto cryptoSuite = makeCryptoSuiteFib171();
    auto gateWay = std::make_shared<FakeGateWay>();
    auto faker = std::make_shared<SyncFixture>(cryptoSuite, gateWay);
    faker->init();
    auto syncStatus = faker->sync()->syncStatus();

    constexpr int kPeers = 4;
    for (int i = 0; i < kPeers; ++i)
    {
        auto k = cryptoSuite->signatureImpl()->generateKeyPair();
        syncStatus->insertEmptyPeer(k->publicKey());
    }

    std::atomic<int> visitCount{0};
    syncStatus->foreachPeer([&](PeerStatus::Ptr) {
        ++visitCount;
        return visitCount.load() < 2;  // stop after seeing the 2nd peer
    });
    BOOST_CHECK_EQUAL(visitCount.load(), 2);
}

// FIB-171: REENTRANT-DELETE regression. Pre-fix, foreachPeer holds x_peersStatus
// (shared) for the entire iteration; a callback that calls deletePeer (which
// takes x_peersStatus unique) deadlocks because std::shared_mutex on libc++ is
// non-reentrant: a single thread holding shared cannot upgrade to unique nor
// take another shared.
//
// Post-fix the snapshot is taken under shared lock, the lock is released, then
// _f runs lock-free — deletePeer can complete without contention.
//
// We bound the test with a thread that calls foreachPeer + deletePeer; if the
// fix is missing the test would deadlock, so we use a join with a deadline.
BOOST_AUTO_TEST_CASE(foreachPeerReentrantDeleteDoesNotDeadlock)
{
    auto cryptoSuite = makeCryptoSuiteFib171();
    auto gateWay = std::make_shared<FakeGateWay>();
    auto faker = std::make_shared<SyncFixture>(cryptoSuite, gateWay);
    faker->init();
    auto syncStatus = faker->sync()->syncStatus();

    constexpr int kPeers = 8;
    std::vector<KeyPairInterface::Ptr> keys;
    for (int i = 0; i < kPeers; ++i)
    {
        keys.push_back(cryptoSuite->signatureImpl()->generateKeyPair());
        syncStatus->insertEmptyPeer(keys.back()->publicKey());
    }

    std::atomic<bool> done{false};
    std::thread runner([&] {
        // The callback deletes the peer it is currently visiting. With the
        // fix, the iteration runs over a copied snapshot so deletePeer does
        // not contend with foreachPeer's lock. Without the fix, deletePeer
        // would block forever on x_peersStatus.
        syncStatus->foreachPeer([&](PeerStatus::Ptr _peer) {
            if (_peer)
            {
                syncStatus->deletePeer(_peer->nodeId());
            }
            return true;
        });
        done.store(true);
    });

    // Bounded wait — if foreachPeer deadlocks against deletePeer, the runner
    // thread never completes within this window.
    auto start = std::chrono::steady_clock::now();
    while (!done.load() && std::chrono::steady_clock::now() - start < std::chrono::seconds(5))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    BOOST_REQUIRE_MESSAGE(done.load(),
        "FIB-171: foreachPeer deadlocked with reentrant deletePeer "
        "(lock not released before invoking external callback)");
    runner.join();

    // All peers should have been visited and removed.
    BOOST_CHECK_EQUAL(syncStatus->peersSize(), 0u);
}

// FIB-171: concurrent insert / delete during foreachPeer iteration must not
// race with the callback. With the fix, foreachPeer iterates a copied
// snapshot, so writers can mutate the underlying map without conflict.
BOOST_AUTO_TEST_CASE(foreachPeerConcurrentMutationIsRaceFree)
{
    auto cryptoSuite = makeCryptoSuiteFib171();
    auto gateWay = std::make_shared<FakeGateWay>();
    auto faker = std::make_shared<SyncFixture>(cryptoSuite, gateWay);
    faker->init();
    auto syncStatus = faker->sync()->syncStatus();

    constexpr int kInitialPeers = 16;
    std::vector<KeyPairInterface::Ptr> seedKeys;
    for (int i = 0; i < kInitialPeers; ++i)
    {
        seedKeys.push_back(cryptoSuite->signatureImpl()->generateKeyPair());
        syncStatus->insertEmptyPeer(seedKeys.back()->publicKey());
    }

    std::atomic<bool> stop{false};
    std::thread mutator([&] {
        std::vector<KeyPairInterface::Ptr> tmpKeys;
        for (int i = 0; i < 200 && !stop.load(); ++i)
        {
            KeyPairInterface::Ptr k = cryptoSuite->signatureImpl()->generateKeyPair();
            syncStatus->insertEmptyPeer(k->publicKey());
            tmpKeys.push_back(k);
            if (i >= 8)
            {
                syncStatus->deletePeer(tmpKeys[i - 8]->publicKey());
            }
        }
    });

    for (int i = 0; i < 200 && !stop.load(); ++i)
    {
        std::atomic<int> visited{0};
        syncStatus->foreachPeer([&](PeerStatus::Ptr _peer) {
            if (_peer)
            {
                ++visited;
            }
            return true;
        });
        // Sanity: visited must be non-negative; race detection is the real
        // assertion (delivered by TSan).
        (void)visited;
    }

    stop.store(true);
    mutator.join();
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
