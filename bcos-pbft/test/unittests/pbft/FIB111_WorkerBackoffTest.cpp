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
 * @brief Regression test for FIB-111: ConsensusEngine::workerProcessLoop() lacked
 *        backoff between iterations causing 100% CPU spin on repeated exceptions.
 * @file FIB111_WorkerBackoffTest.cpp
 * @author: claude
 * @date 2026-05-07
 */
#include "bcos-pbft/core/ConsensusEngine.h"
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

namespace bcos::test
{

// A minimal ConsensusEngine subclass that always throws from executeWorker(),
// simulating the malformed-message scenario that triggers the busy-spin.
class ThrowingConsensusEngine : public bcos::consensus::ConsensusEngine
{
public:
    std::atomic<int> throwCount{0};
    std::atomic<bool> stopped{false};

    // Use 5 ms idle wait so the worker is not entirely sleeping by default
    ThrowingConsensusEngine() : ConsensusEngine("FIB111TestEngine", 5) {}

    void executeWorker() override
    {
        ++throwCount;
        throw std::runtime_error("simulated invalid consensus message");
    }

    // Override stop to record it
    void stop() override
    {
        stopped.store(true);
        ConsensusEngine::stop();
    }
};

BOOST_FIXTURE_TEST_SUITE(FIB111Test, TestPromptFixture)

// Before FIB-111 fix: repeated exceptions in executeWorker() caused the worker
// thread to spin at 100% CPU because there was no backoff in the catch block.
//
// After fix: a timed wait is added after exceptions so the thread yields CPU.
// We verify this by measuring how many times executeWorker() is called (and thus
// throws) during a fixed wall-clock window. With backoff the count must be
// substantially lower than without (upper bound).
BOOST_AUTO_TEST_CASE(exception_loop_has_backoff)
{
    auto engine = std::make_shared<ThrowingConsensusEngine>();

    // Start the worker thread
    engine->start();

    // Let it run for 200 ms
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Stop it
    engine->stop();

    int count = engine->throwCount.load();

    // Without any backoff at all, on a modern CPU we'd expect thousands of
    // iterations in 200 ms. With a post-exception sleep >= 10 ms, we expect
    // fewer than ~25 iterations in 200 ms.
    // We use 100 as a generous upper bound to avoid false positives on slow CI,
    // while still catching a true busy-spin (which would be ~10000+).
    BOOST_CHECK_MESSAGE(count < 100,
        "FIB-111: workerProcessLoop() must not busy-spin on exceptions; "
        "observed " +
            std::to_string(count) + " throws in 200ms (expected < 100)");

    // Also sanity-check at least one exception happened (engine actually ran)
    BOOST_CHECK_GE(count, 1);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
