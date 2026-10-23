/*
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
 */

#pragma once

#include "Task.h"
#include "Trait.h"
#include <oneapi/tbb/task.h>
#include <oneapi/tbb/task_arena.h>
#include <boost/atomic/atomic.hpp>
#include <boost/atomic/atomic_flag.hpp>
#include <concepts>
#include <memory>
#include <type_traits>
#include <utility>

namespace bcos::task::tbb
{

/*
bcos::task::tbb::syncWait仅在配合tbb::task_group使用时，才能有协程切换的效果，在下列函数中会阻塞外部线程，限制并发数：
- tbb::parallel_for
- tbb::parallel_for_each

bcos::task::tbb::syncWait can only have the effect of coroutine switching when used
with tbb::task_group, and will block external threads and limit the number of concurrent
transactions in the following functions
*/
constexpr inline struct SyncWait
{
    template <class Task>
    auto operator()(Task&& task, auto&&... args) const
        -> AwaitableReturnType<std::remove_cvref_t<Task>>
        requires IsAwaitable<Task>
    {
        using ReturnType = AwaitableReturnType<std::remove_cvref_t<Task>>;
        using ReturnTypeWrap = std::conditional_t<std::is_reference_v<ReturnType>,
            std::add_pointer_t<ReturnType>, ReturnType>;
        using ReturnVariant = std::conditional_t<std::is_void_v<ReturnType>,
            std::variant<std::monostate, std::exception_ptr>,
            std::variant<std::monostate, ReturnTypeWrap, std::exception_ptr>>;

        ReturnVariant result;
        boost::atomic_flag finished{};
        boost::atomic<oneapi::tbb::task::suspend_point> suspendPoint{};

        auto waitTask = [](Task&& task, decltype(result)& result, boost::atomic_flag& finished,
                            boost::atomic<oneapi::tbb::task::suspend_point>& suspendPoint,
                            auto&&...) -> task::Task<void> {
            try
            {
                if constexpr (std::is_void_v<ReturnType>)
                {
                    co_await std::forward<Task>(task);
                }
                else
                {
                    if constexpr (std::is_reference_v<ReturnType>)
                    {
                        decltype(auto) ref = co_await task;
                        result = std::addressof(ref);
                    }
                    else
                    {
                        result.template emplace<ReturnType>(co_await std::forward<Task>(task));
                    }
                }
            }
            catch (...)
            {
                result.template emplace<std::exception_ptr>(std::current_exception());
            }

            if (finished.test_and_set())
            {
                // finished已经被设置,说明外部已经suspend了,此处要获取suspendPoint并resume
                // finished has been set, which means that the external has been suspended, here you
                // need to get suspendPoint and resume
                suspendPoint.wait({});
                oneapi::tbb::task::resume(suspendPoint.load());
            }
        }(std::forward<Task>(task), result, finished, suspendPoint,
                                           std::forward<decltype(args)>(args)...);
        waitTask.start();

        if (!finished.test_and_set())
        {
            // finished第一次被设置,说明task还在执行中,suspend并等待task来执行resume
            // finished is set for the first time, indicating that the task is still being executed,
            // suspending and waiting for the task to execute resume
            oneapi::tbb::task::suspend([&](oneapi::tbb::task::suspend_point tag) {
                suspendPoint.store(tag);
                suspendPoint.notify_one();
            });
        }

        if (auto* exception = std::get_if<std::exception_ptr>(std::addressof(result)))
        {
            std::rethrow_exception(*exception);
        }

        if constexpr (!std::is_void_v<ReturnType>)
        {
            if constexpr (std::is_reference_v<ReturnType>)
            {
                return *(std::get<ReturnTypeWrap>(result));
            }
            else
            {
                return std::move(std::get<ReturnTypeWrap>(result));
            }
        }
    }
} syncWait{};

/// Which flavour of wait is legal where
/// ------------------------------------
/// `task::tbb::syncWait` suspends the *calling TBB task*
/// (oneapi::tbb::task::suspend/resume), so it needs a TBB task to suspend. It is legal only
/// inside a frame that already is one -- a parallel_pipeline filter, a parallel_invoke body --
/// and those are the frames the block pipeline uses it in.
///
/// A frame with no TBB task cannot use it: there is nothing to suspend. There,
/// `task::syncWait` (the plain, inline flavour) is the correct wait, but only when the awaited
/// work completes inline and therefore never really blocks. `SchedulerParallelImpl::
/// executeSinglePass`'s mergeLastStorage is exactly that: it merges into the in-memory mutable
/// layer and returns, so the plain wait is both correct and cheaper.
///
/// `runInCallArena` below is the third case: code on a plain thread (an RPC IO thread driving
/// eth_call) that must *become* a TBB task. It enqueues into a shared arena, so the caller
/// returns immediately and the body runs as a real TBB task.

/// The arena the read-only call round trips run in.
///
/// Shared, so wrapping a call does not put arena construction on the hot path. Default
/// concurrency: this is a context, not a throttle.
inline oneapi::tbb::task_arena& callArena()
{
    static oneapi::tbb::task_arena arena;
    return arena;
}

/// Hand @p body to callArena() as a TBB task, without waiting for it.
///
/// `task_arena::enqueue` wraps the body in an `enqueue_task`, which *is* a TBB task, so
/// `task::tbb::syncWait` inside it has something to suspend. (An `execute` body would not:
/// `current_task_ptr()` is null there, and `execute` also blocks the caller until the body
/// finishes, which is the opposite of what an RPC IO thread wants.)
///
/// The caller returns as soon as the task is enqueued: enqueue is a hand-off, and the body runs
/// later on an arena worker. That asynchrony is the point, but it does mean a completion
/// callback invoked from the body never re-enters its caller's stack.
///
/// libtask/tests/TestCallArena.cpp pins the property this choice rests on, so a oneTBB upgrade
/// that changes it fails instead of silently reintroducing the hazard: with a single worker
/// slot, a body that suspends still completes, because the suspended enqueue_task releases the
/// slot its completer needs. Under `execute` there would be no task to suspend at all -- that
/// is a fact about which oneTBB context the body runs in, not something the test can observe
/// from the outside, so it is recorded here rather than asserted there.
///
/// @p body must return void: enqueue has nowhere to put a value, so a body with a result would
/// lose it silently. The constraint below turns that into a compile error.
///
/// The body is boxed in a shared_ptr before being handed over because TBB stores an enqueue
/// functor as `const F` and calls it through that const object, so the body would otherwise have
/// to be const-invocable. A body that moves out of its captures -- an eth_call moving a
/// transaction and a callback into the coroutine it starts -- is not, and cannot reasonably be
/// made so; behind the pointer the body keeps its non-const operator().
template <class Body>
    requires std::invocable<Body&> && std::is_void_v<std::invoke_result_t<Body&>> &&
             std::copy_constructible<std::decay_t<Body>>
void runInCallArena(Body&& body)
{
    auto holder = std::make_shared<std::decay_t<Body>>(std::forward<Body>(body));
    callArena().enqueue([holder] { (*holder)(); });
}

}  // namespace bcos::task::tbb
