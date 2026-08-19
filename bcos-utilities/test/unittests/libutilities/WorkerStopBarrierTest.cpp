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
 * @brief Regression test: Worker::stopWorking() must not return while executeWorker() runs
 * @file WorkerStopBarrierTest.cpp
 * @date 2026-08-18
 *
 * Sealer's FIB164_OnReadyLifecycle test crashed at random on CI (SIGSEGV / "pthread_mutex_lock:
 * Invalid argument") because stopWorking()'s compare_exchange only stops FUTURE ticks from
 * entering executeWorker(); a tick that had already entered kept running on the io_context
 * thread. Sealer::stop() therefore returned while executeWorker() was still dereferencing
 * m_sealingManager, and the caller was free to destroy the Sealer right after -- freeing that
 * member out from under the running tick.
 *
 * ~Worker() does drain the io_context, but by C++ destruction order it runs AFTER the derived
 * class has already destroyed its own members, so it cannot protect them. The barrier belongs in
 * stopWorking(), while the whole object is still intact. This test pins that.
 */

#include "bcos-utilities/Worker.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <chrono>
#include <thread>

using namespace bcos;

namespace bcos::test
{
namespace
{
// How long the blocked tick stays inside executeWorker() after it has observed the stop request.
// This is a margin, not a timing assumption: without the barrier stopWorking() returns in
// microseconds (it only posts a cancel), so any value well above scheduling jitter makes the
// pre-fix behaviour deterministically observable. With the barrier the wait is unbounded from the
// test's point of view -- stopWorking() returns only once executeWorker() has returned, whatever
// this value is.
constexpr auto c_holdAfterStopRequested = std::chrono::milliseconds(200);

// Bound on waiting for the first tick to start, so a broken Worker fails the assert below rather
// than hanging the suite.
constexpr auto c_tickStartTimeout = std::chrono::seconds(5);
}  // namespace

// A Worker whose first tick parks inside executeWorker() until the stop request is visible, then
// lingers before returning. Everything is observed through atomics -- the test never sleeps to
// "wait for" anything.
class BarrierWorker : public Worker
{
public:
    explicit BarrierWorker(boost::asio::io_context& _ioContext)
      : Worker(_ioContext, "BarrierWorker", 0)
    {}

    void run() { startWorking(); }
    void stop() { stopWorking(); }

    bool entered() const { return m_entered.load(); }
    bool leftExecuteWorker() const { return m_left.load(); }

protected:
    void executeWorker() override
    {
        // Only the first tick parks; later ticks (if any) must not block the barrier.
        if (m_entered.exchange(true))
        {
            return;
        }
        // Park until stopWorking() has flipped the state. That instant is exactly where the
        // pre-fix code returned from stopWorking(), so from here on the only thing that can keep
        // stopWorking() waiting is the barrier under test.
        while (!shouldStop())
        {
            std::this_thread::yield();
        }
        std::this_thread::sleep_for(c_holdAfterStopRequested);
        m_left.store(true);
    }

private:
    std::atomic_bool m_entered{false};
    std::atomic_bool m_left{false};
};

BOOST_FIXTURE_TEST_SUITE(WorkerStopBarrier, TestPromptFixture)

BOOST_AUTO_TEST_CASE(stopWorkingWaitsForInFlightExecuteWorker)
{
    boost::asio::io_context ioContext;
    auto work = boost::asio::make_work_guard(ioContext);
    std::thread ioThread([&]() { ioContext.run(); });

    BarrierWorker worker(ioContext);
    worker.run();

    // Hand the io_context thread time to get INTO executeWorker(); stopping before the tick even
    // starts would test nothing. Bounded so a broken Worker fails the assert instead of hanging.
    auto deadline = std::chrono::steady_clock::now() + c_tickStartTimeout;
    while (!worker.entered() && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::yield();
    }
    BOOST_REQUIRE_MESSAGE(worker.entered(), "executeWorker() never ran; the test setup is broken");

    worker.stop();

    // The regression. Pre-fix stopWorking() posted the timer cancel and returned immediately,
    // leaving executeWorker() running on the io_context thread -- so a caller that destroys the
    // derived object right here frees members the tick is still using.
    BOOST_CHECK_MESSAGE(worker.leftExecuteWorker(),
        "stopWorking() returned while executeWorker() was still running: a derived class "
        "destroyed right after stop() would free its members under the running tick");

    work.reset();
    ioContext.stop();
    ioThread.join();
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
