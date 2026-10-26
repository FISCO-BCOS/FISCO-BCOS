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
 * @file WhenAll.h
 * @brief Fan a set of lazy tasks out together and await their collective completion — the
 *        structured replacement for the hand-rolled "shared atomic counter + last callback
 *        fires the real callback" fan-out protocol.
 */

#pragma once
#include "AsyncTask.h"
#include "FireAwaitable.h"
#include "OneShot.h"
#include "Task.h"
#include "Trait.h"
#include <atomic>
#include <cstddef>
#include <exception>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace bcos::task
{
// whenAll starts N lazy tasks together and suspends until ALL of them finish:
//
//     std::vector<task::Task<void>> writes;
//     for (auto& row : rows)
//     {
//         writes.emplace_back(storage2::writeOne(storage, row.key, std::move(row.value)));
//     }
//     co_await task::whenAll(std::move(writes));  // all settled, or first error rethrown
//
// Semantics, each one load-bearing:
//  - CONCURRENT: the tasks are started in order on the awaiting thread, and each one runs until
//    its first genuine suspension; completions (e.g. storage callbacks) may then land on ANY
//    thread and drive the rest of that child concurrently with its siblings — exactly like the
//    callback fan-out loop this replaces. A child whose completion lands INLINE (legacy storages
//    call back before the initiation returns) simply finishes before the next child starts, so
//    the fan-out degrades to a serial loop rather than nesting resumes. The awaiting coroutine
//    resumes on whichever thread finishes LAST.
//  - TOTAL: every child runs to completion, even after siblings fail. The children own in-flight
//    operations (a storage write cannot be cancelled), so whenAll never abandons them — it
//    rethrows only after the whole set has settled.
//  - DETERMINISTIC ERRORS: when several children fail, the exception rethrown is the first one
//    IN CHILD ORDER, not the first to land — replaying the same inputs reports the same error.
//  - ORDERED RESULTS: the value flavor delivers results in child order regardless of completion
//    order; a void child in the variadic flavor contributes a std::monostate tuple element.
//
// Lifetime: each child is owned by a detached runner (AsyncTask, the same owner task::wait
// uses), so a started fan-out always runs to completion even if the awaiting frame is destroyed
// while parked — the parking OneShot then records the settle through its abandoned-wait
// deregistration instead of resuming a dead frame.
namespace detail
{
// One child's outcome slot: exactly one runner writes it (value or exception) and the awaiting
// whenAll reads it after the countdown settles, so the slot itself needs no synchronization — the
// countdown's release/acquire chain publishes every slot to the reader before `done` is observed.
template <class Value>
struct WhenAllSlot
{
    std::variant<std::monostate, Value, std::exception_ptr> outcome;

    void setError(std::exception_ptr error) noexcept
    {
        outcome.template emplace<std::exception_ptr>(std::move(error));
    }
    bool hasError() const noexcept { return std::holds_alternative<std::exception_ptr>(outcome); }
    [[noreturn]] void rethrow() const
    {
        std::rethrow_exception(std::get<std::exception_ptr>(outcome));
    }
    Value take() { return std::move(std::get<Value>(outcome)); }
};

template <>
struct WhenAllSlot<void>
{
    std::exception_ptr error;

    void setError(std::exception_ptr e) noexcept { error = std::move(e); }
    bool hasError() const noexcept { return error != nullptr; }
    [[noreturn]] void rethrow() const { std::rethrow_exception(error); }
    std::monostate take() const noexcept { return {}; }
};

// A void child surfaces as std::monostate so a heterogeneous (variadic) fan-out keeps a
// well-formed tuple element per child.
template <class Value>
using WhenAllResult = std::conditional_t<std::is_void_v<Value>, std::monostate, Value>;

template <class Slots>
struct WhenAllState
{
    explicit WhenAllState(std::ptrdiff_t count) : pending(count) {}

    // Countdown settled by the runners: the LAST finisher publishes `done`, which resumes the
    // parked whenAll coroutine inline on the finisher's thread (OneShot's settle contract) — or
    // records the settle for the fast path when nobody is parked yet (all children completed
    // inline during startup) or any more (the wait was abandoned).
    std::atomic<std::ptrdiff_t> pending;
    OneShot<> done;
    Slots slots;
};

// The detached owner of one child task (the role executeTask plays for task::wait): the child is
// moved in and co_awaited, so its frame dies with this one. The runner NEVER throws — a child
// exception lands in the slot, and the final publish is exception-proofed because a consumer
// that does not catch lets the failure escape the resume (see OneShot's publish contract).
template <class Task, class Slot, class State>
AsyncTask whenAllRunner(Task task, Slot& slot, std::shared_ptr<State> state)
{
    try
    {
        if constexpr (std::is_void_v<AwaitableReturnType<Task>>)
        {
            co_await std::move(task);
        }
        else
        {
            slot.outcome.template emplace<AwaitableReturnType<Task>>(co_await std::move(task));
        }
    }
    catch (...)
    {
        slot.setError(std::current_exception());
    }

    if (state->pending.fetch_sub(1, std::memory_order_acq_rel) == 1)
    {
        try
        {
            state->done.complete();
        }
        catch (...)
        {
            logResumeException("whenAll's consumer threw out of the settle resume");
        }
    }
}
}  // namespace detail

// Vector flavor over Task<void> children: plain completion, or the first error in child order.
template <IsAwaitable Task>
    requires std::is_void_v<AwaitableReturnType<Task>>
task::Task<void> whenAll(std::vector<Task> tasks)
{
    if (tasks.empty())
    {
        co_return;
    }
    using State = detail::WhenAllState<std::vector<detail::WhenAllSlot<void>>>;
    auto state = std::make_shared<State>(static_cast<std::ptrdiff_t>(tasks.size()));
    state->slots.resize(tasks.size());
    for (size_t i = 0; i < tasks.size(); ++i)
    {
        detail::whenAllRunner(std::move(tasks[i]), state->slots[i], state).start();
    }
    co_await state->done;
    for (auto& slot : state->slots)
    {
        if (slot.hasError())
        {
            slot.rethrow();
        }
    }
}

// Vector flavor over value-returning children: the results in child order.
template <IsAwaitable Task>
    requires(!std::is_void_v<AwaitableReturnType<Task>>)
task::Task<std::vector<AwaitableReturnType<Task>>> whenAll(std::vector<Task> tasks)
{
    using Value = AwaitableReturnType<Task>;
    if (tasks.empty())
    {
        co_return {};
    }
    using State = detail::WhenAllState<std::vector<detail::WhenAllSlot<Value>>>;
    auto state = std::make_shared<State>(static_cast<std::ptrdiff_t>(tasks.size()));
    state->slots.resize(tasks.size());
    for (size_t i = 0; i < tasks.size(); ++i)
    {
        detail::whenAllRunner(std::move(tasks[i]), state->slots[i], state).start();
    }
    co_await state->done;
    for (auto& slot : state->slots)
    {
        if (slot.hasError())
        {
            slot.rethrow();
        }
    }
    std::vector<Value> values;
    values.reserve(state->slots.size());
    for (auto& slot : state->slots)
    {
        values.emplace_back(slot.take());
    }
    co_return values;
}

// Variadic flavor over heterogeneous tasks: one tuple element per child (void -> std::monostate).
template <IsAwaitable... Tasks>
    requires(sizeof...(Tasks) > 0)
task::Task<std::tuple<detail::WhenAllResult<AwaitableReturnType<Tasks>>...>> whenAll(
    Tasks&&... tasks)
{
    using State =
        detail::WhenAllState<std::tuple<detail::WhenAllSlot<AwaitableReturnType<Tasks>>...>>;
    auto state = std::make_shared<State>(static_cast<std::ptrdiff_t>(sizeof...(Tasks)));
    [&]<size_t... I>(std::index_sequence<I...>) {
        (detail::whenAllRunner(std::move(tasks), std::get<I>(state->slots), state).start(), ...);
    }(std::index_sequence_for<Tasks...>{});
    co_await state->done;
    // The first error IN CHILD ORDER wins, regardless of which child settled first.
    std::apply([](auto&... slot) { ((slot.hasError() ? (slot.rethrow(), 0) : 0), ...); },
        state->slots);
    co_return std::apply(
        [](auto&... slot) {
            return std::tuple<detail::WhenAllResult<AwaitableReturnType<Tasks>>...>{
                slot.take()...};
        },
        state->slots);
}

}  // namespace bcos::task
