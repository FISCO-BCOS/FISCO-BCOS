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
#include <atomic>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace bcos::task
{
// Single-consumer, multi-producer async channel: producers push() from any thread, the single
// consumer pulls with co_await recv(). The consumer picks its own execution context: a parked
// recv() is always resumed through the Poster handed to the constructor, never inline on a
// producer's stack — a producer (e.g. a socket read loop) can never be stalled by consumer code.
//
// Backpressure: push() returns false when the channel is full (count or byte budget) or closed —
// leaving the value INTACT, so a producer can park and retry the same item: a coroutine producer
// co_awaits waitWritable(bytes) until a push of `bytes` (same measure as sizeOf) fits, turning
// "full" into producer-side backpressure rather than a drop. Producers park in a FIFO queue and
// their wakes are also delivered through the poster. A pop wakes the first parked producer whose
// item fits; recv() remains single-waiter, with a second recv() displacing the first. A producer
// wake does not reserve the space — another producer may consume it first — so multi-producer
// users must loop push/waitWritable (safe because a failed push keeps the value).
// close() is idempotent (the FIRST close's exception sticks), wakes a parked recv() with that
// exception and every parked waitWritable() with false, and rejects later pushes; values already
// queued remain drainable — recv() throws only once the queue is empty, so teardown loses
// nothing that was accepted.
//
// Cancellation: `cancel(error)` is the same operation named for the case a caller actually has —
// an external teardown waking a parked pump so it does not have to wait out a socket timeout. It is
// thread-safe and idempotent, and queued values stay drainable ahead of the error.
//
// Lifetime: the queue, the close state and all waiter state live in a shared State owned by
// the channel AND by every awaitable it handed out, so either side may be destroyed first.
// A coroutine frame destroyed while still parked (e.g. a dropped Task pump — teardown, not
// misuse) unregisters its slot or queue entry from the awaitable's destructor: a later
// push()/close()/pop then just enqueues or closes instead of writing into or resuming the dead
// frame, and the abandoned wait is simply forgotten. The channel itself may also be destroyed while
// waiters are parked — the State is kept alive by the parked awaitables, but with the channel gone
// nothing will ever wake them, so they stay parked until their own frames are destroyed;
// callers that want parked pumps to FINISH must still close() first. The wakes touch only the
// waiting coroutines' frames (await_resume reads only its own slots, never the channel), so
// once close() has run the channel may be freed while the resumes are still in flight.
template <typename T>
class Channel
{
public:
    // Wake delivery for parked waiters. Must be callable CONCURRENTLY from any thread —
    // push(), close() and RecvAwaitable::await_suspend() all post, from producer and consumer
    // threads alike — and RE-ENTRANT from inside a running wake: an inline poster resumes the
    // woken coroutine on the caller's stack, and that coroutine may immediately re-enter the
    // channel and post another wake.
    using Poster = std::function<void(std::function<void()>)>;
    using Ptr = std::shared_ptr<Channel>;

    struct Stats
    {
        std::size_t count = 0;
        std::size_t bytes = 0;
        std::size_t producerWaiters = 0;
        std::uint64_t producerParks = 0;
    };

    // maxBytes == 0 disables the byte budget; sizeOf defaults to 1-per-item (pure count cap).
    Channel(Poster poster, std::size_t maxCount, std::size_t maxBytes = 0,
        std::function<std::size_t(const T&)> sizeOf = nullptr)
      : m_state(std::make_shared<State>(std::move(poster), maxCount, maxBytes, std::move(sizeOf)))
    {}

    Channel(const Channel&) = delete;
    Channel(Channel&&) = delete;
    Channel& operator=(const Channel&) = delete;
    Channel& operator=(Channel&&) = delete;
    ~Channel() = default;

    // On false (full or closed) `value` is left intact: only a successful push moves from it, so
    // a producer that lost the waitWritable race can retry the same item after parking again.
    bool push(T& value)
    {
        std::function<void()> wake;
        {
            std::lock_guard lock(m_state->mutex);
            if (m_state->closed)
            {
                return false;
            }
            if (m_state->waiter)
            {
                // Direct handoff: a parked waiter implies an empty queue, so FIFO holds.
                *m_state->waiter->value = std::move(value);
                wake = m_state->takeWaiterLocked();
            }
            else
            {
                auto bytes = m_state->measure(value);
                if (m_state->queue.size() >= m_state->maxCount ||
                    (m_state->maxBytes != 0 && m_state->queuedBytes + bytes > m_state->maxBytes))
                {
                    return false;
                }
                m_state->queuedBytes += bytes;
                m_state->queue.push_back(std::move(value));
            }
        }
        if (wake)
        {
            m_state->poster(std::move(wake));
        }
        return true;
    }

    // Rvalue convenience: same semantics — on false the argument is NOT consumed.
    bool push(T&& value) { return push(value); }

    void close(std::exception_ptr error = nullptr)
    {
        std::vector<std::function<void()>> wakes;
        std::vector<std::function<void()>> producerWakes;
        {
            std::lock_guard lock(m_state->mutex);
            if (m_state->closed)
            {
                return;
            }
            m_state->closed = true;
            m_state->closeError =
                error ? error : std::make_exception_ptr(std::runtime_error("channel closed"));
            if (m_state->waiter)
            {
                *m_state->waiter->error = m_state->closeError;
                wakes.emplace_back(m_state->takeWaiterLocked());
            }
            // Results stay false: no space is coming for any parked producer.
            producerWakes = m_state->takeAllWritableWaitersLocked();
        }
        for (auto& wake : producerWakes)
        {
            wakes.emplace_back(std::move(wake));
        }
        if (wakes.empty())
        {
            return;
        }

        // One idempotent delivery ensures a poster failure cannot strand the remaining waiters or
        // resume one twice when the fallback runs. A waiter exception must not block the others.
        auto sharedWakes = std::make_shared<std::vector<std::function<void()>>>(std::move(wakes));
        auto started = std::make_shared<std::atomic_bool>(false);
        std::function<void()> wakeAll = [sharedWakes, started] {
            if (started->exchange(true))
            {
                return;
            }
            for (auto& wake : *sharedWakes)
            {
                try
                {
                    wake();
                }
                catch (...)
                {
                    detail::reclaimEscapedFrames();
                }
            }
        };
        try
        {
            m_state->poster(wakeAll);
        }
        catch (...)
        {
            wakeAll();
        }
    }

    bool closed() const
    {
        std::lock_guard lock(m_state->mutex);
        return m_state->closed;
    }

    // CANCEL a parked consumer / producer from any thread, waking it with `error` (or the generic
    // "channel closed" exception). This is `close()` under the name that says what a caller
    // actually wants: an EXTERNAL cancellation — a shutdown path, a session teardown, or a peer
    // thread that must not let a parked pump wait out a socket timeout. It exists as its own name
    // because the tree has repeatedly hand-rolled that wake-up (PostingWorker, teardown executors,
    // an "abort" timer racing a read) and the resulting code read as if a reader/writer was closing
    // its own channel.
    //
    // Semantics are exactly close()'s, which are already thread-safe (the State mutex) and
    // idempotent (the FIRST error sticks): parked recv() rethrows `error`, parked waitWritable()
    // returns false, and values already queued stay drainable ahead of the error — cancellation
    // never loses work that was already accepted. Calling cancel() twice, or after a close(), is
    // a no-op.
    void cancel(std::exception_ptr error) { close(std::move(error)); }

    std::size_t size() const
    {
        std::lock_guard lock(m_state->mutex);
        return m_state->queue.size();
    }

    Stats stats() const
    {
        std::lock_guard lock(m_state->mutex);
        return Stats{m_state->queue.size(), m_state->queuedBytes, m_state->writableWaiters.size(),
            m_state->producerParks};
    }

    // recv() and waitWritable() return the bare awaitable, not a Task: awaiting one allocates
    // nothing beyond the State the channel already owns (a Task wrapper would cost one coroutine
    // frame per message — the promise has no custom operator new, and the poster round-trip rules
    // HALO out). The awaitable is self-contained — it holds a shared_ptr to the channel's State
    // plus its own result slots — so any coroutine may co_await it directly, and it stays valid
    // even if the channel itself is destroyed first (see the lifetime contract above).
    auto recv() { return RecvAwaitable{m_state}; }

    // Producer side: suspends until a push of `bytes` (the same measure sizeOf reports for the
    // item about to be pushed; irrelevant when the channel has no byte budget) can succeed, then
    // returns true. Returns false once the channel is closed, or immediately when such a push
    // can never fit (a zero maxCount, or `bytes` larger than the whole byte budget) — parking
    // there could never be woken. Any number of producers may park; they wait in FIFO order and
    // are woken through the poster as consumer pops make room.
    auto waitWritable(std::size_t bytes = 1) { return WritableAwaitable{m_state, bytes}; }

private:
    // Everything a parked waiter can still touch after the Channel itself is gone: the
    // configuration, the queue, the close state and all waiter state, behind one mutex. The
    // shared ownership (channel + every live awaitable) is what makes "either side may be
    // destroyed first" safe — see the lifetime contract at the top of the file.
    struct State
    {
        State(Poster poster, std::size_t maxCount, std::size_t maxBytes,
            std::function<std::size_t(const T&)> sizeOf)
          : poster(std::move(poster)),
            maxCount(maxCount),
            maxBytes(maxBytes),
            sizeOf(std::move(sizeOf))
        {}

        // The value/error slots point into the awaiting coroutine's frame: the producer fills
        // them under the mutex, and await_resume reads ONLY the frame — never the channel — so
        // a close()ed channel may be destroyed while the posted resume is still in flight. The
        // awaitable unregisters its slot from its destructor, so a slot never outlives the
        // frame it points into.
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

        // sizeOf defaults to 1-per-item (see the constructor): both the push() accounting and
        // the pop-side release must go through this so the two can never disagree.
        std::size_t measure(const T& value) const { return sizeOf ? sizeOf(value) : 1; }

        T popFrontLocked()
        {
            queuedBytes -= measure(queue.front());
            T value = std::move(queue.front());
            queue.pop_front();
            return value;
        }

        // The mirror of the push() fullness checks: a push of `bytes` would be accepted right
        // now.
        bool hasSpaceForLocked(std::size_t bytes) const
        {
            return queue.size() < maxCount && (maxBytes == 0 || queuedBytes + bytes <= maxBytes);
        }

        // Whether ANY state of the queue could accept a push of `bytes`: a zero maxCount rejects
        // every push, and an item bigger than the whole byte budget fits no matter how much is
        // drained. A waitWritable() that fails this fails fast instead of parking until close().
        bool canEverFitLocked(std::size_t bytes) const
        {
            return maxCount != 0 && (maxBytes == 0 || bytes <= maxBytes);
        }

        std::function<void()> takeWaiterLocked()
        {
            auto handle = waiter->handle;
            waiter.reset();
            return [handle]() { handle.resume(); };
        }

        std::function<void()> takeWritableWaiterLocked()
        {
            auto handle = writableWaiters.front().handle;
            writableWaiters.pop_front();
            return [handle]() { handle.resume(); };
        }

        std::vector<std::function<void()>> takeAllWritableWaitersLocked()
        {
            std::vector<std::function<void()>> wakes;
            wakes.reserve(writableWaiters.size());
            while (!writableWaiters.empty())
            {
                wakes.emplace_back(takeWritableWaiterLocked());
            }
            return wakes;
        }

        // One waiter at a time: before parking over an already-parked waiter, the new waiter
        // wakes the displaced one with a failure, so its coroutine is never stranded (a dropped
        // handle would leak the frame and lose the wake forever).
        std::function<void()> displaceWaiterLocked()
        {
            if (!waiter)
            {
                return nullptr;
            }
            *waiter->error = std::make_exception_ptr(
                std::runtime_error("channel waiter displaced by a second recv()"));
            return takeWaiterLocked();
        }

        // Called by the consumer after a pop: hands the freed space to the first parked producer
        // whose pending push now fits, otherwise leaves the FIFO queue parked.
        std::function<void()> takeWritableWaiterIfSpaceLocked()
        {
            if (writableWaiters.empty() || !hasSpaceForLocked(writableWaiters.front().bytes))
            {
                return nullptr;
            }
            *writableWaiters.front().result = true;
            return takeWritableWaiterLocked();
        }

        Poster poster;
        std::size_t maxCount;
        std::size_t maxBytes;
        std::function<std::size_t(const T&)> sizeOf;

        mutable std::mutex mutex;
        std::deque<T> queue;
        std::size_t queuedBytes = 0;
        bool closed = false;
        std::exception_ptr closeError;
        std::optional<WaiterSlot> waiter;
        std::deque<WritableWaiterSlot> writableWaiters;
        std::uint64_t producerParks = 0;
    };

    struct RecvAwaitable
    {
        explicit RecvAwaitable(std::shared_ptr<State> state) : m_state(std::move(state)) {}

        RecvAwaitable(const RecvAwaitable&) = delete;
        RecvAwaitable& operator=(const RecvAwaitable&) = delete;
        RecvAwaitable& operator=(RecvAwaitable&&) = delete;

        // Trait.h's getAwaitable returns the not-yet-awaited awaitable by value, so a move
        // constructor must exist; in practice it only ever moves PRE-registration. The
        // registered branch re-points the slot at the new address anyway, so the invariant
        // "the slot points at the live awaitable" holds either way. (Moving while another
        // thread is waking the waiter is the caller's data race, as for any object.)
        RecvAwaitable(RecvAwaitable&& other)
          : m_state(std::move(other.m_state)),
            m_value(std::move(other.m_value)),
            m_error(std::move(other.m_error)),
            m_registered(other.m_registered)
        {
            if (m_registered)
            {
                std::lock_guard lock(m_state->mutex);
                if (m_state->waiter && m_state->waiter->value == &other.m_value)
                {
                    m_state->waiter->value = &m_value;
                    m_state->waiter->error = &m_error;
                }
                other.m_registered = false;
            }
        }

        // Runs when the awaiting frame is destroyed while still parked (e.g. a dropped Task
        // pump): unregister, so a later push()/close() sees no waiter and cannot write into or
        // resume the dead frame. If the slot was already taken by a wake it no longer points
        // here, and there is nothing to do.
        ~RecvAwaitable()
        {
            if (!m_registered)
            {
                return;
            }
            std::lock_guard lock(m_state->mutex);
            if (m_state->waiter && m_state->waiter->value == &m_value)
            {
                m_state->waiter.reset();
            }
        }

        std::shared_ptr<State> m_state;
        std::optional<T> m_value;
        std::exception_ptr m_error;
        bool m_registered = false;

        static bool await_ready() noexcept { return false; }
        bool await_suspend(std::coroutine_handle<> handle)
        {
            // INVARIANT: every wake is posted while OUR OWN slot is unregistered, and nothing
            // is posted between registering the slot and returning true. An inline poster
            // resumes the woken coroutine on THIS stack; if its await_resume throws, the
            // exception escapes await_suspend and this awaitable is destroyed together with
            // the suspending coroutine — safe only while no slot points into it. (A slot
            // registered before the post would dangle, and a coroutine resumed inline could
            // also displace a waiter that has not finished parking yet.)
            while (true)
            {
                std::function<void()> wake;
                {
                    std::lock_guard lock(m_state->mutex);
                    // The queue check must win over the closed check: queued values stay
                    // drainable after close() (teardown loses nothing already accepted).
                    if (!m_state->queue.empty())
                    {
                        m_value.emplace(m_state->popFrontLocked());
                        // The pop may have freed the space a parked producer is waiting for.
                        wake = m_state->takeWritableWaiterIfSpaceLocked();
                    }
                    else if (m_state->closed)
                    {
                        m_error = m_state->closeError;
                    }
                    else if (m_state->waiter)
                    {
                        // One waiter at a time: a second parked recv() displaces the first,
                        // which is woken with an exception rather than stranded. Loop and
                        // re-check AFTER the post: a push() or close() may have landed while
                        // the displaced waiter's wake ran.
                        wake = m_state->displaceWaiterLocked();
                    }
                    else
                    {
                        m_state->waiter = typename State::WaiterSlot{handle, &m_value, &m_error};
                        m_registered = true;
                        return true;
                    }
                }
                // Posted outside the lock, as in push()/close(): the Session poster can run a
                // wake INLINE once the host is gone, and a coroutine resumed there immediately
                // calls back into the channel, which would re-take the non-recursive mutex.
                if (wake)
                {
                    m_state->poster(std::move(wake));
                }
                if (m_value || m_error)
                {
                    return false;  // never suspended: value or close error already in hand
                }
            }
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
        WritableAwaitable(std::shared_ptr<State> state, std::size_t bytes)
          : m_state(std::move(state)), m_bytes(bytes)
        {}

        // Not copyable: a registered queue entry points at THIS object's m_result.
        WritableAwaitable(const WritableAwaitable&) = delete;
        WritableAwaitable& operator=(const WritableAwaitable&) = delete;
        WritableAwaitable& operator=(WritableAwaitable&&) = delete;

        // See RecvAwaitable's move constructor: only moved pre-registration in practice; the
        // registered branch re-points the queue entry at the new address.
        WritableAwaitable(WritableAwaitable&& other)
          : m_state(std::move(other.m_state)),
            m_bytes(other.m_bytes),
            m_result(other.m_result),
            m_registered(other.m_registered)
        {
            if (m_registered)
            {
                std::lock_guard lock(m_state->mutex);
                for (auto& waiter : m_state->writableWaiters)
                {
                    if (waiter.result == &other.m_result)
                    {
                        waiter.result = &m_result;
                        break;
                    }
                }
                other.m_registered = false;
            }
        }

        // See ~RecvAwaitable: a frame destroyed while parked in waitWritable() unregisters its
        // queue entry here, so a later pop or close() cannot write into or resume the dead frame.
        ~WritableAwaitable()
        {
            if (!m_registered)
            {
                return;
            }
            std::lock_guard lock(m_state->mutex);
            for (auto it = m_state->writableWaiters.begin(); it != m_state->writableWaiters.end();
                ++it)
            {
                if (it->result == &m_result)
                {
                    m_state->writableWaiters.erase(it);
                    break;
                }
            }
        }

        std::shared_ptr<State> m_state;
        std::size_t m_bytes;
        bool m_result = false;
        bool m_registered = false;

        static bool await_ready() noexcept { return false; }
        bool await_suspend(std::coroutine_handle<> handle)
        {
            std::lock_guard lock(m_state->mutex);
            if (m_state->closed)
            {
                return false;  // m_result stays false
            }
            if (m_state->hasSpaceForLocked(m_bytes))
            {
                m_result = true;
                return false;  // never suspended: space already available
            }
            if (!m_state->canEverFitLocked(m_bytes))
            {
                // m_result stays false: this push can never fit, so fail fast instead
                // of parking until close().
                return false;
            }
            m_state->writableWaiters.push_back(
                typename State::WritableWaiterSlot{handle, m_bytes, &m_result});
            ++m_state->producerParks;
            m_registered = true;
            return true;
        }
        bool await_resume() const noexcept { return m_result; }
    };

    std::shared_ptr<State> m_state;
};
}  // namespace bcos::task
