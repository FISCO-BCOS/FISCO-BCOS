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
 * @file TestCallGroup.cpp
 * @brief Pins what BaselineScheduler's eth_call path relies on: `tbb::task_group::run` called
 *        from a plain (non-TBB) thread turns the body into a TBB task, without running it on the
 *        caller, and a body that awaits through `task::tbb::syncWait` is resumed and completed by
 *        another task of the same group.
 *
 * All three are properties of oneTBB that this tree now depends on instead of merely documenting,
 * so an upgrade that changes any of them fails here rather than silently reintroducing the
 * original hazard:
 *
 *   1. run() does not execute the body on the caller -- an RPC IO thread comes back immediately
 *      instead of parking on the EVM round trip;
 *   2. inside the body `task::current_context()` is non-null, which is what makes syncWait's
 *      `tbb::task::suspend` legal there (under a plain thread it is undefined);
 *   3. the suspended body is resumed from a later task of the same group and completes.
 *
 * The coroutine used here is deliberately minimal: it parks on a hand-rolled awaitable rather than
 * a storage read, because the property under test is "a syncWait-parked body can be resumed at
 * all", not what resumes it. The completion arrives as a separate group task, which is the shape
 * the scheduler's own callbacks have.
 *
 * Every flag and the parked handle live in a heap block shared by owner, so a task that runs late
 * cannot touch a dead stack. The only timing is a bounded wait for a flag, so a broken mechanism
 * reports a failure instead of hanging CI.
 */

#include "bcos-task/TBBWait.h"
#include "bcos-task/Task.h"
#include "bcos-task/Wait.h"
#include <oneapi/tbb/task.h>
#include <oneapi/tbb/task_group.h>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <chrono>
#include <coroutine>
#include <memory>
#include <thread>

namespace task = bcos::task;
using task::Task;

namespace
{
/// Shared by the caller thread, the group body and the completing task.
struct State
{
    std::atomic<bool> bodyStarted{false};
    std::atomic<bool> hasTaskContext{false};
    std::atomic<bool> bodyRanOnCaller{false};
    std::atomic<bool> parked{false};
    std::atomic<bool> completed{false};
    std::atomic<void*> parkedHandle{nullptr};
};

/// Parks the awaiting coroutine so the resume has to come from outside.
struct Park
{
    std::shared_ptr<State> state;

    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle) const noexcept
    {
        state->parkedHandle.store(handle.address());
        state->parked = true;
    }
    void await_resume() const noexcept {}
};

/// The shape BaselineScheduler::call hands to the group: await through task::tbb::syncWait.
Task<void> callShapedBody(std::shared_ptr<State> state)
{
    auto inner = [](std::shared_ptr<State> state) -> Task<void> {
        co_await Park{state};
        co_return;
    };
    task::tbb::syncWait(inner(state));
    state->completed = true;
    co_return;
}

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

BOOST_AUTO_TEST_SUITE(CallGroupTest)

/// Properties 1 and 2: the body is a TBB task, and it is not the caller.
BOOST_AUTO_TEST_CASE(BodyBecomesATbbTaskOffTheCaller)
{
    auto state = std::make_shared<State>();
    oneapi::tbb::task_group group;

    std::thread caller{[&] {
        auto const callerId = std::this_thread::get_id();
        group.run([state, callerId] {
            state->bodyRanOnCaller = (std::this_thread::get_id() == callerId);
            state->hasTaskContext = (oneapi::tbb::task::current_context() != nullptr);
            state->bodyStarted = true;
        });
    }};
    caller.join();

    BOOST_REQUIRE_MESSAGE(waitFor(state->bodyStarted), "the body never ran");
    // The assertion that fails if run() is ever replaced by a blocking execute, or if the body is
    // invoked inline on the submitting thread.
    BOOST_CHECK_MESSAGE(!state->bodyRanOnCaller.load(), "run() executed the body on the caller");
    BOOST_CHECK_MESSAGE(state->hasTaskContext.load(),
        "the body has no TBB task context, so task::tbb::syncWait cannot suspend there");
    group.wait();
}

/// Property 3: a body parked inside syncWait is resumed by another task of the same group.
BOOST_AUTO_TEST_CASE(SuspendedBodyIsResumedByAnotherGroupTask)
{
    auto state = std::make_shared<State>();
    oneapi::tbb::task_group group;

    group.run([state] { task::wait(callShapedBody(state)); });
    BOOST_REQUIRE_MESSAGE(waitFor(state->parked), "the awaited coroutine never parked");
    BOOST_CHECK_MESSAGE(!state->completed.load(), "the body completed without being resumed");

    // The completion arrives as a separate member of the same group, which is what a scheduler
    // callback is.
    group.run([state] {
        if (auto* handle = state->parkedHandle.load(); handle != nullptr)
        {
            std::coroutine_handle<>::from_address(handle).resume();
        }
    });
    group.wait();

    BOOST_CHECK_MESSAGE(waitFor(state->completed), "the suspended body was never completed");
}

/// A body whose await completes inline never parks: suspending is not a requirement for progress,
/// or an eth_call served entirely from warm state would hang.
BOOST_AUTO_TEST_CASE(BodyThatNeverParksCompletes)
{
    auto state = std::make_shared<State>();
    oneapi::tbb::task_group group;

    group.run([state] {
        task::wait([](std::shared_ptr<State> state) -> Task<void> {
            auto inlineTask = []() -> Task<void> { co_return; };
            task::tbb::syncWait(inlineTask());
            state->completed = true;
            co_return;
        }(state));
    });
    group.wait();

    BOOST_CHECK_MESSAGE(waitFor(state->completed), "an inline await never completed");
}

BOOST_AUTO_TEST_SUITE_END()
