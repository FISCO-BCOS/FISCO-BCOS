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
// also delivered through the poster. A second parked waiter DISPLACES the first, which is woken
// with a failure (recv() throws, waitWritable() returns false) instead of being stranded. Space
// is only freed by the consumer popping, so only a pop can wake a parked producer. With more
// than one producer the wake does not reserve the space — another producer may consume it
// first — so multi-producer users must loop push/waitWritable.
// close() is idempotent (the FIRST close's exception sticks), wakes a parked recv() with that
// exception and a parked waitWritable() with false, and rejects later pushes; values already
// queued remain drainable — recv() throws only once the queue is empty, so teardown loses
// nothing that was accepted.
//
// Cancellation: `cancel(error)` is the same operation named for the case a caller actually has —
// an external teardown waking a parked pump so it does not have to wait out a socket timeout. It is
// thread-safe and idempotent, and queued values stay drainable ahead of the error.
//
// Lifetime: the queue, the close state and both waiter slots live in a shared State owned by
// the channel AND by every awaitable it handed out, so either side may be destroyed first.
// A coroutine frame destroyed while still parked (e.g. a dropped Task pump — teardown, not
// misuse) unregisters its slot from the awaitable's destructor: a later push()/close()/pop
// then just enqueues or closes instead of writing into or resuming the dead frame, and the
// abandoned wait is simply forgotten. The channel itself may also be destroyed while waiters
// are parked — the State is kept alive by the parked awaitables, but with the channel gone
// nothing will ever wake them, so they stay parked until their own frames are destroyed;
// callers that want parked pumps to FINISH must still close() first. The wakes touch only the
// waiting coroutines' frames (await_resume reads only its own slots, never the channel), so
// once close() has run the channel may be freed while the resumes are still in flight.
template <typename T>
class Channel
{
public:
    // Wake delivery for parked waiters. Must be callable CONCURRENTLY from any thread —
    // push(), close() and both awaitables' await_suspend() all post, from producer and
    // consumer threads alike — and RE-ENTRANT from inside a running wake: an inline poster
    // resumes the woken coroutine on the caller's stack, and that coroutine may immediately
    // re-enter the channel and post another wake.
    using Poster = std::function<void(std::function<void()>)>;
    using Ptr = std::shared_ptr<Channel>;

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

    bool push(T value)
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

    void close(std::exception_ptr error = nullptr)
    {
        std::function<void()> wake;
        std::function<void()> wakeProducer;
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
                wake = m_state->takeWaiterLocked();
            }
            if (m_state->writableWaiter)
            {
                // result stays false: no space is coming
                wakeProducer = m_state->takeWritableWaiterLocked();
            }
        }
        if (wake)
        {
            m_state->poster(std::move(wake));
        }
        if (wakeProducer)
        {
            m_state->poster(std::move(wakeProducer));
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
    // there could never be woken. At most one waiter may be parked: a second displaces the
    // first, which is woken with false, mirroring recv().
    auto waitWritable(std::size_t bytes = 1) { return WritableAwaitable{m_state, bytes}; }

private:
    // Everything a parked waiter can still touch after the Channel itself is gone: the
    // configuration, the queue, the close state and both waiter slots, behind one mutex. The
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
            auto handle = writableWaiter->handle;
            writableWaiter.reset();
            return [handle]() { handle.resume(); };
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

        std::function<void()> displaceWritableWaiterLocked()
        {
            if (!writableWaiter)
            {
                return nullptr;
            }
            // result stays false, as on close(): no space is coming for the displaced waiter.
            return takeWritableWaiterLocked();
        }

        // Called by the consumer after a pop: hands the freed space to a parked producer if the
        // producer's pending push now fits, otherwise leaves it parked.
        std::function<void()> takeWritableWaiterIfSpaceLocked()
        {
            if (!writableWaiter || !hasSpaceForLocked(writableWaiter->bytes))
            {
                return nullptr;
            }
            *writableWaiter->result = true;
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
        std::optional<WritableWaiterSlot> writableWaiter;
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

        // Not movable, like RecvAwaitable: the waiter slot points at THIS object's m_result.
        WritableAwaitable(const WritableAwaitable&) = delete;
        WritableAwaitable& operator=(const WritableAwaitable&) = delete;
        WritableAwaitable& operator=(WritableAwaitable&&) = delete;

        // See RecvAwaitable's move constructor: only moved pre-registration in practice; the
        // registered branch re-points the slot at the new address.
        WritableAwaitable(WritableAwaitable&& other)
          : m_state(std::move(other.m_state)),
            m_bytes(other.m_bytes),
            m_result(other.m_result),
            m_registered(other.m_registered)
        {
            if (m_registered)
            {
                std::lock_guard lock(m_state->mutex);
                if (m_state->writableWaiter && m_state->writableWaiter->result == &other.m_result)
                {
                    m_state->writableWaiter->result = &m_result;
                }
                other.m_registered = false;
            }
        }

        // See ~RecvAwaitable: a frame destroyed while parked in waitWritable() unregisters its
        // slot here, so a later pop or close() cannot write into or resume the dead frame.
        ~WritableAwaitable()
        {
            if (!m_registered)
            {
                return;
            }
            std::lock_guard lock(m_state->mutex);
            if (m_state->writableWaiter && m_state->writableWaiter->result == &m_result)
            {
                m_state->writableWaiter.reset();
            }
        }

        std::shared_ptr<State> m_state;
        std::size_t m_bytes;
        bool m_result = false;
        bool m_registered = false;

        static bool await_ready() noexcept { return false; }
        bool await_suspend(std::coroutine_handle<> handle)
        {
            // Same invariant as RecvAwaitable: the displaced waiter's wake is posted while OUR
            // OWN slot is unregistered, and nothing is posted after registering it.
            while (true)
            {
                std::function<void()> wake;
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
                    if (m_state->writableWaiter)
                    {
                        // One waiter at a time: a second parked waitWritable() displaces the
                        // first, which is woken with false rather than stranded. Loop and
                        // re-check AFTER the post: a pop or close() may have landed while the
                        // displaced waiter's wake ran.
                        wake = m_state->displaceWritableWaiterLocked();
                    }
                    else
                    {
                        m_state->writableWaiter =
                            typename State::WritableWaiterSlot{handle, m_bytes, &m_result};
                        m_registered = true;
                        return true;
                    }
                }
                // Posted outside the lock, as in push()/close(): the poster can run a wake
                // INLINE, and a producer resumed there calls back into the channel.
                if (wake)
                {
                    m_state->poster(std::move(wake));
                }
            }
        }
        bool await_resume() const noexcept { return m_result; }
    };

    std::shared_ptr<State> m_state;
};
}  // namespace bcos::task
