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

#include "bcos-task/Task.h"
#include <coroutine>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace bcos::task
{
// Single-consumer, multi-producer async channel: producers push() from any thread, the single
// consumer pulls with co_await recv(). The consumer picks its own execution context: a parked
// recv() is always resumed through the Poster handed to the constructor, never inline on a
// producer's stack — a producer (e.g. a socket read loop) can never be stalled by consumer code.
//
// Backpressure: push() returns false when the channel is full (count or byte budget) or closed;
// a coroutine producer can instead co_await waitWritable(bytes) to park until a push of `bytes`
// (same measure as sizeOf) fits, turning "full" into producer-side backpressure rather than a
// drop. Like recv(), at most ONE waitWritable() waiter may be parked at a time, and its wake is
// also delivered through the poster. Space is only freed by the consumer popping, so only a pop
// can wake a parked producer. With more than one producer the wake does not reserve the space —
// another producer may consume it first — so multi-producer users must loop push/waitWritable.
// close() is idempotent (the FIRST close's exception sticks), wakes a parked recv() with that
// exception and a parked waitWritable() with false, and rejects later pushes; values already
// queued remain drainable — recv() throws only once the queue is empty, so teardown loses
// nothing that was accepted.
//
// Lifetime contract: close() the channel before destroying it and keep it alive until every
// parked recv()/waitWritable() has been woken (the wakes touch only the waiting coroutines'
// frames, so once close() has run the channel may be freed while the resumes are still in
// flight).
template <typename T>
class Channel
{
public:
    using Poster = std::function<void(std::function<void()>)>;
    using Ptr = std::shared_ptr<Channel>;

    // maxBytes == 0 disables the byte budget; sizeOf defaults to 1-per-item (pure count cap).
    Channel(Poster poster, std::size_t maxCount, std::size_t maxBytes = 0,
        std::function<std::size_t(const T&)> sizeOf = nullptr)
      : m_poster(std::move(poster)),
        m_maxCount(maxCount),
        m_maxBytes(maxBytes),
        m_sizeOf(std::move(sizeOf))
    {}

    Channel(const Channel&) = delete;
    Channel(Channel&&) = delete;
    Channel& operator=(const Channel&) = delete;
    Channel& operator=(Channel&&) = delete;
    ~Channel() = default;

    bool push(T value)
    {
        std::function<void()> wake;
        {
            std::lock_guard lock(m_mutex);
            if (m_closed)
            {
                return false;
            }
            if (m_waiter)
            {
                // Direct handoff: a parked waiter implies an empty queue, so FIFO holds.
                *m_waiter->value = std::move(value);
                wake = takeWaiterLocked();
            }
            else
            {
                auto bytes = m_sizeOf ? m_sizeOf(value) : 1;
                if (m_queue.size() >= m_maxCount ||
                    (m_maxBytes != 0 && m_queuedBytes + bytes > m_maxBytes))
                {
                    return false;
                }
                m_queuedBytes += bytes;
                m_queue.push_back(std::move(value));
            }
        }
        if (wake)
        {
            m_poster(std::move(wake));
        }
        return true;
    }

    void close(std::exception_ptr error = nullptr)
    {
        std::function<void()> wake;
        std::function<void()> wakeProducer;
        {
            std::lock_guard lock(m_mutex);
            if (m_closed)
            {
                return;
            }
            m_closed = true;
            m_closeError = error ? error :
                                     std::make_exception_ptr(std::runtime_error("channel closed"));
            if (m_waiter)
            {
                *m_waiter->error = m_closeError;
                wake = takeWaiterLocked();
            }
            if (m_writableWaiter)
            {
                // result stays false: no space is coming
                wakeProducer = takeWritableWaiterLocked();
            }
        }
        if (wake)
        {
            m_poster(std::move(wake));
        }
        if (wakeProducer)
        {
            m_poster(std::move(wakeProducer));
        }
    }

    bool closed() const
    {
        std::lock_guard lock(m_mutex);
        return m_closed;
    }

    std::size_t size() const
    {
        std::lock_guard lock(m_mutex);
        return m_queue.size();
    }

    task::Task<T> recv()
    {
        co_return co_await RecvAwaitable{*this};
    }

    // Producer side: suspends until a push of `bytes` (the same measure sizeOf reports for the
    // item about to be pushed; irrelevant when the channel has no byte budget) can succeed, then
    // returns true. Returns false — immediately or after the wake — once the channel is closed.
    // At most one waiter may be parked (a second parks over the first), mirroring recv().
    task::Task<bool> waitWritable(std::size_t bytes = 1)
    {
        co_return co_await WritableAwaitable{*this, bytes};
    }

private:
    // Posts `wake` (if set) during destruction. Declared BEFORE the lock guard in an await_suspend
    // it pairs with, locals are destroyed in reverse declaration order — the lock releases first,
    // then this posts. Posting under the lock would be a deadlock hazard: the Session poster runs
    // the wake INLINE once the host is gone, and a producer resumed there immediately calls back
    // into the channel (push/waitWritable), which would re-take the non-recursive m_mutex.
    struct PostOnExit
    {
        Poster& poster;
        std::function<void()>& wake;
        ~PostOnExit()
        {
            if (wake)
            {
                poster(std::move(wake));
            }
        }
    };
    // The value/error slots point into the awaiting coroutine's frame: the producer fills them
    // under the channel mutex, and await_resume reads ONLY the frame — never the channel — so a
    // close()ed channel may be destroyed while the posted resume is still in flight.
    struct WaiterSlot
    {
        std::coroutine_handle<> handle;
        std::optional<T>* value;
        std::exception_ptr* error;
    };

    // Producer-side counterpart of WaiterSlot: `bytes` is the size of the pending push (the
    // wake condition), `result` points into the producer's coroutine frame.
    struct WritableWaiterSlot
    {
        std::coroutine_handle<> handle;
        std::size_t bytes;
        bool* result;
    };

    struct RecvAwaitable
    {
        explicit RecvAwaitable(Channel& channel) : m_channel(channel) {}

        Channel& m_channel;
        std::optional<T> m_value;
        std::exception_ptr m_error;

        static bool await_ready() noexcept { return false; }
        bool await_suspend(std::coroutine_handle<> handle)
        {
            std::function<void()> wakeProducer;
            PostOnExit postOnExit{m_channel.m_poster, wakeProducer};
            std::lock_guard lock(m_channel.m_mutex);
            // The queue check must win over the closed check: queued values stay drainable after
            // close() (teardown loses nothing already accepted).
            if (!m_channel.m_queue.empty())
            {
                m_value.emplace(m_channel.popFrontLocked());
                // The pop may have freed the space a parked producer is waiting for.
                wakeProducer = m_channel.takeWritableWaiterIfSpaceLocked();
                return false;  // never suspended: continue on the consumer's own stack
            }
            if (m_channel.m_closed)
            {
                m_error = m_channel.m_closeError;
                return false;
            }
            m_channel.m_waiter = WaiterSlot{handle, &m_value, &m_error};
            return true;
        }
        T await_resume()
        {
            if (m_error)
            {
                std::rethrow_exception(m_error);
            }
            return std::move(*m_value);
        }
    };

    struct WritableAwaitable
    {
        WritableAwaitable(Channel& channel, std::size_t bytes) : m_channel(channel), m_bytes(bytes)
        {}

        Channel& m_channel;
        std::size_t m_bytes;
        bool m_result = false;

        static bool await_ready() noexcept { return false; }
        bool await_suspend(std::coroutine_handle<> handle)
        {
            std::lock_guard lock(m_channel.m_mutex);
            if (m_channel.m_closed)
            {
                return false;  // m_result stays false
            }
            if (m_channel.hasSpaceForLocked(m_bytes))
            {
                m_result = true;
                return false;  // never suspended: space already available
            }
            // The slot points into the awaiting coroutine's frame, exactly like WaiterSlot.
            m_channel.m_writableWaiter = WritableWaiterSlot{handle, m_bytes, &m_result};
            return true;
        }
        bool await_resume() const noexcept { return m_result; }
    };

    T popFrontLocked()
    {
        if (m_sizeOf)
        {
            m_queuedBytes -= m_sizeOf(m_queue.front());
        }
        T value = std::move(m_queue.front());
        m_queue.pop_front();
        return value;
    }

    // The mirror of the push() fullness checks: a push of `bytes` would be accepted right now.
    bool hasSpaceForLocked(std::size_t bytes) const
    {
        return m_queue.size() < m_maxCount &&
               (m_maxBytes == 0 || m_queuedBytes + bytes <= m_maxBytes);
    }

    std::function<void()> takeWaiterLocked()
    {
        auto handle = m_waiter->handle;
        m_waiter.reset();
        return [handle]() { handle.resume(); };
    }

    std::function<void()> takeWritableWaiterLocked()
    {
        auto handle = m_writableWaiter->handle;
        m_writableWaiter.reset();
        return [handle]() { handle.resume(); };
    }

    // Called by the consumer after a pop: hands the freed space to a parked producer if the
    // producer's pending push now fits, otherwise leaves it parked.
    std::function<void()> takeWritableWaiterIfSpaceLocked()
    {
        if (!m_writableWaiter || !hasSpaceForLocked(m_writableWaiter->bytes))
        {
            return nullptr;
        }
        *m_writableWaiter->result = true;
        return takeWritableWaiterLocked();
    }

    Poster m_poster;
    std::size_t m_maxCount;
    std::size_t m_maxBytes;
    std::function<std::size_t(const T&)> m_sizeOf;

    mutable std::mutex m_mutex;
    std::deque<T> m_queue;
    std::size_t m_queuedBytes = 0;
    bool m_closed = false;
    std::exception_ptr m_closeError;
    std::optional<WaiterSlot> m_waiter;
    std::optional<WritableWaiterSlot> m_writableWaiter;
};
}  // namespace bcos::task
