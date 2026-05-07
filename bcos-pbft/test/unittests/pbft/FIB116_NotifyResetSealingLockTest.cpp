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
 * @brief Regression test for FIB-116: notifyResetSealing() must hold
 *        x_committedProposal when reading m_committedProposal->index().
 *        Verified by exercising concurrent setCommittedProposal / notifyResetSealing
 *        without triggering data-race sanitiser failures.
 * @file FIB116_NotifyResetSealingLockTest.cpp
 * @date 2026-05-07
 */
#include "test/unittests/pbft/PBFTFixture.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <thread>

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::crypto;
using namespace bcos::test;

namespace bcos::test
{

BOOST_AUTO_TEST_SUITE(FIB116_NotifyResetSealingLockTest)

// Test: notifyResetSealing() reads m_committedProposal->index() under the lock.
// We exercise concurrent setCommittedProposal / notifyResetSealing to ensure the
// lock is taken correctly (no null-ptr deref, no data race).
BOOST_AUTO_TEST_CASE(ConcurrentSetCommittedAndNotifyReset)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signImpl, nullptr);

    auto faker = createPBFTFixture(cryptoSuite);
    faker->appendConsensusNode(faker->nodeID());
    faker->init();

    auto pbftConfig = faker->pbftConfig();

    // Register a sealer-reset notifier that records calls.
    std::atomic<int> resetCallCount{0};
    pbftConfig->registerSealerResetNotifier([&](std::function<void(Error::Ptr)> cb) {
        ++resetCallCount;
        cb(nullptr);  // success
    });

    // Run setCommittedProposal and notifyResetSealing concurrently for many iterations.
    constexpr int kIter = 200;
    std::atomic<bool> go{false};

    auto writerThread = std::thread([&]() {
        while (!go.load())
        {
        }
        for (int i = 1; i <= kIter; ++i)
        {
            auto proposal = pbftConfig->pbftMessageFactory()->createPBFTProposal();
            proposal->setIndex(i);
            pbftConfig->setCommittedProposal(proposal);
        }
    });

    auto readerThread = std::thread([&]() {
        while (!go.load())
        {
        }
        for (int i = 0; i < kIter; ++i)
        {
            // Must not crash even when m_committedProposal is being replaced.
            BOOST_CHECK_NO_THROW(pbftConfig->notifyResetSealing());
        }
    });

    go.store(true);
    writerThread.join();
    readerThread.join();

    // At least some resets must have been triggered.
    BOOST_CHECK(resetCallCount.load() > 0);
}

// Test: reNotifySealer also reads m_committedProposal->index() — must not crash.
BOOST_AUTO_TEST_CASE(ReNotifySealerNoNullDeref)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signImpl, nullptr);

    auto faker = createPBFTFixture(cryptoSuite);
    faker->appendConsensusNode(faker->nodeID());
    faker->init();

    auto pbftConfig = faker->pbftConfig();

    // A valid committedProposal must exist before calling reNotifySealer.
    BOOST_REQUIRE(pbftConfig->committedProposal() != nullptr);

    // reNotifySealer reads m_committedProposal->index(); must not crash.
    BOOST_CHECK_NO_THROW(pbftConfig->reNotifySealer(pbftConfig->committedProposal()->index() + 1));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
