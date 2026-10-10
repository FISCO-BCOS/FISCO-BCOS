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
 * @brief: asynchronous sink for fully formatted log lines. It never touches
 *  boost::log core: the producer thread assembles the complete line (prefix +
 *  message) into a pooled std::string slot and hands the slot to the feeding
 *  thread, which passes the line straight to the boost text backend (rotation,
 *  file collector, auto-flush are kept). When all slots are in flight the line
 *  is dropped (drop-on-overflow) instead of blocking the producer; dropped
 *  lines are counted and reported with a marker line once the backlog clears.
 *  FATAL lines bypass the queue entirely and are written synchronously, so a
 *  FATAL can neither be dropped nor delayed past the backend's abort().
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
 *  steady-state log line causes no allocation on either thread. A slot that
 *  once held an outsized line (beyond MaxRetainedLineCapacity) gets a fresh
 *  string when it returns to the pool, so one huge line cannot pin memory.
 *
 *  The feeding thread flushes the backend whenever the queue drains and, under
 *  a continuous flood, at least every FlushInterval — log lines reach the file
 *  within ~100ms without paying a per-line flush on the producer.
 *
 * @file: LineAsyncSink.h
 */
#pragma once

#include "LogStream.h"
#include <boost/make_shared.hpp>
#include <boost/smart_ptr/shared_ptr.hpp>
#include <array>
#include <atomic>
#include <chrono>
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
void commitLine(LogLevel _level, std::string_view _message);
// "severity|YYYY-MM-DD HH:MM:SS.ffffff|thread|" — shared with the sink so the
// dropped-lines marker keeps the same line layout as producer-formatted lines.
void appendLinePrefix(std::string& _out, LogLevel _level);

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

    // Lines dropped on slot-pool overflow since the last marker was emitted.
    std::uint64_t droppedLines() const noexcept
    {
        return m_droppedLines.load(std::memory_order_relaxed);
    }

    void writeLine(LogLevel _level, std::string_view _prefix, std::string_view _message) override
    {
        // FATAL bypasses the queue: a queued FATAL could be dropped on
        // overflow or sit behind a backlog, and the abort lives in the
        // backend's consumeLine — write it synchronously so it always lands.
        if (_level == LogLevel::FATAL)
        {
            std::string line;
            line.reserve(_prefix.size() + _message.size());
            line.append(_prefix);
            line.append(_message);
            try
            {
                std::lock_guard<std::mutex> lock(m_backendMutex);
                m_backend->consumeLine(_level, line);
            }
            catch (...)
            {}
            return;
        }
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (m_stop)
            {
                return;  // stopped: drop
            }
            if (m_freeSlots.empty())
            {
                // out of slots: drop, and leave a marker once the backlog clears
                m_droppedLines.fetch_add(1, std::memory_order_relaxed);
                return;
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
            reportDroppedLocked();
            flushBackendLocked();
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

    // A slot that once held a line this large does not keep the heap buffer
    // when it returns to the pool.
    static constexpr std::size_t MaxRetainedLineCapacity = 4096;
    // Staleness bound for buffered lines: flush when the queue drains, and
    // under a continuous flood re-check the clock every this many lines.
    static constexpr auto FlushInterval = std::chrono::milliseconds(100);
    static constexpr std::uint64_t FlushCheckEvery = 64;

    // FIB-184: a backend exception must never kill the feeding thread.
    void flushBackendLocked()
    {
        try
        {
            std::lock_guard<std::mutex> backendLock(m_backendMutex);
            m_backend->flush();
        }
        catch (...)
        {}
    }

    // Emits one marker line for the lines dropped since the last report.
    // m_queueMutex must be held.
    void reportDroppedLocked()
    {
        auto const dropped = m_droppedLines.exchange(0, std::memory_order_relaxed);
        if (dropped == 0)
        {
            return;
        }
        std::string line;
        appendLinePrefix(line, LogLevel::WARNING);
        line += "[log] dropped ";
        line += std::to_string(dropped);
        line += " log line(s): slot pool exhausted";
        try
        {
            std::lock_guard<std::mutex> backendLock(m_backendMutex);
            m_backend->consumeLine(LogLevel::WARNING, line);
        }
        catch (...)
        {}
    }

    // m_queueMutex must be held.
    void maybeFlushLocked()
    {
        auto const now = std::chrono::steady_clock::now();
        if (now - m_lastFlush < FlushInterval)
        {
            return;
        }
        m_lastFlush = now;
        flushBackendLocked();
    }

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
        if (s.line.capacity() > MaxRetainedLineCapacity)
        {
            std::string().swap(s.line);
        }
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_freeSlots.push_back(_slot);
        }
    }
    void feedLoop()
    {
        std::uint64_t linesSinceFlushCheck = 0;
        for (;;)
        {
            std::uint32_t slot = 0;
            {
                std::unique_lock<std::mutex> lock(m_queueMutex);
                if (m_queue.empty() && !m_stop && !m_flushRequested)
                {
                    // Queue drained: surface dropped lines, then flush so the
                    // tail of the burst reaches the file right away.
                    reportDroppedLocked();
                    maybeFlushLocked();
                }
                m_cv.wait(lock,
                    [this] { return m_stop || m_flushRequested || !m_queue.empty(); });
                if (m_queue.empty())
                {
                    reportDroppedLocked();
                    if (m_flushRequested)
                    {
                        flushBackendLocked();
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
            // Under a continuous flood the queue never drains; keep the flush
            // staleness bounded. One clock read per FlushCheckEvery lines.
            if (++linesSinceFlushCheck >= FlushCheckEvery)
            {
                linesSinceFlushCheck = 0;
                std::lock_guard<std::mutex> lock(m_queueMutex);
                maybeFlushLocked();
            }
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
    std::atomic<std::uint64_t> m_droppedLines{0};
    std::chrono::steady_clock::time_point m_lastFlush{};
    bool m_stop = false;
    bool m_flushRequested = false;
};
}  // namespace bcos::log
