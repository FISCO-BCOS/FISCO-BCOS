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
 * @brief: asynchronous sink for fully formatted log lines. Unlike
 *  BoundedAsyncSink it never touches boost::log core: the producer thread
 *  assembles the complete line (prefix + message) into a pooled std::string
 *  slot and hands the slot to the feeding thread, which passes the line
 *  straight to the boost text backend (rotation, file collector, auto-flush
 *  are kept). When all slots are in flight the line is dropped
 *  (drop-on-overflow) instead of blocking the producer.
 *
 *  The hand-off is one mutex + two std::deque<uint32_t> (free/ready slot
 *  indices), the same architecture spdlog's async thread pool uses. A
 *  lock-free queue was measured ~350ns/call slower here: the slot pool, the
 *  queue head/tail and the string buffers are 4+ cache lines ping-ponging
 *  between the producer and consumer cores, while the mutex variant touches
 *  1-2 lines and folds slot allocation, line assembly and enqueueing into a
 *  single short critical section.
 *
 *  Slot strings keep their heap capacity between rounds, so after warmup a
 *  steady-state log line causes no allocation on either thread.
 *
 * @file: LineAsyncSink.h
 */
#pragma once

#include "LogStream.h"
#include <boost/make_shared.hpp>
#include <boost/smart_ptr/shared_ptr.hpp>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace bcos::log
{
// Producer-facing interface of a whole-line sink; the registry in
// BoostLog.cpp only knows this base. LogLevel comes from LogStream.h so the
// logging hot path stays free of boost/log headers.
class LineSinkWriter
{
public:
    virtual ~LineSinkWriter() = default;
    // Writes one complete log line (prefix + message). Must never throw and
    // never block the caller on backend IO.
    virtual void writeLine(
        LogLevel _level, std::string_view _prefix, std::string_view _message) = 0;
};

// Process-wide registry (implemented in BoostLog.cpp). The initializer
// registers whole-line sinks on initLog and unregisters them in stopLogging.
void registerLineSink(std::shared_ptr<LineSinkWriter> _sink);
void unregisterLineSink(LineSinkWriter const* _sink);
bool hasLineSinks() noexcept;
bool commitLine(LogLevel _level, std::string_view _message);

// BackendT must provide consumeLine(LogLevel, const std::string&) and
// flush(), as BoostLogInitializer::Sink/ConsoleSink do.
template <typename BackendT, std::size_t CapacityV = 32768>
class LineAsyncSink : public LineSinkWriter
{
public:
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

    LineAsyncSink() : LineAsyncSink(boost::make_shared<BackendT>()) {}
    explicit LineAsyncSink(boost::shared_ptr<BackendT> _backend) : m_backend(std::move(_backend))
    {
        for (std::uint32_t i = 0; i < CapacityV; ++i)
        {
            m_freeSlots.push_back(i);
        }
        m_feedingThread = std::thread([this]() { feedLoop(); });
    }
    LineAsyncSink(LineAsyncSink const&) = delete;
    LineAsyncSink& operator=(LineAsyncSink const&) = delete;
    ~LineAsyncSink() override { stop(); }

    LockedBackend locked_backend() { return LockedBackend(m_backend, m_backendMutex); }

    void writeLine(LogLevel _level, std::string_view _prefix, std::string_view _message) override
    {
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (m_stop || m_freeSlots.empty())
            {
                return;  // stopped or out of slots: drop
            }
            std::uint32_t slot = m_freeSlots.front();
            m_freeSlots.pop_front();
            Slot& s = m_slots[slot];
            s.level = _level;
            s.line.clear();
            s.line.reserve(_prefix.size() + _message.size());
            s.line.append(_prefix);
            s.line.append(_message);
            m_queue.push_back(slot);
        }
        m_cv.notify_one();
    }

    // Waits until every buffered line is fed to the backend, then flushes it.
    void flush()
    {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        if (m_stop)
        {
            // feeding thread already joined: drain inline
            while (!m_queue.empty())
            {
                std::uint32_t slot = m_queue.front();
                m_queue.pop_front();
                lock.unlock();
                feedOne(slot);
                lock.lock();
            }
            std::lock_guard<std::mutex> backendLock(m_backendMutex);
            m_backend->flush();
            return;
        }
        m_flushRequested = true;
        m_cv.notify_all();
        m_flushDone.wait(lock, [this] { return !m_flushRequested; });
    }

    // Stops the feeding thread; buffered lines are drained before it exits.
    void stop()
    {
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (m_stop)
            {
                return;
            }
            m_stop = true;
        }
        m_cv.notify_all();
        if (m_feedingThread.joinable())
        {
            m_feedingThread.join();
        }
    }

private:
    struct Slot
    {
        std::string line;
        LogLevel level = LogLevel::TRACE;
    };

    void feedOne(std::uint32_t _slot)
    {
        Slot& s = m_slots[_slot];
        try
        {
            std::lock_guard<std::mutex> lock(m_backendMutex);
            m_backend->consumeLine(s.level, s.line);
        }
        catch (...)
        {
            // FIB-184: a backend exception must never kill the feeding thread.
        }
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_freeSlots.push_back(_slot);
        }
    }
    void feedLoop()
    {
        for (;;)
        {
            std::uint32_t slot = 0;
            {
                std::unique_lock<std::mutex> lock(m_queueMutex);
                m_cv.wait(lock,
                    [this] { return m_stop || m_flushRequested || !m_queue.empty(); });
                if (m_queue.empty())
                {
                    if (m_flushRequested)
                    {
                        {
                            std::lock_guard<std::mutex> backendLock(m_backendMutex);
                            m_backend->flush();
                        }
                        m_flushRequested = false;
                        m_flushDone.notify_all();
                    }
                    if (m_stop)
                    {
                        return;
                    }
                    continue;
                }
                slot = m_queue.front();
                m_queue.pop_front();
            }
            feedOne(slot);
        }
    }

    boost::shared_ptr<BackendT> m_backend;
    std::mutex m_backendMutex;
    std::array<Slot, CapacityV> m_slots;
    std::deque<std::uint32_t> m_queue;
    std::deque<std::uint32_t> m_freeSlots;
    std::thread m_feedingThread;
    std::mutex m_queueMutex;
    std::condition_variable m_cv;
    std::condition_variable m_flushDone;
    bool m_stop = false;
    bool m_flushRequested = false;
};
}  // namespace bcos::log
