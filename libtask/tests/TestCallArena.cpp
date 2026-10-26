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
 * @brief Pins the two properties of task::tbb::runInCallArena that this tree actually relies on
 *        and can assert deterministically:
 *
 *   1. the body does not run on the caller -- enqueue is a hand-off, so an RPC IO thread that
 *      drives eth_call comes back immediately instead of parking on the EVM round trip;
 *   2. the body runs with a TBB task context, which is the precondition that makes
 *      task::tbb::syncWait's tbb::task::suspend legal inside it. Under task_arena::execute the
 *      body is not a task and that precondition is false.
 *
 * What is deliberately NOT asserted here: that a body which suspends is later resumed, and by
 * whom. Driving that needs a completion delivered from a task context (oneTBB's task::resume
 * requires one) and the production completions come from storage/IO threads; the shape is
 * therefore exercised by the eth_call integration path, not by a unit test that would have to
 * reproduce oneTBB's stack-switching rules. The previous version of this file claimed to pin it
 * while actually awaiting with the plain task::wait and resuming from a detached thread, so it
 * asserted neither property and could outlive its own stack -- which is what this file stops
 * doing.
 */

#include "bcos-task/TBBWait.h"
#include <oneapi/tbb/task.h>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

namespace task = bcos::task;

namespace
{
/// Shared by the test and the enqueued body; nothing lives on the test's stack, so a body that
/// runs late cannot touch freed memory.
struct State
{
    std::atomic<bool> ran{false};
    std::atomic<bool> hasTaskContext{false};
    std::atomic<bool> threadIndexInitialized{false};
};

/// Bounded wait on @p flag, so a broken mechanism fails the test instead of hanging it.
bool waitFor(std::atomic<bool> const& flag)
{
    for (int i = 0; i < 5000 && !flag.load(); ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return flag.load();
}
}  // namespace

BOOST_AUTO_TEST_SUITE(CallArenaTest)

/// Both properties in one hand-off.
BOOST_AUTO_TEST_CASE(BodyRunsLaterInATaskContext)
{
    auto state = std::make_shared<State>();

    std::thread caller{[state] {
        task::tbb::runInCallArena([state] {
            state->hasTaskContext = (oneapi::tbb::task::current_context() != nullptr);
            state->threadIndexInitialized = (oneapi::tbb::this_task_arena::current_thread_index() !=
                                             oneapi::tbb::task_arena::not_initialized);
            state->ran = true;
        });
    }};
    caller.join();

    // Property 1: the caller's thread is back and the body has not been executed on it. This is
    // the assertion that fails if the enqueue is ever replaced by a blocking execute.
    BOOST_CHECK_MESSAGE(!state->ran.load(), "runInCallArena executed the body on the caller");

    BOOST_REQUIRE_MESSAGE(waitFor(state->ran), "the body never ran");
    // Property 2: the body is a TBB task, so syncWait has something to suspend.
    BOOST_CHECK_MESSAGE(state->hasTaskContext.load(),
        "the body has no TBB task context, so syncWait cannot suspend there");
    BOOST_CHECK_MESSAGE(state->threadIndexInitialized.load(),
        "the body did not run on an initialized arena thread");
}

/// The body is moved into the arena, not copied: an eth_call body moves a transaction and a
/// callback, and neither is copyable in production.
BOOST_AUTO_TEST_CASE(MoveOnlyBodyIsAccepted)
{
    struct MoveOnly
    {
        std::shared_ptr<State> state;
        MoveOnly(MoveOnly const&) = delete;
        MoveOnly& operator=(MoveOnly const&) = delete;
        MoveOnly(MoveOnly&&) noexcept = default;
        MoveOnly& operator=(MoveOnly&&) noexcept = default;
        explicit MoveOnly(std::shared_ptr<State> state) : state(std::move(state)) {}
        void operator()() const { state->ran = true; }
    };

    auto state = std::make_shared<State>();
    task::tbb::runInCallArena(MoveOnly{state});
    BOOST_CHECK_MESSAGE(waitFor(state->ran), "a move-only body never ran");
}

BOOST_AUTO_TEST_SUITE_END()
