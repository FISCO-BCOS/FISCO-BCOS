/*
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
 * @brief Unit tests for Deadline: expiry, disarm, destruction, and the late-expiry guard that stops
 *        an already-QUEUED expiry from cancelling the NEXT operation on the same socket.
 *
 * The late-expiry tests drive the io_context with poll()/poll_one() on the test thread instead of
 * a background runner, and stage the race for real: advanceClock() expires the timer without
 * running anything, then moveExpiredHandlerToReadyQueue() wakes the scheduler so the expired
 * handler is moved into the ready queue — queued but not yet run — while the test disarms and
 * re-arms underneath it. With a live runner thread the queued handler simply runs, and the race
 * cannot be reproduced.
 * @file TestDeadline.cpp
 */
#include "bcos-task/Deadline.h"
#include "bcos-task/Task.h"
#include "bcos-task/Wait.h"
#include <boost/asio/error.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <chrono>
#include <functional>
#include <stdexcept>
#include <thread>

using namespace bcos::task;
using namespace std::chrono_literals;

namespace
{
std::size_t poll(boost::asio::io_context& io)
{
    // poll() runs until the queue is empty and then leaves the context STOPPED, so the next poll()
    // would return 0 without running anything. restart() is what makes repeated polls work.
    io.restart();
    return io.poll();
}

// Runs the io_context's handlers on a background thread, the way a node services a socket. Used
// only by the tests that have no queued-handler race to stage (real expiry, disarm, sleep).
class IORunner
{
public:
    IORunner() : m_work(boost::asio::make_work_guard(m_io)), m_thread([this] { m_io.run(); }) {}
    ~IORunner()
    {
        m_work.reset();
        m_io.stop();
        m_thread.join();
    }

    IORunner(const IORunner&) = delete;
    IORunner& operator=(const IORunner&) = delete;

    boost::asio::io_context& io() noexcept { return m_io; }

    // Wait until `predicate` holds, so a negative assertion (nothing fired) is only taken after the
    // handler had a real chance to run.
    static bool waitFor(const std::function<bool()>& predicate, std::chrono::milliseconds limit)
    {
        const auto deadline = std::chrono::steady_clock::now() + limit;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (predicate())
            {
                return true;
            }
            std::this_thread::sleep_for(1ms);
        }
        return predicate();
    }

private:
    boost::asio::io_context m_io;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> m_work;
    std::thread m_thread;
};

// Sleep until `duration` has elapsed on the timer clock, WITHOUT running the io_context. A timer
// armed before this call is therefore expired but its handler is still queued, which is the state
// every late-expiry test below needs.
void advanceClock(std::chrono::milliseconds duration)
{
    const auto target = boost::asio::steady_timer::clock_type::now() + duration;
    while (boost::asio::steady_timer::clock_type::now() < target)
    {
        std::this_thread::sleep_for(1ms);
    }
}

// Moves an EXPIRED timer's handler from asio's timer queue into the io_context's ready queue,
// WITHOUT running it — the state cancel()/expires_after() cannot recall, which is exactly what
// Deadline's generation guard exists for. The sentinel is posted first, so it sits at the head of
// the ready queue: poll_one() wakes the scheduler (which moves every expired timer handler into
// the ready queue) but executes only the head — the sentinel. The timer must already be armed and
// expired (see advanceClock) before this call.
void moveExpiredHandlerToReadyQueue(boost::asio::io_context& io)
{
    bool sentinelRan = false;
    boost::asio::post(io, [&sentinelRan] { sentinelRan = true; });
    BOOST_REQUIRE_EQUAL(io.poll_one(), 1);
    BOOST_REQUIRE(sentinelRan);
}
}  // namespace

BOOST_AUTO_TEST_SUITE(DeadlineTest)

// A deadline that is never armed must never fire, and destroying it must be a no-op.
BOOST_AUTO_TEST_CASE(NeverArmedNeverFires)
{
    boost::asio::io_context io;
    {
        Deadline deadline(io, 10);
        BOOST_CHECK(!deadline.expired());
    }
    // Nothing can fire: arm() was never called, so no timer wait was ever started.
    BOOST_CHECK_EQUAL(poll(io), 0);
}

// Expiry path: the handler runs once, with an empty error code, and expired() reports it.
BOOST_AUTO_TEST_CASE(ExpiryRunsTheHandlerOnce)
{
    boost::asio::io_context io;
    Deadline deadline(io, 5);
    int calls = 0;
    bool sawEmptyError = false;
    deadline.arm([&calls, &sawEmptyError](boost::system::error_code error) {
        ++calls;
        sawEmptyError = !error;
    });

    advanceClock(200ms);
    BOOST_CHECK_EQUAL(poll(io), 1);
    BOOST_CHECK_EQUAL(calls, 1);
    BOOST_CHECK(sawEmptyError);
    BOOST_CHECK(deadline.expired());

    // A second poll must not resurrect the handler.
    BOOST_CHECK_EQUAL(poll(io), 0);
    BOOST_CHECK_EQUAL(calls, 1);
    deadline.disarm();
}

// Disarm before expiry: the handler never runs, and expired() stays false.
BOOST_AUTO_TEST_CASE(DisarmBeforeExpirySuppressesTheHandler)
{
    boost::asio::io_context io;
    Deadline deadline(io, 50);
    int calls = 0;

    deadline.arm([&calls](boost::system::error_code) { ++calls; });
    deadline.disarm();

    advanceClock(200ms);
    (void)poll(io);

    BOOST_CHECK_EQUAL(calls, 0);
    BOOST_CHECK(!deadline.expired());
}

// Re-arm after a real expiry must still work: the generation guard rejects late expiries without
// breaking the normal "expires, then the caller arms again" sequence.
BOOST_AUTO_TEST_CASE(RearmAfterExpiryStillFires)
{
    boost::asio::io_context io;
    Deadline deadline(io, 5);
    int calls = 0;

    deadline.arm([&calls](boost::system::error_code) { ++calls; });
    advanceClock(200ms);
    BOOST_CHECK_EQUAL(poll(io), 1);
    BOOST_CHECK_EQUAL(calls, 1);
    deadline.disarm();

    deadline.arm([&calls](boost::system::error_code) { ++calls; });
    advanceClock(200ms);
    BOOST_CHECK_EQUAL(poll(io), 1);
    BOOST_CHECK_EQUAL(calls, 2);
    deadline.disarm();
}

// THE regression test. This is the exact race Deadline exists for.
//
// A per-read timer expires at the same moment the read completes successfully. The coroutine
// resumes on the read's completion, calls disarm(), and starts the NEXT read with a fresh arm().
// The expiry handler for the PREVIOUS read was already moved to the io_context's ready queue by
// then, and cancel() cannot recall it. If it ran unchecked it would cancel the NEXT read on the
// same socket — which is why the guard must reject it, not just "cancel and hope".
//
// Steps 1-3 stage the race for real: the old expiry sits in the ready queue (queued but not run)
// while the test disarms and re-arms underneath it. Step 4 is the assertion: the stale handler must
// run — poll() reports exactly it — and the guard must reject it.
BOOST_AUTO_TEST_CASE(QueuedExpiryCannotCancelTheNextOperation)
{
    boost::asio::io_context io;
    // A 50ms timeout keeps step 4 deterministic: the gap between the re-arm and the poll is
    // microseconds, so the fresh deadline cannot expire while the stale handler runs.
    Deadline deadline(io, 50);
    int staleCalls = 0;
    int nextCalls = 0;

    // 1. The previous read arms its deadline, the timer expires, and its handler is moved into the
    //    ready queue, where cancel() cannot recall it.
    deadline.arm([&staleCalls](boost::system::error_code) { ++staleCalls; });
    advanceClock(200ms);
    moveExpiredHandlerToReadyQueue(io);

    // 2. The previous read completes successfully and the owner disarms — but the expiry handler is
    //    already queued, so it is not "gone".
    deadline.disarm();

    // 3. The next read arms a fresh deadline.
    deadline.arm([&nextCalls](boost::system::error_code) { ++nextCalls; });
    BOOST_CHECK(!deadline.expired());

    // 4. Drive the queue. The stale expiry must prove it cannot cancel the next operation.
    BOOST_CHECK_EQUAL(poll(io), 1);
    BOOST_CHECK_EQUAL(staleCalls, 0);
    BOOST_CHECK_EQUAL(nextCalls, 0);
    BOOST_CHECK(!deadline.expired());

    // 5. The new deadline still works.
    advanceClock(200ms);
    BOOST_CHECK_EQUAL(poll(io), 1);
    BOOST_CHECK_EQUAL(nextCalls, 1);
    BOOST_CHECK(deadline.expired());
    deadline.disarm();
}

// The same guard must hold when the owner re-arms WITHOUT an intervening disarm(). This shape is
// not used in the tree, but it pins down the ordering inside arm(): the generation must be bumped
// before the timer is re-armed, because a handler already queued by the previous arm can still run
// after the expiry changes — expires_after() cannot recall it from the ready queue.
BOOST_AUTO_TEST_CASE(QueuedExpirySurvivesARearmWithoutDisarm)
{
    boost::asio::io_context io;
    Deadline deadline(io, 50);
    int staleCalls = 0;
    int nextCalls = 0;

    deadline.arm([&staleCalls](boost::system::error_code) { ++staleCalls; });
    advanceClock(200ms);
    moveExpiredHandlerToReadyQueue(io);

    // No disarm() here on purpose: the re-arm itself must invalidate the queued expiry.
    deadline.arm([&nextCalls](boost::system::error_code) { ++nextCalls; });

    // The stale expiry runs — poll() reports exactly it — and the guard must reject it.
    BOOST_CHECK_EQUAL(poll(io), 1);
    BOOST_CHECK_EQUAL(staleCalls, 0);
    BOOST_CHECK_EQUAL(nextCalls, 0);
    deadline.disarm();
}

// Destruction while an expiry is already queued: the queued handler holds the shared state, so it
// must observe the stale generation and not fire into a destroyed owner.
BOOST_AUTO_TEST_CASE(DestructionRejectsQueuedExpiry)
{
    boost::asio::io_context io;
    int staleCalls = 0;

    {
        Deadline deadline(io, 50);
        deadline.arm([&staleCalls](boost::system::error_code) { ++staleCalls; });
        advanceClock(200ms);
        moveExpiredHandlerToReadyQueue(io);
        // ~Deadline runs here with the expiry already queued.
    }

    // The queued expiry runs — poll() reports exactly it — and must be rejected.
    BOOST_CHECK_EQUAL(poll(io), 1);
    BOOST_CHECK_EQUAL(staleCalls, 0);
}

// expired() is a per-arm report: it is false again as soon as the deadline is re-armed.
BOOST_AUTO_TEST_CASE(ExpiredIsResetByTheNextArm)
{
    boost::asio::io_context io;
    Deadline deadline(io, 5);
    int calls = 0;

    deadline.arm([&calls](boost::system::error_code) { ++calls; });
    advanceClock(200ms);
    BOOST_CHECK_EQUAL(poll(io), 1);
    BOOST_CHECK(deadline.expired());

    deadline.disarm();
    deadline.arm([&calls](boost::system::error_code) { ++calls; });
    BOOST_CHECK(!deadline.expired());
    deadline.disarm();
}

// A deadline that is re-armed over and over (the session read loop) must keep working: the first
// expiry wins each time, and a queued expiry from the previous iteration never leaks into the next.
BOOST_AUTO_TEST_CASE(ReadLoopRearmPatternStaysCorrect)
{
    boost::asio::io_context io;
    Deadline deadline(io, 5);
    int expiries = 0;

    for (int iteration = 0; iteration < 5; ++iteration)
    {
        deadline.arm([&expiries](boost::system::error_code) { ++expiries; });

        if (iteration % 2 == 0)
        {
            // This "read" times out.
            advanceClock(200ms);
            (void)poll(io);
            BOOST_CHECK_EQUAL(expiries, iteration / 2 + 1);
        }
        else
        {
            // This "read" completes before the timeout, with the expiry possibly already queued.
            deadline.disarm();
            (void)poll(io);
            BOOST_CHECK_EQUAL(expiries, (iteration + 1) / 2);
        }
    }

    deadline.disarm();
}

// awaitableSleep is the coroutine-facing timer: it must suspend the frame and resume it after the
// duration, with an empty error code on the normal path.
BOOST_AUTO_TEST_CASE(AwaitableSleepResumesAfterTheDuration)
{
    IORunner runner;
    const auto started = std::chrono::steady_clock::now();

    auto error = syncWait([&runner]() -> Task<boost::system::error_code> {
        co_return co_await awaitableSleep(runner.io(), 20);
    }());

    const auto elapsed = std::chrono::steady_clock::now() - started;
    BOOST_CHECK(!error);
    BOOST_CHECK(elapsed >= 15ms);
}

// Destroying the awaiting coroutine's frame mid-wait must not resume a destroyed coroutine or
// write into freed frame memory: ~SleepAwaitable cancels the timer and marks the shared state
// dead, and asio still delivers the pending handler (with operation_aborted), which must no-op.
// Under ASan this catches the original bug: the handler wrote into the freed frame.
BOOST_AUTO_TEST_CASE(AwaitableSleepFrameDestroyedMidWaitDoesNotResume)
{
    boost::asio::io_context io;
    {
        auto task = awaitableSleep(io, 60000);
        task.start();  // suspends inside SleepAwaitable; the wait is pending on `io`
    }  // ~Task destroys the frame; ~SleepAwaitable cancels the timer and marks the state dead

    // The cancelled wait's handler is still delivered; it must observe the dead state and no-op.
    BOOST_CHECK_EQUAL(poll(io), 1);
}

BOOST_AUTO_TEST_CASE(TimeoutHandlerExceptionDoesNotEscapeIoContext)
{
    boost::asio::io_context io;
    Deadline deadline(io, 5);
    int calls = 0;
    deadline.arm([&calls](boost::system::error_code) {
        ++calls;
        throw std::runtime_error("timeout handler failed");
    });

    advanceClock(200ms);
    std::size_t handlers = 0;
    BOOST_CHECK_NO_THROW(handlers = poll(io));
    BOOST_CHECK_EQUAL(handlers, 1);
    BOOST_CHECK_EQUAL(calls, 1);
    BOOST_CHECK(deadline.expired());
    deadline.disarm();
}

BOOST_AUTO_TEST_CASE(AwaitableSleepResumeExceptionDoesNotEscapeIoContext)
{
    boost::asio::io_context io;
    bool resumed = false;
    bcos::task::wait([](boost::asio::io_context& io, bool& resumed) -> Task<void> {
        co_await awaitableSleep(io, 5);
        resumed = true;
        throw std::runtime_error("sleep consumer failed");
    }(io, resumed));

    advanceClock(200ms);
    std::size_t handlers = 0;
    BOOST_CHECK_NO_THROW(handlers = poll(io));
    BOOST_CHECK_EQUAL(handlers, 1);
    BOOST_CHECK(resumed);
}

BOOST_AUTO_TEST_SUITE_END()
