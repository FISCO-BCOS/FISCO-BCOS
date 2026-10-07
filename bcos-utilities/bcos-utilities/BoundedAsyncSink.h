/**
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
 *
 * @brief: asynchronous sink frontend with a lock-free bounded MPSC queue.
 *  Unlike boost::log::sinks::asynchronous_sink, the queue is preallocated (no
 *  per-record queue node allocation or queue mutex on the logging thread);
 *  when the queue is full the record is dropped instead of blocking producers.
 *  The condition variable is only touched when the consumer goes idle, so a
 *  busy producer never pays a futex. The boost backend (rotation, file
 *  collector, formatter, FIB-184 exception suppression) is kept unchanged.
 *
 *  boost::lockfree::queue requires trivially copyable/destructible elements,
 *  which boost::log::record_view is not, so the queue carries uint32_t indices
 *  into m_slots, a fixed array of CapacityV views owned by the sink itself
 *  (the same hand-off boost uses internally: copying a view is a refcount
 *  bump, the record data stays owned by the shared implementation).
 *
 * @file: BoundedAsyncSink.h
 */
#pragma once

#include <boost/lockfree/policies.hpp>
#include <boost/lockfree/queue.hpp>
#include <boost/log/core/record.hpp>
#include <boost/log/core/record_view.hpp>
#include <boost/log/sinks/basic_sink_frontend.hpp>
#include <boost/make_shared.hpp>
#include <boost/smart_ptr/shared_ptr.hpp>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>
#include <utility>

namespace bcos
{
template <typename BackendT, std::size_t CapacityV = 32768>
class BoundedAsyncSink : public boost::log::sinks::basic_formatting_sink_frontend<char>
{
    using base_type = boost::log::sinks::basic_formatting_sink_frontend<char>;

public:
    using backend_type = BackendT;

    // Mimics asynchronous_sink::locked_backend(): serializes backend
    // configuration against the feeding thread.
    class LockedBackend
    {
    public:
        LockedBackend(boost::shared_ptr<BackendT> const& _backend, std::mutex& _mutex)
          : m_lock(_mutex), m_backend(_backend)
        {}
        BackendT* operator->() const { return m_backend.get(); }
        BackendT& operator*() const { return *m_backend; }

    private:
        std::unique_lock<std::mutex> m_lock;
        boost::shared_ptr<BackendT> m_backend;
    };

    BoundedAsyncSink() : BoundedAsyncSink(boost::make_shared<BackendT>()) {}
    explicit BoundedAsyncSink(boost::shared_ptr<BackendT> _backend)
      : base_type(true), m_backend(std::move(_backend))
    {
        for (std::uint32_t i = 0; i < CapacityV; ++i)
        {
            m_freeSlots.push(i);
        }
        m_feedingThread = std::thread([this]() { feedLoop(); });
    }
    BoundedAsyncSink(BoundedAsyncSink const&) = delete;
    BoundedAsyncSink& operator=(BoundedAsyncSink const&) = delete;
    ~BoundedAsyncSink() override { stop(); }

    LockedBackend locked_backend() { return LockedBackend(m_backend, m_backendMutex); }

    // Called on the logging thread after will_consume() accepted the record.
    // The record is copied into a pooled slot; when the queue is full the
    // record is dropped (drop-on-overflow) instead of blocking the producer.
    void consume(boost::log::record_view const& _rec) override
    {
        if (m_flushRequested.load(std::memory_order_acquire))
        {
            waitForFlush();
        }
        std::uint32_t slot = 0;
        if (!m_freeSlots.pop(slot))
        {
            return;  // out of slots: drop
        }
        m_slots[slot] = _rec;
        if (m_queue.push(slot))
        {
            wakeConsumer();
        }
        else
        {
            m_freeSlots.push(slot);  // queue full: drop the record
        }
    }

    // Feeds every buffered record to the backend, then flushes it.
    void flush() override
    {
        if (m_stop.load(std::memory_order_acquire))
        {
            // feeding thread already joined: drain inline
            std::uint32_t slot = 0;
            while (m_queue.pop(slot))
            {
                feedOne(slot);
            }
            base_type::flush_backend(m_backendMutex, *m_backend);
            return;
        }
        m_flushRequested.store(true, std::memory_order_release);
        wakeConsumer();
        waitForFlush();
    }

    // Stops the feeding thread; buffered records are drained before it exits.
    void stop()
    {
        bool expected = false;
        if (!m_stop.compare_exchange_strong(expected, true))
        {
            return;  // already stopped
        }
        wakeConsumer();
        if (m_feedingThread.joinable())
        {
            m_feedingThread.join();
        }
    }

private:
    void wakeConsumer()
    {
        // Cheap check first: while the consumer is busy this is a single
        // acquire load with no fence on the producer hot path. The seq_cst
        // load pairs with the seq_cst m_sleeping.store(true) in feedLoop the
        // same way an exchange would: reading false orders us before that
        // store, so the consumer's predicate (evaluated afterwards, under
        // the wait mutex) still observes the state this notifier set.
        if (!m_sleeping.load(std::memory_order_acquire) || !m_sleeping.exchange(false))
        {
            return;
        }
        // Notify under the mutex: the consumer evaluates the wait predicate
        // while holding m_waitMutex, so locking here serializes against it
        // and the notification can never land between the predicate check
        // and the futex sleep (missed wakeup).
        std::lock_guard<std::mutex> lock(m_waitMutex);
        m_cv.notify_one();
    }
    void waitForFlush()
    {
        std::unique_lock<std::mutex> lock(m_flushMutex);
        m_flushDone.wait(
            lock, [this] { return !m_flushRequested.load(std::memory_order_acquire); });
    }
    void feedOne(std::uint32_t _slot)
    {
        try
        {
            base_type::feed_record(m_slots[_slot], m_backendMutex, *m_backend);
        }
        catch (...)
        {
            // FIB-184: a backend exception must never kill the feeding thread
            // even if no exception handler is installed.
        }
        m_freeSlots.push(_slot);
    }
    void doFlush()
    {
        std::uint32_t slot = 0;
        while (m_queue.pop(slot))
        {
            feedOne(slot);
        }
        base_type::flush_backend(m_backendMutex, *m_backend);
        {
            // Same missed-wakeup rule as wakeConsumer: the flag must change
            // under the waiter's mutex so notify_all cannot be lost.
            std::lock_guard<std::mutex> lock(m_flushMutex);
            m_flushRequested.store(false, std::memory_order_release);
        }
        m_flushDone.notify_all();
    }
    void feedLoop()
    {
        std::uint32_t slot = 0;
        for (;;)
        {
            if (m_queue.pop(slot))
            {
                feedOne(slot);
                continue;
            }
            if (m_flushRequested.load(std::memory_order_acquire))
            {
                doFlush();
                continue;
            }
            if (m_stop.load(std::memory_order_acquire))
            {
                while (m_queue.pop(slot))
                {
                    feedOne(slot);
                }
                if (m_flushRequested.load(std::memory_order_acquire))
                {
                    doFlush();
                }
                return;
            }
            std::unique_lock<std::mutex> lock(m_waitMutex);
            m_sleeping.store(true);
            if (m_queue.pop(slot))
            {
                m_sleeping.store(false, std::memory_order_release);
                lock.unlock();
                feedOne(slot);
                continue;
            }
            m_cv.wait(lock, [this] {
                return !m_sleeping.load(std::memory_order_acquire) ||
                       m_stop.load(std::memory_order_acquire) ||
                       m_flushRequested.load(std::memory_order_acquire) || !m_queue.empty();
            });
        }
    }

    boost::shared_ptr<BackendT> m_backend;
    std::mutex m_backendMutex;
    std::array<boost::log::record_view, CapacityV> m_slots;
    boost::lockfree::queue<std::uint32_t, boost::lockfree::fixed_sized<true>,
        boost::lockfree::capacity<CapacityV>>
        m_queue;
    boost::lockfree::queue<std::uint32_t, boost::lockfree::fixed_sized<true>,
        boost::lockfree::capacity<CapacityV>>
        m_freeSlots;
    std::thread m_feedingThread;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_sleeping{false};
    std::atomic<bool> m_flushRequested{false};
    std::mutex m_waitMutex;
    std::condition_variable m_cv;
    std::mutex m_flushMutex;
    std::condition_variable m_flushDone;
};
}  // namespace bcos
