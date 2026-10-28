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
 */

#pragma once

#include <bcos-task/Task.h>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bcos::gateway
{
// Byte-and-count admission budget with configurable multi-acquirer waits. Strict FIFO admits only
// the queue head; first-fit lets later requests use capacity that cannot satisfy earlier waiters.
// A ready waiter's charge is reserved until it resumes or is cancelled. Abandoned waits unregister.
class DispatchBudget
{
public:
    using Ptr = std::shared_ptr<DispatchBudget>;
    using Poster = std::function<void(std::function<void()>)>;

    enum class WaitPolicy
    {
        StrictFIFO,
        FirstFit
    };

    struct Usage
    {
        std::size_t usedBytes = 0;
        std::size_t usedCount = 0;
        std::size_t waiting = 0;
        std::uint64_t parks = 0;
        std::uint64_t parkTimeMs = 0;
        std::uint64_t longestWaitMs = 0;
    };

private:
    enum class WaitResult
    {
        Pending,
        Ready,
        Closed,
        Cancelled
    };

    struct Waiter
    {
        std::size_t bytes;
        std::chrono::steady_clock::time_point since;
        std::string owner;
        std::coroutine_handle<> handle;
        WaitResult result = WaitResult::Pending;
        bool resumeStarted = false;
        bool handled = false;
    };

    struct State
    {
        State(std::size_t _maxBytes, std::size_t _maxCount, Poster _poster, WaitPolicy _policy)
          : maxBytes(_maxBytes), maxCount(_maxCount), poster(std::move(_poster)), policy(_policy)
        {}

        std::size_t maxBytes;
        std::size_t maxCount;
        Poster poster;
        WaitPolicy policy;
        std::mutex mutex;
        std::size_t usedBytes = 0;
        std::size_t usedCount = 0;
        bool closed = false;
        std::deque<std::shared_ptr<Waiter>> waiters;
        std::shared_ptr<Waiter> ready;
        std::size_t waiting = 0;
        std::uint64_t parks = 0;
        std::uint64_t parkTimeMs = 0;
    };

    static bool fitsLocked(State const& _state, std::size_t _bytes)
    {
        if (_state.usedCount + (_state.ready ? 1 : 0) >= _state.maxCount ||
            _state.usedBytes > _state.maxBytes)
        {
            return false;
        }
        auto availableBytes = _state.maxBytes - _state.usedBytes;
        if (_state.ready)
        {
            if (_state.ready->bytes > availableBytes)
            {
                return false;
            }
            availableBytes -= _state.ready->bytes;
        }
        return _bytes <= availableBytes;
    }

    static void finishWaiterLocked(State& _state, Waiter const& _waiter)
    {
        --_state.waiting;
        _state.parkTimeMs +=
            static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - _waiter.since)
                    .count());
    }

    static bool removeWaiterLocked(State& _state, std::shared_ptr<Waiter> const& _waiter)
    {
        if (_state.ready == _waiter)
        {
            _state.ready.reset();
            finishWaiterLocked(_state, *_waiter);
            return true;
        }
        for (auto it = _state.waiters.begin(); it != _state.waiters.end(); ++it)
        {
            if (*it == _waiter)
            {
                _state.waiters.erase(it);
                finishWaiterLocked(_state, *_waiter);
                return true;
            }
        }
        return false;
    }

    static std::shared_ptr<Waiter> wakeNextIfFitsLocked(State& _state)
    {
        auto it = _state.waiters.begin();
        while (!_state.closed && !_state.ready && it != _state.waiters.end())
        {
            if ((*it)->result != WaitResult::Pending)
            {
                it = _state.waiters.erase(it);
                continue;
            }
            if (!fitsLocked(_state, (*it)->bytes))
            {
                if (_state.policy == WaitPolicy::StrictFIFO)
                {
                    return nullptr;
                }
                ++it;
                continue;
            }
            _state.ready = *it;
            _state.waiters.erase(it);
            _state.ready->result = WaitResult::Ready;
            return _state.ready;
        }
        return nullptr;
    }

    static void post(
        std::shared_ptr<State> const& _state, std::shared_ptr<Waiter> const& _waiter) noexcept
    {
        std::function<void()> wake = [_state, _waiter] {
            std::coroutine_handle<> handle;
            {
                std::lock_guard lock(_state->mutex);
                if (_waiter->resumeStarted || _waiter->handled)
                {
                    return;
                }
                _waiter->resumeStarted = true;
                handle = _waiter->handle;
            }
            if (handle)
            {
                try
                {
                    bcos::task::detail::resumeDetached(handle);
                }
                catch (...)
                {}
            }
        };
        try
        {
            _state->poster(wake);
        }
        catch (...)
        {
            try
            {
                wake();
            }
            catch (...)
            {}
        }
    }

    struct Awaitable
    {
        std::shared_ptr<State> state;
        std::shared_ptr<Waiter> waiter;

        bool await_ready() const
        {
            std::lock_guard lock(state->mutex);
            return waiter->result != WaitResult::Pending;
        }

        bool await_suspend(std::coroutine_handle<> handle)
        {
            std::lock_guard lock(state->mutex);
            if (waiter->result != WaitResult::Pending)
            {
                return false;
            }
            waiter->handle = handle;
            return true;
        }

        WaitResult await_resume()
        {
            std::lock_guard lock(state->mutex);
            waiter->handled = true;
            return waiter->result;
        }

        ~Awaitable()
        {
            std::shared_ptr<Waiter> next;
            {
                std::lock_guard lock(state->mutex);
                if (waiter->handled)
                {
                    return;
                }
                waiter->handled = true;
                removeWaiterLocked(*state, waiter);
                next = wakeNextIfFitsLocked(*state);
            }
            if (next)
            {
                post(state, next);
            }
        }
    };

    static void release(std::shared_ptr<State> const& _state, std::size_t _bytes) noexcept
    {
        std::shared_ptr<Waiter> wake;
        {
            std::lock_guard lock(_state->mutex);
            --_state->usedCount;
            _state->usedBytes -= _bytes;
            wake = wakeNextIfFitsLocked(*_state);
        }
        if (wake)
        {
            post(_state, wake);
        }
    }

public:
    class Permit
    {
    public:
        Permit() = default;
        Permit(std::shared_ptr<State> _state, std::size_t _bytes)
          : m_state(std::move(_state)), m_bytes(_bytes)
        {}
        Permit(const Permit&) = delete;
        Permit& operator=(const Permit&) = delete;
        Permit(Permit&& _other) noexcept
          : m_state(std::move(_other.m_state)), m_bytes(std::exchange(_other.m_bytes, 0))
        {}
        Permit& operator=(Permit&& _other) noexcept
        {
            if (this != &_other)
            {
                reset();
                m_state = std::move(_other.m_state);
                m_bytes = std::exchange(_other.m_bytes, 0);
            }
            return *this;
        }
        ~Permit() { reset(); }

        explicit operator bool() const { return static_cast<bool>(m_state); }
        void reset() noexcept
        {
            if (m_state)
            {
                release(m_state, m_bytes);
                m_state.reset();
                m_bytes = 0;
            }
        }

    private:
        std::shared_ptr<State> m_state;
        std::size_t m_bytes = 0;
    };

    DispatchBudget(std::size_t _maxBytes, std::size_t _maxCount, Poster _poster,
        WaitPolicy _policy = WaitPolicy::StrictFIFO)
      : m_state(std::make_shared<State>(_maxBytes, _maxCount, std::move(_poster), _policy))
    {}
    ~DispatchBudget() { close(); }

    task::Task<Permit> acquire(std::size_t _bytes, std::string _owner = {})
    {
        auto state = m_state;
        if (_bytes > state->maxBytes || state->maxCount == 0)
        {
            co_return Permit{};
        }
        std::shared_ptr<Waiter> waiter;
        while (true)
        {
            std::shared_ptr<Waiter> wake;
            bool acquired = false;
            bool failed = false;
            {
                std::lock_guard lock(state->mutex);
                if (waiter && waiter->result == WaitResult::Ready)
                {
                    removeWaiterLocked(*state, waiter);
                    ++state->usedCount;
                    state->usedBytes += _bytes;
                    acquired = true;
                    wake = wakeNextIfFitsLocked(*state);
                }
                else if (waiter && waiter->result != WaitResult::Pending)
                {
                    removeWaiterLocked(*state, waiter);
                    failed = true;
                    wake = wakeNextIfFitsLocked(*state);
                }
                else if (state->closed)
                {
                    co_return Permit{};
                }
                else if (!waiter &&
                         (state->policy == WaitPolicy::FirstFit ||
                             (state->waiters.empty() && !state->ready)) &&
                         fitsLocked(*state, _bytes))
                {
                    ++state->usedCount;
                    state->usedBytes += _bytes;
                    co_return Permit{state, _bytes};
                }
                else if (!waiter)
                {
                    waiter =
                        std::make_shared<Waiter>(Waiter{_bytes, std::chrono::steady_clock::now(),
                            std::move(_owner), {}, WaitResult::Pending, false, false});
                    state->waiters.push_back(waiter);
                    ++state->waiting;
                    ++state->parks;
                }
            }
            if (wake)
            {
                post(state, wake);
            }
            if (acquired)
            {
                co_return Permit{state, _bytes};
            }
            if (failed)
            {
                co_return Permit{};
            }
            (void)co_await Awaitable{state, waiter};
        }
    }

    bool closed() const
    {
        std::lock_guard lock(m_state->mutex);
        return m_state->closed;
    }

    std::size_t maxBytes() const { return m_state->maxBytes; }

    Usage usage() const
    {
        std::lock_guard lock(m_state->mutex);
        Usage result;
        result.usedBytes = m_state->usedBytes;
        result.usedCount = m_state->usedCount;
        result.waiting = m_state->waiting;
        result.parks = m_state->parks;
        result.parkTimeMs = m_state->parkTimeMs;
        if (m_state->ready)
        {
            result.longestWaitMs =
                static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - m_state->ready->since)
                        .count());
        }
        else if (!m_state->waiters.empty())
        {
            result.longestWaitMs =
                static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - m_state->waiters.front()->since)
                        .count());
        }
        return result;
    }

    void cancelWaiters(std::string_view _owner = {})
    {
        auto state = m_state;
        std::vector<std::shared_ptr<Waiter>> wakes;
        {
            std::lock_guard lock(state->mutex);
            for (auto it = state->waiters.begin(); it != state->waiters.end();)
            {
                auto waiter = *it;
                if (_owner.empty() || waiter->owner == _owner)
                {
                    waiter->result = WaitResult::Cancelled;
                    wakes.push_back(waiter);
                    it = state->waiters.erase(it);
                    finishWaiterLocked(*state, *waiter);
                }
                else
                {
                    ++it;
                }
            }
            if (state->ready && (_owner.empty() || state->ready->owner == _owner))
            {
                state->ready->result = WaitResult::Cancelled;
                wakes.push_back(state->ready);
                finishWaiterLocked(*state, *state->ready);
                state->ready.reset();
            }
            if (auto next = wakeNextIfFitsLocked(*state))
            {
                wakes.push_back(next);
            }
        }
        for (auto const& waiter : wakes)
        {
            post(state, waiter);
        }
    }

    void close()
    {
        auto state = m_state;
        std::vector<std::shared_ptr<Waiter>> wakes;
        {
            std::lock_guard lock(state->mutex);
            if (state->closed)
            {
                return;
            }
            state->closed = true;
            for (auto& waiter : state->waiters)
            {
                waiter->result = WaitResult::Closed;
                wakes.push_back(waiter);
                finishWaiterLocked(*state, *waiter);
            }
            state->waiters.clear();
            if (state->ready)
            {
                state->ready->result = WaitResult::Closed;
                wakes.push_back(state->ready);
                finishWaiterLocked(*state, *state->ready);
                state->ready.reset();
            }
        }
        for (auto const& waiter : wakes)
        {
            post(state, waiter);
        }
    }

private:
    std::shared_ptr<State> m_state;
};

// Field order releases the global charge before the peer charge during destruction.
struct DispatchPermit
{
    DispatchBudget::Permit peer;
    DispatchBudget::Permit global;
};

// Per-peer caps are smaller than the global cap in both dimensions, so one peer cannot exhaust
// either global dimension. Defaults require eight peers by bytes or twelve by count. At genuine
// saturation, a receive loop waiting for a global permit also pauses later packet types on its
// session until a permit is released or its wait is cancelled by disconnect/stop.
struct DispatchBudgetConfig
{
    constexpr static std::size_t DEFAULT_GLOBAL_IN_FLIGHT_BYTES = 768 * 1024 * 1024;
    constexpr static std::size_t DEFAULT_GLOBAL_IN_FLIGHT_COUNT = 3072;
    constexpr static std::size_t DEFAULT_PEER_IN_FLIGHT_BYTES = 96 * 1024 * 1024;
    constexpr static std::size_t DEFAULT_PEER_IN_FLIGHT_COUNT = 256;

    std::size_t globalBytes = DEFAULT_GLOBAL_IN_FLIGHT_BYTES;
    std::size_t globalCount = DEFAULT_GLOBAL_IN_FLIGHT_COUNT;
    std::size_t peerBytes = DEFAULT_PEER_IN_FLIGHT_BYTES;
    std::size_t peerCount = DEFAULT_PEER_IN_FLIGHT_COUNT;
    DispatchBudget::Poster poster;
};
}  // namespace bcos::gateway
