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
 * @brief Regression tests for FIB-185: consensus-timer re-arm watchdog
 * @file FIB185_ConsensusRearmWatchdogTest.cpp
 * @date 2026-06-15
 */
#include "test/unittests/pbft/PBFTFixture.h"
#include <chrono>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <memory>
#include <thread>
#include <boost/test/unit_test.hpp>

using namespace bcos::crypto;

namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(FIB185_ConsensusRearmWatchdogTest, TestPromptFixture)

inline PBFTFixture::Ptr makeConsensusFixture()
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    auto fixture = createPBFTFixture(cryptoSuite);
    // make the node a consensus node so the watchdog's isConsensusNode() guard passes.
    // Note: intentionally do NOT call init() here. init() starts the engine worker and the
    // periodic checkpoint timer, which would invoke the watchdog concurrently and race the
    // deterministic timer/timeout state these tests set. The watchdog only reads config +
    // m_stopped (default false), so the engine need not be started for the unit under test.
    fixture->appendConsensusNode(fixture->nodeID());
    return fixture;
}

// Timer::start() now dispatches startTimer() onto the io_context instead of flipping the running
// flag on the caller's thread, so arming is only observable once the pool thread has run it. The
// assertion is unchanged in meaning -- "the timer ends up armed" -- it just has to wait for the
// dispatch rather than read the flag in the same breath.
inline bool waitTimerRunning(const std::shared_ptr<bcos::Timer>& _timer)
{
    for (int i = 0; i < 200 && !_timer->running(); ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return _timer->running();
}

// FIB-185: the wedge signature is a consensus node sitting in the timeout state whose
// view-change timer is not running. The watchdog must re-arm the timer.
BOOST_AUTO_TEST_CASE(WatchdogRearmsStalledTimer)
{
    auto fixture = makeConsensusFixture();
    auto config = fixture->pbftConfig();
    BOOST_CHECK(config->isConsensusNode());

    // simulate the wedge: in timeout state but the consensus timer is stopped
    config->setTimeoutState(true);
    config->timer()->stop();
    BOOST_CHECK(config->timeout());
    BOOST_CHECK(!config->timer()->running());

    fixture->pbftEngine()->checkConsensusTimerWatchdogForTest();

    // the watchdog re-armed the timer
    BOOST_CHECK(waitTimerRunning(config->timer()));

    config->timer()->stop();
}

// FIB-185: the watchdog must NOT re-arm the timer when the node is not in the timeout state.
// Re-arming outside timeout would risk spurious view-change activity.
BOOST_AUTO_TEST_CASE(WatchdogDoesNotRearmWhenNotInTimeout)
{
    auto fixture = makeConsensusFixture();
    auto config = fixture->pbftConfig();
    BOOST_CHECK(config->isConsensusNode());

    config->setTimeoutState(false);
    config->timer()->stop();
    BOOST_CHECK(!config->timeout());
    BOOST_CHECK(!config->timer()->running());

    fixture->pbftEngine()->checkConsensusTimerWatchdogForTest();

    // not in timeout: the watchdog leaves the stopped timer alone
    BOOST_CHECK(!config->timer()->running());
}

// FIB-185: when the timer is already running the watchdog is a no-op (it does not double-arm).
BOOST_AUTO_TEST_CASE(WatchdogIsNoopWhenTimerRunning)
{
    auto fixture = makeConsensusFixture();
    auto config = fixture->pbftConfig();
    BOOST_CHECK(config->isConsensusNode());

    config->setTimeoutState(true);
    config->timer()->start();
    BOOST_CHECK(waitTimerRunning(config->timer()));

    fixture->pbftEngine()->checkConsensusTimerWatchdogForTest();

    // still running, no crash / no double-arm side effect
    BOOST_CHECK(config->timer()->running());

    config->timer()->stop();
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
