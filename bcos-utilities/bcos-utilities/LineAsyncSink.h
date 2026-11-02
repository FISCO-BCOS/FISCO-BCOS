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
 *  message) into a pooled std::string slot and hands the slot to the shared
 *  feeding thread (LineFeeder), which passes the line straight to the boost
 *  text backend (rotation, file collector, auto-flush are kept). When all
 *  slots are in flight the line is dropped (drop-on-overflow) instead of
 *  blocking the producer; dropped lines are counted and reported with a
 *  marker line once the backlog clears. FATAL lines bypass the queue: the
 *  producer first requests a bounded drain of the backlog (so earlier lines
 *  keep their base-behaviour ordering ahead of the abort), then writes the
 *  FATAL line synchronously — it can neither be dropped nor delayed past the
 *  backend's abort(). The drain wait and the backend-lock acquisition are
 *  bounded: if the lock cannot be taken in time (a wedged backend holding
 *  it) the line goes to stderr and the process aborts directly. A backend
 *  whose write itself blocks (e.g. a hung log volume reached while the
 *  worker is idle) can still stall the abort; closing that residual is a
 *  design change tracked in #5701.
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
 *  The shared feeding thread flushes the backend whenever the queue drains
 *  and, under a continuous flood, at a bounded interval: the flood path
 *  consults the clock once per FlushCheckEvery fed lines, so the real
 *  staleness bound is FlushInterval plus up to FlushCheckEvery - 1 lines of
 *  backend latency. Log lines still reach the file promptly without paying
 *  a per-line flush on the producer.
 *
 * @file: LineAsyncSink.h
 */
#pragma once

#include "LineLayout.h"
#include "LogStream.h"
#include <boost/make_shared.hpp>
#include <boost/smart_ptr/shared_ptr.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

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
    // Cumulative lines dropped on overflow since construction (0 for sinks
    // that never drop); summed by droppedLogLines() for the admin RPC.
    virtual std::uint64_t droppedTotal() const noexcept { return 0; }
};

// Process-wide registry (implemented in BoostLog.cpp). The initializer
// registers whole-line sinks on initLog and unregisters them in stopLogging.
void registerLineSink(std::shared_ptr<LineSinkWriter> _sink);
void unregisterLineSink(LineSinkWriter const* _sink);
bool hasLineSinks() noexcept;
void commitLine(LogLevel _level, std::string_view _message);
// Sum of droppedTotal() over every registered sink; read by the
// admin_getLogLevel RPC so log loss is pollable.
std::uint64_t droppedLogLines() noexcept;
// "severity|YYYY-MM-DD HH:MM:SS.ffffff|thread|" — the built-in line prefix,
// rendered on the caller thread.
void appendLinePrefix(std::string& _out, LogLevel _level);

// Process-wide feeding worker shared by every LineAsyncSink instantiation —
// file, console and test sinks all feed on this one thread instead of
// spawning a thread per sink. A sink registers itself at construction and
// wakes the worker after enqueueing; the worker walks the registered sinks
// in order and runs each sink's feedOnWorker(), which drains that sink's
// backlog to empty before the worker advances (run-to-completion, not
// round-robin: safe because production registers at most one line sink —
// file XOR console — and MaxLineSinks is 4). Blocking backend
// IO (file writes, rotation, collector scans) stays confined to this thread,
// keeping it off producer threads and off shared IO pools whose threads also
// log.
//
// The instance is intentionally leaked: line sinks may live in statics whose
// destruction order against a static worker is undefined, and an idle worker
// costs nothing but a thread sleeping on its condition variable.
class LineFeeder
{
public:
    class Client
    {
    public:
        // Runs on the worker thread: drain pending lines to the backend.
        // Must never throw (the worker guards with catch(...) regardless).
        virtual void feedOnWorker() = 0;

    protected:
        ~Client() = default;
    };

    static LineFeeder& instance()
    {
        static LineFeeder* s_instance = new LineFeeder();
        return *s_instance;
    }

    void addClient(Client* _client)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_clients.push_back(_client);
    }

    // Wakes the worker for one pass over all registered clients. Cheap when a
    // wake is already pending or the worker is mid-pass: one atomic exchange,
    // no mutex. Only the thread that flips the flag takes the lock to notify;
    // taking m_mutex after the exchange pairs with the waiter's predicate
    // check under the same mutex, so no wakeup is lost.
    void wake()
    {
        if (!m_dirty.exchange(true, std::memory_order_acq_rel))
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
            }
            m_cv.notify_one();
        }
    }

    // Removes _client, blocking until the worker is not inside its
    // feedOnWorker(); once this returns the worker never touches _client
    // again, so the caller may destroy it.
    void removeClient(Client* _client)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        std::erase(m_clients, _client);
        m_idleCv.wait(lock, [this, _client]() { return m_active.find(_client) == m_active.end(); });
    }

private:
    LineFeeder() : m_thread([this]() { loop(); }) {}

    void loop()
    {
        std::vector<Client*> snapshot;
        for (;;)
        {
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [this]() { return m_dirty.load(std::memory_order_acquire); });
                m_dirty.store(false, std::memory_order_release);
                snapshot = m_clients;
            }
            for (Client* client : snapshot)
            {
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (std::find(m_clients.begin(), m_clients.end(), client) == m_clients.end())
                    {
                        continue;  // removed after the snapshot was taken
                    }
                    m_active.insert(client);
                }
                try
                {
                    client->feedOnWorker();
                }
                catch (...)
                {}
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_active.erase(client);
                }
                m_idleCv.notify_all();
            }
        }
    }

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::condition_variable m_idleCv;
    std::vector<Client*> m_clients;
    // Clients whose feedOnWorker() is running on the worker right now;
    // removeClient() waits for this to clear before letting the sink die.
    std::set<Client*> m_active;
    std::atomic<bool> m_dirty{false};
    std::thread m_thread;
};

// BackendT must provide consumeLine(LogLevel, const std::string&) and
// flush(), as BoostLogInitializer::Sink/ConsoleSink do.
template <typename BackendT, std::size_t CapacityV = 32768>
class LineAsyncSink : public LineSinkWriter, public LineFeeder::Client
{
public:
    // Mimics asynchronous_sink::locked_backend(): serializes backend
    // configuration against the feeding worker.
    class LockedBackend
    {
    public:
        LockedBackend(boost::shared_ptr<BackendT> const& _backend, std::timed_mutex& _mutex)
          : m_lock(_mutex), m_backend(_backend)
        {}
        BackendT* operator->() const { return m_backend.get(); }
        BackendT& operator*() const { return *m_backend; }

    private:
        std::unique_lock<std::timed_mutex> m_lock;
        boost::shared_ptr<BackendT> m_backend;
    };

    LineAsyncSink() : LineAsyncSink(boost::make_shared<BackendT>()) {}
    explicit LineAsyncSink(boost::shared_ptr<BackendT> _backend) : m_backend(std::move(_backend))
    {
        for (std::uint32_t i = 0; i < CapacityV; ++i)
        {
            m_freeSlots.push_back(i);
        }
        LineFeeder::instance().addClient(this);
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

    // Every line dropped on slot-pool overflow since construction; never
    // reset, suitable for an operator-facing metric.
    std::uint64_t droppedTotal() const noexcept override
    {
        return m_droppedTotal.load(std::memory_order_relaxed);
    }

    void writeLine(LogLevel _level, std::string_view _prefix, std::string_view _message) override
    {
        // FATAL bypasses the queue: a queued FATAL could be dropped on
        // overflow or sit behind a backlog, and the abort lives in the
        // backend's consumeLine — write it synchronously so it always lands.
        // First drain the backlog the way flush() does: the base's single
        // FIFO guaranteed every line logged before a FATAL reached the
        // backend before the abort, and that ordering is what makes the
        // lines preceding a FATAL explain it. The drain wait and the backend
        // acquisition below are both bounded: while the worker holds the
        // backend lock, a wedged backend cannot turn the fail-stop into a
        // hang. A backend whose write itself blocks (a hung log volume
        // reached while the worker is idle) can still stall the abort;
        // closing that residual is a design change tracked in #5701.
        if (_level == LogLevel::FATAL)
        {
            {
                std::unique_lock<std::mutex> lock(m_queueMutex);
                if (m_stop)
                {
                    // detached from the worker: drain inline
                    drainQueueLocked(lock);
                    reportDroppedLocked();
                }
                else
                {
                    auto const gen = ++m_flushRequestGen;
                    LineFeeder::instance().wake();
                    m_flushDone.wait_for(
                        lock, FatalDrainTimeout, [this, gen] { return m_flushDoneGen >= gen; });
                }
            }
            std::string line;
            line.reserve(_prefix.size() + _message.size());
            line.append(_prefix);
            line.append(_message);
            try
            {
                std::unique_lock<std::timed_mutex> lock(m_backendMutex, std::defer_lock);
                if (lock.try_lock_for(FatalDrainTimeout))
                {
                    m_backend->consumeLine(_level, line);
                }
                else
                {
                    // The backend is wedged: the feeding worker holds
                    // m_backendMutex inside consumeLine and may never release
                    // it, so the abort in the backend would never run. Keep
                    // the fail-stop total — emit the line to stderr and abort
                    // directly instead of going through the backend.
                    std::fwrite(line.data(), 1, line.size(), stderr);
                    std::fputc('\n', stderr);
                    std::fflush(stderr);
                    std::abort();
                }
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
                m_droppedTotal.fetch_add(1, std::memory_order_relaxed);
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
        LineFeeder::instance().wake();
    }

    // Waits until every buffered line is fed to the backend, then flushes it.
    // Bounded: under a sustained flood the worker's drained branch never
    // runs, so the acknowledgement can take arbitrarily long; on timeout the
    // tail still reaches the file via the flood-path maybeFlush().
    void flush()
    {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        if (m_stop)
        {
            // detached from the worker: drain inline
            drainQueueLocked(lock);
            reportDroppedLocked();
            flushBackendLocked();
            return;
        }
        auto const gen = ++m_flushRequestGen;
        LineFeeder::instance().wake();
        m_flushDone.wait_for(lock, FlushWaitTimeout, [this, gen] { return m_flushDoneGen >= gen; });
    }

    // Detaches from the shared feeding worker; buffered lines are drained
    // inline before returning.
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
        {
            // The worker will not drain for us after removeClient returns, so
            // flush the backlog inline first.
            std::unique_lock<std::mutex> lock(m_queueMutex);
            drainQueueLocked(lock);
            reportDroppedLocked();
            flushBackendLocked();
            if (m_flushDoneGen < m_flushRequestGen)
            {
                // a flush() that raced with stop() is still waiting; the
                // inline drain above satisfies every request so far
                m_flushDoneGen = m_flushRequestGen;
                m_flushDone.notify_all();
            }
        }
        LineFeeder::instance().removeClient(this);
    }

private:
    struct Slot
    {
        std::string line;
        LogLevel level = LogLevel::TRACE;
    };

    // A slot that once held a line this large does not keep the heap buffer
    // when it returns to the pool. The worst-case retained heap per sink is
    // CapacityV x MaxRetainedLineCapacity (~128 MB with the default
    // CapacityV of 32768); typical FISCO-BCOS line lengths keep it around
    // 16 MB. The pool size is a template parameter, not operator-configurable
    // (tracked in #5701).
    static constexpr std::size_t MaxRetainedLineCapacity = 4096;
    // Staleness bound under a continuous flood, where the queue never drains
    // and the drain-time flush never runs: flush at least this often. The
    // flood path only consults the clock once per FlushCheckEvery fed lines,
    // so the effective bound is FlushInterval plus up to FlushCheckEvery - 1
    // lines of backend latency.
    static constexpr auto FlushInterval = std::chrono::milliseconds(100);
    // One clock read per this many fed lines on the flood path.
    static constexpr std::uint64_t FlushCheckEvery = 64;
    // Bounded wait for the pre-FATAL backlog drain: a wedged backend must not
    // turn the fail-stop into a hang.
    static constexpr auto FatalDrainTimeout = std::chrono::seconds(1);
    // Bounded wait for flush(): under a sustained flood the worker's drained
    // branch (the only place requests are acknowledged) never runs, so an
    // unbounded wait could block the caller forever.
    static constexpr auto FlushWaitTimeout = std::chrono::seconds(5);

    // FIB-184: a backend exception must never kill the shared feeding thread.
    // Hot-path backend IO (feedOne, the drained branch, the flood-path
    // maybeFlush) runs without m_queueMutex held: producers keep their
    // single short critical section (LineSinkWriter's contract).
    // m_backendMutex serializes against the other backend users
    // (locked_backend(), the FATAL write).
    void flushBackend()
    {
        try
        {
            std::lock_guard<std::timed_mutex> backendLock(m_backendMutex);
            m_backend->flush();
        }
        catch (...)
        {}
    }

    // Emits one marker line for _dropped dropped lines. Line building lives
    // inside the guard as well: this function is reached from stop() (i.e.
    // the destructor), where an escaping bad_alloc would terminate.
    void reportDropped(std::uint64_t _dropped)
    {
        try
        {
            std::string marker = "[log] dropped ";
            marker += std::to_string(_dropped);
            marker += " log line(s): slot pool exhausted";
            // formatLogLine follows the installed log.format layout, so the
            // marker keeps the same line layout as producer-formatted lines.
            std::string line = formatLogLine(LogLevel::WARNING, marker);
            std::lock_guard<std::timed_mutex> backendLock(m_backendMutex);
            m_backend->consumeLine(LogLevel::WARNING, line);
        }
        catch (...)
        {}
    }

    // Locked variant for the cold paths (flush(), stop()): m_queueMutex is
    // already held there and producer latency is not a concern. m_queueMutex
    // guards m_lastFlush.
    void flushBackendLocked()
    {
        m_lastFlush = std::chrono::steady_clock::now();
        flushBackend();
    }

    // m_queueMutex must be held.
    void reportDroppedLocked()
    {
        auto const dropped = m_droppedLines.exchange(0, std::memory_order_relaxed);
        if (dropped != 0)
        {
            reportDropped(dropped);
        }
    }

    // Flood-path flush: only the bookkeeping runs under m_queueMutex, the
    // backend flush itself runs after releasing it, so a slow file flush
    // does not stall producers in writeLine(). m_lastFlush stays guarded by
    // m_queueMutex, same as the locked variant above.
    void maybeFlush()
    {
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (std::chrono::steady_clock::now() - m_lastFlush < FlushInterval)
            {
                return;
            }
            m_lastFlush = std::chrono::steady_clock::now();
        }
        flushBackend();
    }

    // Drains the backlog inline on the caller thread. _lock holds
    // m_queueMutex on entry and exit.
    void drainQueueLocked(std::unique_lock<std::mutex>& _lock)
    {
        while (!m_queue.empty())
        {
            std::uint32_t slot = m_queue.front();
            m_queue.pop_front();
            _lock.unlock();
            feedOne(slot);
            _lock.lock();
        }
    }

    void feedOne(std::uint32_t _slot)
    {
        Slot& s = m_slots[_slot];
        try
        {
            std::lock_guard<std::timed_mutex> lock(m_backendMutex);
            m_backend->consumeLine(s.level, s.line);
        }
        catch (...)
        {
            // FIB-184: a backend exception must never kill the shared feeding
            // thread.
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

    // LineFeeder::Client: runs on the shared worker thread. Drains the
    // backlog completely; the worker round-robins to the next sink once this
    // queue empties.
    void feedOnWorker() override
    {
        std::uint64_t linesSinceFlushCheck = 0;
        bool fedAnything = false;
        for (;;)
        {
            std::uint32_t slot = 0;
            bool haveSlot = false;
            std::uint64_t dropped = 0;
            std::uint64_t flushReqGen = 0;
            bool needFlush = false;
            {
                std::lock_guard<std::mutex> lock(m_queueMutex);
                if (!m_queue.empty())
                {
                    slot = m_queue.front();
                    m_queue.pop_front();
                    haveSlot = true;
                }
                else
                {
                    // Queue drained. Take only the bookkeeping under the queue
                    // mutex: writeLine() wakes the worker per line, so this
                    // branch runs once per line at any rate the worker keeps
                    // up with, and the backend IO below must not stand in
                    // front of the producers. An empty pass (the worker was
                    // woken mid-drain and the same pass fed the line) skips
                    // the backend flush: nothing new reached the backend.
                    dropped = m_droppedLines.exchange(0, std::memory_order_relaxed);
                    flushReqGen = m_flushRequestGen;
                    needFlush = fedAnything || m_flushDoneGen < flushReqGen;
                    if (needFlush)
                    {
                        m_lastFlush = std::chrono::steady_clock::now();
                    }
                }
            }
            if (haveSlot)
            {
                fedAnything = true;
                feedOne(slot);
                // Under a continuous flood the queue never drains; keep the
                // flush staleness bounded. One clock read per FlushCheckEvery
                // lines; the backend flush itself runs off m_queueMutex.
                if (++linesSinceFlushCheck >= FlushCheckEvery)
                {
                    linesSinceFlushCheck = 0;
                    maybeFlush();
                }
                continue;
            }
            // Drained branch, queue mutex released: surface dropped lines,
            // then flush so the tail of a burst reaches the file right away.
            // Under a flood this branch never runs; that case stays
            // rate-limited by maybeFlush() above.
            if (dropped != 0)
            {
                reportDropped(dropped);
            }
            if (needFlush)
            {
                flushBackend();
            }
            {
                std::lock_guard<std::mutex> lock(m_queueMutex);
                // Acknowledge only the requests seen at the snapshot: a
                // flush() or FATAL drain that raised m_flushRequestGen after
                // the snapshot stays pending for the next pass (its wake()
                // guarantees one), because its own lines may not have been
                // fed by this pass's flushBackend() above.
                if (m_flushDoneGen < flushReqGen)
                {
                    m_flushDoneGen = flushReqGen;
                    m_flushDone.notify_all();
                }
            }
            return;
        }
    }

    boost::shared_ptr<BackendT> m_backend;
    // timed_mutex so the FATAL path can bound its acquisition: a wedged
    // backend must not turn the fail-stop into a hang.
    std::timed_mutex m_backendMutex;
    std::array<Slot, CapacityV> m_slots;
    std::deque<std::uint32_t> m_queue;
    std::deque<std::uint32_t> m_freeSlots;
    std::mutex m_queueMutex;
    std::condition_variable m_flushDone;
    std::atomic<std::uint64_t> m_droppedLines{0};
    // Cumulative count of every line dropped on slot-pool overflow since
    // construction; unlike m_droppedLines it is never reset by marker
    // emission, so it can be exported as an operator-facing metric.
    std::atomic<std::uint64_t> m_droppedTotal{0};
    std::chrono::steady_clock::time_point m_lastFlush{};
    bool m_stop = false;
    // Flush handshake, guarded by m_queueMutex: a requester (flush(), the
    // FATAL drain) bumps m_flushRequestGen and waits for m_flushDoneGen to
    // reach its own generation; the worker acknowledges only the generation
    // it snapshotted before flushBackend(), so a request raised mid-pass is
    // never satisfied early by a flush that predates it.
    std::uint64_t m_flushRequestGen = 0;
    std::uint64_t m_flushDoneGen = 0;
};
}  // namespace bcos::log
