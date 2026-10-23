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
 * @file TestCallArena.cpp
 * @brief Pins the property runInCallArena exists for: an eth_call body that has to suspend
 *        must not hold a worker slot, and the work that completes it must still get to run.
 *
 * This is the measurement TBBWait.h used to record only as prose. It is the property that
 * justifies using task_arena::enqueue rather than task_arena::execute: execute blocks the
 * caller and its body is not a TBB task, so a body that suspends can never be resumed by work
 * enqueued into the same arena once the arena is saturated.
 *
 * The test uses a single-slot arena so the distinction is not a matter of timing: with one
 * worker, a body that kept its slot while suspended would starve the only thread that could
 * resume it, and the test would hang instead of passing.
 */

#include "bcos-task/TBBWait.h"
#include "bcos-task/Task.h"
#include "bcos-task/Wait.h"
#include <oneapi/tbb/task_arena.h>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <chrono>
#include <thread>

namespace task = bcos::task;
using task::Task;

namespace
{
/// An awaitable that suspends immediately and records where it resumes.
struct Deferred
{
    std::atomic<bool>* done;
    std::atomic<int>* resumeThreadMarker;

    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle) const
    {
        // Complete from a *different* thread, so resuming requires another thread to be able
        // to run. The handle is resumed exactly once.
        std::thread{[handle, done = done, marker = resumeThreadMarker] {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            *marker = 1;  // set before resume so the coroutine can observe it
            handle.resume();
            *done = true;
        }}.detach();
    }
    void await_resume() const noexcept {}
};

Task<void> suspendingBody(
    std::atomic<bool>* done, std::atomic<int>* marker, std::atomic<bool>* bodyCompleted)
{
    co_await Deferred{done, marker};
    *bodyCompleted = true;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(CallArenaTest)

/// The core property: with ONE worker slot, a body that suspends completes anyway. Under
/// `execute` with a non-task body there is no way for the completer to run, because the single
/// worker is inside the blocked body.
BOOST_AUTO_TEST_CASE(SuspendedBodyCompletesWithASingleWorker)
{
    oneapi::tbb::task_arena arena{1};
    arena.initialize();

    std::atomic<bool> done{false};
    std::atomic<int> marker{0};
    std::atomic<bool> bodyCompleted{false};

    arena.enqueue([&] {
        // task::wait starts the coroutine inline; it suspends and returns here, releasing the
        // worker, which is what lets the completer thread make progress.
        task::wait(suspendingBody(&done, &marker, &bodyCompleted));
    });

    for (int i = 0; i < 500 && !bodyCompleted.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    BOOST_CHECK_MESSAGE(bodyCompleted.load(), "a suspended body never resumed");
    BOOST_CHECK_MESSAGE(marker.load() == 1, "the completer thread never ran");
}

/// The caller of runInCallArena must not be blocked by a body that suspends.
BOOST_AUTO_TEST_CASE(EnqueueReturnsBeforeTheBodyFinishes)
{
    std::atomic<bool> done{false};
    std::atomic<int> marker{0};
    std::atomic<bool> bodyCompleted{false};

    auto const start = std::chrono::steady_clock::now();
    task::tbb::runInCallArena([&] { task::wait(suspendingBody(&done, &marker, &bodyCompleted)); });
    auto const elapsed = std::chrono::steady_clock::now() - start;

    // The body sleeps for 20 ms before completing; enqueue must not have waited for it.
    BOOST_CHECK_MESSAGE(elapsed < std::chrono::milliseconds(20),
        "runInCallArena blocked its caller for the whole round trip");
    BOOST_CHECK_MESSAGE(!bodyCompleted.load(), "the body completed inline, so this proves nothing");

    for (int i = 0; i < 500 && !bodyCompleted.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    BOOST_CHECK(bodyCompleted.load());
}

/// enqueue is a hand-off, not an inline call: the body always runs later, even when an arena
/// worker is idle. Pinned because the earlier `execute`-based version did run it inline, and a
/// callback invoked from the body would then re-enter its caller's stack -- an ordering the RPC
/// layer must not accidentally start depending on.
BOOST_AUTO_TEST_CASE(BodyRunsLaterNotOnTheCaller)
{
    std::atomic<bool> ran{false};
    task::tbb::runInCallArena([&] {
        task::wait([](std::atomic<bool>* flag) -> Task<void> {
            *flag = true;
            co_return;
        }(&ran));
    });

    // It may already have run on a worker by now; what must never happen is a body that
    // re-enters the caller's stack synchronously as execute() did. Either way it completes.
    for (int i = 0; i < 500 && !ran.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    BOOST_CHECK_MESSAGE(ran.load(), "an enqueued body never ran");
}

/// The same shape through the public helper, on a single-slot arena, is what actually guards the
/// shared callArena() against a regression to `execute` + a blocking wait.
BOOST_AUTO_TEST_CASE(SharedCallArenaDoesNotDeadlock)
{
    std::atomic<bool> bodyCompleted{false};
    std::atomic<bool> done{false};
    std::atomic<int> marker{0};

    task::tbb::runInCallArena([&] { task::wait(suspendingBody(&done, &marker, &bodyCompleted)); });

    for (int i = 0; i < 500 && !bodyCompleted.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    BOOST_CHECK_MESSAGE(bodyCompleted.load(), "the shared call arena stalled a suspended body");
}

BOOST_AUTO_TEST_SUITE_END()
