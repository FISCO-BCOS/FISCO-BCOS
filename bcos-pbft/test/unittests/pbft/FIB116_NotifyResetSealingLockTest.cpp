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

// Test: canHandleNewProposal(msg) reads m_committedProposal->index() — must hold the lock
// AND null-check before deref.  Exercise concurrent setCommittedProposal to validate.
BOOST_AUTO_TEST_CASE(CanHandleNewProposalConcurrentReadSafe)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signImpl, nullptr);

    auto faker = createPBFTFixture(cryptoSuite);
    faker->appendConsensusNode(faker->nodeID());
    faker->init();

    auto pbftConfig = faker->pbftConfig();
    // Force the canHandleNewProposal() (no-arg) branch to return false so the message
    // variant actually reads m_committedProposal->index() on line 257.
    pbftConfig->setWaitSealUntil(pbftConfig->committedProposal()->index() + 100);

    auto msg = pbftConfig->pbftMessageFactory()->createPBFTMsg();
    msg->setIndex(pbftConfig->committedProposal()->index() + 1);

    constexpr int kIter = 200;
    std::atomic<bool> startFlag{false};

    auto writerThread = std::thread([&]() {
        while (!startFlag.load())
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
        while (!startFlag.load())
        {
        }
        for (int i = 0; i < kIter; ++i)
        {
            BOOST_CHECK_NO_THROW((void)pbftConfig->canHandleNewProposal(msg));
        }
    });

    startFlag.store(true);
    writerThread.join();
    readerThread.join();
}

// Test: resetConfig reads m_committedProposal->index() — must hold the lock so that
// the read does not race with concurrent setCommittedProposal.  Kept lightweight (one
// resetConfig call) because resetConfig also drives several unrelated subsystems
// (state-notifier / new-block-notifier / sync state) we do not need to exercise here.
BOOST_AUTO_TEST_CASE(ResetConfigReadsCommittedUnderLock)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signImpl, nullptr);

    auto faker = createPBFTFixture(cryptoSuite);
    faker->appendConsensusNode(faker->nodeID());
    faker->init();

    auto pbftConfig = faker->pbftConfig();
    // Seed committedProposal at a positive index so the resetConfig early-return guard
    // (`committedIndex > 0`) is guaranteed to fire and we exercise only the locked read.
    auto seed = pbftConfig->pbftMessageFactory()->createPBFTProposal();
    seed->setIndex(10);
    pbftConfig->setCommittedProposal(seed);

    constexpr int kWriterIter = 200;
    std::atomic<bool> startFlag{false};
    std::atomic<bool> stopWriter{false};

    auto writerThread = std::thread([&]() {
        while (!startFlag.load())
        {
        }
        for (int i = 11; i <= kWriterIter + 10 && !stopWriter.load(); ++i)
        {
            auto proposal = pbftConfig->pbftMessageFactory()->createPBFTProposal();
            proposal->setIndex(i);
            pbftConfig->setCommittedProposal(proposal);
        }
    });

    startFlag.store(true);
    // resetConfig with blockNumber == 0 hits the early-return path (committedIndex > 0),
    // so we exercise the (now-locked) committedIndex read without driving the heavy
    // subsystem work that follows.
    auto ledgerConfig = std::make_shared<bcos::ledger::LedgerConfig>();
    ledgerConfig->setBlockNumber(0);
    BOOST_CHECK_NO_THROW(pbftConfig->resetConfig(ledgerConfig, false));
    stopWriter.store(true);
    writerThread.join();
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
