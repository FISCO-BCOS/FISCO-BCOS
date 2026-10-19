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
#include "bcos-utilities/Exceptions.h"
#include <boost/exception/diagnostic_information.hpp>
#include <boost/throw_exception.hpp>
#include <atomic>
#include <coroutine>
#include <exception>
#include <memory>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace bcos::task
{
DERIVE_BCOS_EXCEPTION(NoReturnValue);

namespace detail
{
// Registry for detached coroutine frames stranded at their final suspend point by an escaping
// unhandled_exception() (see AsyncTask). Such a frame cannot be reclaimed by its own
// coroutine: unhandled_exception() runs while the coroutine is still executing (where
// destroy() would be undefined), and its throwing only leaves the coroutine "considered
// suspended at the final suspend point" ([dcl.fct.def.coroutine]), so final_suspend's
// self-destroy never runs. The frame is registered here before the rethrow; the resumer that
// catches the escape (task::wait, GetResultAwaitable::publish) reclaims it, with thread exit
// as the safety net. Destroying the registered frame also destroys the frames its parameter
// copies own — parameter copies die with the frame, not with the unwinding body — so one
// registration reclaims the whole escaped chain (e.g. executeTask's `task` parameter owns the
// inner task's frame).
inline void reclaimEscapedFrames() noexcept;

struct EscapedFrameRegistry
{
    std::vector<std::coroutine_handle<>> frames;
    ~EscapedFrameRegistry() { reclaimEscapedFrames(); }
};

inline EscapedFrameRegistry& escapedFrameRegistry()
{
    thread_local EscapedFrameRegistry registry;
    return registry;
}

inline void registerEscapedFrame(std::coroutine_handle<> handle)
{
    escapedFrameRegistry().frames.push_back(handle);
}

// Destroy every registered frame. Each is suspended at its final suspend point, so this is a
// plain destroy(): it runs destructors but never resumes user code. The loop picks up frames
// registered by those destructors' side effects.
inline void reclaimEscapedFrames() noexcept
{
    auto& frames = escapedFrameRegistry().frames;
    while (!frames.empty())
    {
        auto pending = std::exchange(frames, {});
        for (auto handle : pending)
        {
            handle.destroy();
        }
    }
}
}  // namespace detail

template <class Promise>
struct FinalAwaitable
{
    constexpr bool await_ready() noexcept { return false; }
    constexpr std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> handle) noexcept
    {
        return handle.promise().m_continuation ?
                   static_cast<std::coroutine_handle<>>(handle.promise().m_continuation->handle) :
                   std::noop_coroutine();
    }
    constexpr void await_resume() noexcept {}
};

template <class VariantValue>
struct Continuation
{
    std::coroutine_handle<> handle;
    VariantValue value;
};

template <class TaskType, class PromiseImpl>
struct PromiseBase
{
    constexpr std::suspend_always initial_suspend() noexcept { return {}; }
    constexpr auto final_suspend() noexcept { return FinalAwaitable<PromiseImpl>{}; }
    TaskType get_return_object()
    {
        auto handle =
            std::coroutine_handle<PromiseImpl>::from_promise(*static_cast<PromiseImpl*>(this));
        return TaskType{handle};
    }
    void unhandled_exception()
    {
        auto exception = std::current_exception();
        if (m_continuation == nullptr)
        {
            std::rethrow_exception(exception);
        }
        m_continuation->value.template emplace<std::exception_ptr>(exception);
    }

    Continuation<typename TaskType::VariantValue>* m_continuation = nullptr;
};

template <class TaskType>
struct PromiseVoid : public PromiseBase<TaskType, PromiseVoid<TaskType>>
{
    constexpr void return_void() noexcept {}
};

template <class TaskType>
struct PromiseValue : public PromiseBase<TaskType, PromiseValue<TaskType>>
{
    void return_value(TaskType::Value value)
    {
        if (PromiseBase<TaskType, PromiseValue<TaskType>>::m_continuation)
        {
            PromiseBase<TaskType, PromiseValue<TaskType>>::m_continuation->value
                .template emplace<typename TaskType::Value>(std::move(value));
        }
    }
};

struct TaskPure
{
public:
    struct promise_type
    {
        constexpr std::suspend_always initial_suspend() noexcept { return {}; }
        constexpr std::suspend_never final_suspend() noexcept { return {}; }
        constexpr void return_void() noexcept {}
        void unhandled_exception() { std::rethrow_exception(std::current_exception()); }
        TaskPure get_return_object()
        {
            auto handle = std::coroutine_handle<promise_type>::from_promise(
                *static_cast<promise_type*>(this));
            return TaskPure{handle};
        }
    };

    explicit TaskPure(std::coroutine_handle<promise_type> handle) : m_handle(handle) {}
    TaskPure(const TaskPure&) = delete;
    TaskPure(TaskPure&&) noexcept = default;
    TaskPure& operator=(const TaskPure&) = delete;
    TaskPure& operator=(TaskPure&&) noexcept = default;
    ~TaskPure() noexcept = default;
    const std::coroutine_handle<promise_type>& getHandle() const { return m_handle; }

private:
    std::coroutine_handle<promise_type> m_handle;
};

template <typename... Resp>
struct GetResultAwaitable
{
    // Shared state, single-shot: one Result backs exactly one co_await and exactly one
    // complete(). There is no reset — reuse would silently redeliver the first completion.
    struct Result
    {
        enum class State : uint8_t
        {
            INIT,       // no waiter parked, the settle not yet published
            SUSPENDED,  // a waiter is parked; handle is valid
            WOKEN,      // a settler claimed the parked waiter's resume
            DONE,       // published with no parked waiter (settle-before-park, or the parked
                        // waiter deregistered); a later co_await takes the fast path
        };

        std::tuple<Resp...> data;
        // Suspend handshake only: DONE means the settle was published with nobody parked (so
        // await_ready() takes the fast path); a settle delivered to a parked waiter claims the
        // resume with WOKEN. Exactly-once is owned by `completed`, not here.
        std::atomic<State> state = State::INIT;
        std::coroutine_handle<> handle;
        // Exactly-once claim: the completer that flips this false->true owns the completion.
        std::atomic<bool> completed{false};
        // Why a settled wait delivered no value, when it was settled by cancellation rather than by
        // complete(). Null on the value path, and on a wait that was never settled at all.
        std::exception_ptr error;
    };

    explicit GetResultAwaitable(Result& result) : m_result(result) {}
    // A copy would deregister the same wait twice.
    GetResultAwaitable(const GetResultAwaitable&) = delete;
    GetResultAwaitable& operator=(const GetResultAwaitable&) = delete;

    // The awaitable lives in the waiting coroutine's frame and is destroyed with it. A frame
    // destroyed while still parked ABANDONS the wait: deregister, so a later settle only
    // records its outcome instead of resuming a dead frame. Losing the race against a settler
    // that already claimed the resume (state == WOKEN) leaves the settle owning the frame
    // until resume() returns — destroying the frame concurrently with an in-flight settle is
    // outside the contract.
    ~GetResultAwaitable() noexcept
    {
        if (!m_parked || m_resumed)
        {
            // never suspended, or the wait already ran to completion
            return;
        }
        auto expected = Result::State::SUSPENDED;
        (void)m_result.state.compare_exchange_strong(
            expected, Result::State::INIT, std::memory_order_acq_rel, std::memory_order_acquire);
    }

    bool await_ready() noexcept
    {
        // Fast path: the result was completed before the coroutine reached this co_await
        // (e.g. an ack arrived while the coroutine was still waiting on the write). Skip the
        // suspension entirely and let await_resume read the already-completed data.
        return m_result.state.load(std::memory_order_acquire) == Result::State::DONE;
    }
    bool await_suspend(std::coroutine_handle<> handle) noexcept
    {
        // Mark parked BEFORE publishing the handle: once state reads SUSPENDED, a settler may
        // resume (and finish, destroying this awaitable) before await_suspend() returns.
        m_parked = true;
        m_result.handle = handle;
        typename Result::State expected = Result::State::INIT;
        if (m_result.state.compare_exchange_strong(expected, Result::State::SUSPENDED,
                std::memory_order_acq_rel, std::memory_order_acquire))
        {
            return true;
        }
        // The completion won the race (state already DONE): do not suspend, await_resume reads
        // the data inline.
        return false;
    }
    std::tuple<Resp...> await_resume()
    {
        // Set before any throw below: once the wait is over, this awaitable's destruction must
        // never deregister, no matter how the delivered wait ends.
        m_resumed = true;
        // A settled-without-value wait (OneShot::cancel) reports its reason here, so teardown is
        // never mistaken for a real result. A wait that was never settled at all is a bug in the
        // producer; report it the same way instead of returning default-constructed values.
        if (m_result.error)
        {
            std::rethrow_exception(m_result.error);
        }
        if (!m_result.completed.load(std::memory_order_acquire))
        {
            BOOST_THROW_EXCEPTION(NoReturnValue{});
        }
        return std::move(m_result.data);
    }

    static void complete(Result& result, Resp... resp)
    {
        // Claim first: a second (or concurrent) complete() must not touch data or handle.
        auto claimed = false;
        if (!result.completed.compare_exchange_strong(claimed, true))
        {
            return;
        }
        // Write before publishing, so a reader that observes the settle also observes data.
        result.data = std::make_tuple(std::move(resp)...);
        publish(result);
    }

    // Publish the settled state: resume the parked awaiter, or record DONE when nobody is
    // parked (never parked, or the wait was abandoned) so the settle is not lost and a later
    // co_await takes the await_ready() fast path. resume() runs the awaiter INLINE, so an
    // awaiter that does not catch lets the exception propagate to the caller — deliberate, and
    // therefore NOT noexcept. An escaped detached (task::wait) chain leaves its top frame
    // stranded at its final suspend point; reclaim it before propagating.
    static void publish(Result& result)
    {
        auto expected = Result::State::INIT;
        if (result.state.compare_exchange_strong(expected, Result::State::DONE,
                std::memory_order_acq_rel, std::memory_order_acquire))
        {
            return;
        }
        // A waiter is parked (only this settler can observe WOKEN here, and DONE is its own
        // store above, so expected == SUSPENDED). Claim the resume; a waiter deregistering
        // concurrently flips SUSPENDED back to INIT, and only the claim's winner may touch the
        // handle.
        if (result.state.compare_exchange_strong(expected, Result::State::WOKEN,
                std::memory_order_acq_rel, std::memory_order_acquire))
        {
            try
            {
                result.handle.resume();
            }
            catch (...)
            {
                detail::reclaimEscapedFrames();
                throw;
            }
            return;
        }
        // The parked waiter deregistered mid-publish: record the settle, resume no one.
        result.state.store(Result::State::DONE, std::memory_order_release);
    }

private:
    Result& m_result;
    // Fast-path filters for the destructor; the state machine above stays the arbiter, so
    // stale reads here cannot cause a double resume or a lost settle.
    std::atomic<bool> m_parked{false};
    std::atomic<bool> m_resumed{false};
};

template <class TaskType>
struct Awaitable
{
    explicit Awaitable(std::coroutine_handle<typename TaskType::promise_type> handle)
    {
        m_continuation.handle = std::move(handle);
    }
    Awaitable(const Awaitable&) = delete;
    Awaitable(Awaitable&&) noexcept = default;
    Awaitable& operator=(const Awaitable&) = delete;
    Awaitable& operator=(Awaitable&&) noexcept = default;
    ~Awaitable() noexcept = default;

    bool await_ready() const noexcept
    {
        return !m_continuation.handle || m_continuation.handle.done();
    }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle)
    {
        // This transformation operation is safe
        // when Awaitable is constructed by coroutine_handle<typename TaskType::promise_type>
        // but unsafe when Awaitable is constructed by other ways
        auto nextHandle = std::coroutine_handle<typename TaskType::promise_type>::from_address(
            m_continuation.handle.address());
        m_continuation.handle = handle;
        nextHandle.promise().m_continuation = std::addressof(m_continuation);
        return nextHandle;
    }
    TaskType::Value await_resume()
    {
        if (auto* exception = std::get_if<std::exception_ptr>(std::addressof(m_continuation.value)))
        {
            std::rethrow_exception(*exception);
        }

        if constexpr (!std::is_void_v<typename TaskType::Value>)
        {
            if (auto* value =
                    std::get_if<typename TaskType::Value>(std::addressof(m_continuation.value)))
            {
                return std::move(*value);
            }
            BOOST_THROW_EXCEPTION(NoReturnValue{});
        }
    }

    Continuation<typename TaskType::VariantValue> m_continuation;
};

template <class ValueType>
    requires(!std::is_reference_v<ValueType>)
class [[nodiscard]] Task
{
public:
    using VariantValue = std::conditional_t<std::is_void_v<ValueType>,
        std::variant<std::monostate, std::exception_ptr>,
        std::variant<std::monostate, ValueType, std::exception_ptr>>;

    using Value = ValueType;
    using promise_type =
        std::conditional_t<std::is_same_v<ValueType, void>, PromiseVoid<Task>, PromiseValue<Task>>;

    Awaitable<Task> operator co_await() && { return Awaitable<Task>(m_handle); }
    explicit Task(std::coroutine_handle<promise_type> handle) : m_handle(handle) {}
    Task(const Task&) = delete;
    Task(Task&& task) noexcept : m_handle(task.m_handle) { task.m_handle = nullptr; }
    Task& operator=(const Task&) = delete;
    Task& operator=(Task&& task) noexcept
    {
        if (m_handle)
        {
            m_handle.destroy();
        }
        m_handle = task.m_handle;
        task.m_handle = nullptr;
        return *this;
    }
    ~Task() noexcept
    {
        if (m_handle)
        {
            m_handle.destroy();
        }
    }
    void start() { m_handle.resume(); }

private:
    std::coroutine_handle<promise_type> m_handle;
};

}  // namespace bcos::task
