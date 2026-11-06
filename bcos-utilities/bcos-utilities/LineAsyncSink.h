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
    // Writes one record: the caller supplies the sampled LineRecord and the
    // message only, and the sink renders the prefix on its feeding thread. The
    // default implementation renders it here (caller thread) so sinks that do
    // not run a worker still work.
    virtual void writeRecord(LineRecord const& _record, std::string_view _message);
    // Cumulative lines dropped on overflow since construction (0 for sinks
    // that never drop); summed by droppedLogLines() for the admin RPC.
    virtual std::uint64_t droppedTotal() const noexcept { return 0; }
};

// Process-wide registry (implemented in BoostLog.cpp). The initializer
// registers whole-line sinks on initLog and unregisters them in stopLogging.
void registerLineSink(std::shared_ptr<LineSinkWriter> _sink);
void unregisterLineSink(LineSinkWriter const* _sink);
bool hasLineSinks() noexcept;
void commitLine(LineRecord const& _record, std::string_view _message);
// Sum of droppedTotal() over every registered sink; read by the
// admin_getLogLevel RPC so log loss is pollable.
std::uint64_t droppedLogLines() noexcept;
// "severity|YYYY-MM-DD HH:MM:SS.ffffff|thread|" — the built-in line prefix.
// Rendering is cheap and cache-friendly (one localtime_r per second), so it is
// done on whichever thread consumes the record.
void appendRecordPrefix(std::string& _out, LineRecord const& _record);
// Same prefix, sampled and rendered immediately; used by the cold paths
// (dropped-line markers).
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

    // Registers a client in the first free slot. One CAS per slot, no lock: the
    // registration path runs once per sink, and the worker only ever reads the
    // slots.
    void addClient(Client* _client)
    {
        for (auto& slot : m_clientSlots)
        {
            Client* expected = nullptr;
            if (slot.client.compare_exchange_strong(
                    expected, _client, std::memory_order_acq_rel, std::memory_order_relaxed))
            {
                return;
            }
        }
        // More clients than slots. Not reachable with today's deployments
        // (console XOR file); say so instead of dropping silently.
        std::fputs("LineFeeder: client table full, client not registered\n", stderr);
    }

    // Wakes the worker for one pass over all registered clients. Cheap when a
    // wake is already pending or the worker is mid-pass: one atomic exchange
    // plus, only for the thread that flips the flag, one notify.
    //
    // m_dirty doubles as the wait predicate and the wake channel: the worker
    // blocks in m_dirty.wait(false) and this notify_one releases it. There is
    // no separate condition variable, so no mutex has to be acquired just to
    // pair the flag change with the notification (which is what the previous
    // m_cv + m_mutex pair was for). A notify that arrives while the worker is
    // busy is not lost: m_dirty stays true, so the next wait() returns
    // immediately and the worker runs another (possibly empty) pass.
    void wake()
    {
        if (!m_dirty.exchange(true, std::memory_order_acq_rel))
        {
            m_dirty.notify_one();
        }
    }

    // Removes _client, blocking until the worker is not inside its
    // feedOnWorker(); once this returns the worker never touches _client
    // again, so the caller may destroy it.
    //
    // The pointer is cleared first, so no later pass can pick the client up; the
    // worker then publishes inFlight before re-reading the pointer, which closes
    // the window where it might have loaded the pointer just before it was
    // cleared. Waiting uses an epoch counter because feedOnWorker() can take
    // milliseconds (a disk write) and must not be spun on.
    void removeClient(Client* _client)
    {
        for (auto& slot : m_clientSlots)
        {
            if (slot.client.load(std::memory_order_acquire) != _client)
            {
                continue;
            }
            slot.client.store(nullptr, std::memory_order_release);
            for (;;)
            {
                uint32_t const epoch = m_idleEpoch.load(std::memory_order_acquire);
                if (!slot.inFlight.load(std::memory_order_acquire))
                {
                    break;
                }
                m_idleEpoch.wait(epoch, std::memory_order_acquire);
            }
            return;
        }
    }

private:
    LineFeeder() : m_thread([this]() { loop(); }) {}

    void loop()
    {
        for (;;)
        {
            // Block until there is work. m_dirty is cleared *before* the pass,
            // so any wake() that arrives while this pass runs re-raises it and
            // the next wait() returns immediately: no wakeup can be lost, and a
            // spurious notify just costs one empty pass.
            m_dirty.wait(false, std::memory_order_acquire);
            m_dirty.store(false, std::memory_order_relaxed);

            for (auto& slot : m_clientSlots)
            {
                Client* const client = slot.client.load(std::memory_order_acquire);
                if (client == nullptr)
                {
                    continue;
                }
                // Publish "in use" before re-reading the pointer: if
                // removeClient() cleared it in between, the re-read detects that
                // and this pass skips the client instead of calling into an
                // object the remover may already be destroying.
                slot.inFlight.store(true, std::memory_order_release);
                if (slot.client.load(std::memory_order_acquire) != client)
                {
                    slot.inFlight.store(false, std::memory_order_release);
                    bumpIdleEpoch();
                    continue;
                }
                try
                {
                    client->feedOnWorker();
                }
                catch (...)
                {}
                slot.inFlight.store(false, std::memory_order_release);
                bumpIdleEpoch();
            }
        }
    }

    // Publishes "a client left the in-flight state" and wakes the waiters in
    // removeClient().
    void bumpIdleEpoch()
    {
        m_idleEpoch.fetch_add(1, std::memory_order_acq_rel);
        m_idleEpoch.notify_all();
    }

    // Client table. Fixed capacity so the worker can iterate it without a lock;
    // a slot is claimed with a CAS on registration and cleared on removal.
    struct ClientSlot
    {
        std::atomic<Client*> client{nullptr};
        /// Set by the worker for the duration of feedOnWorker(); removeClient()
        /// waits for it to clear before returning.
        std::atomic<bool> inFlight{false};
    };
    static constexpr std::size_t MaxClients = 4;
    std::array<ClientSlot, MaxClients> m_clientSlots{};
    /// Incremented whenever a client leaves the in-flight state; the wait
    /// channel for removeClient().
    std::atomic<uint32_t> m_idleEpoch{0};
    // Work-pending flag, and the wait/notify channel between producers and the
    // worker: producers raise it and notify, the worker waits for true and
    // clears it before each pass.
    std::atomic<bool> m_dirty{false};
    std::thread m_thread;
};

// BackendT must provide consumeLine(LogLevel, const std::string&) and
// flush(), as BoostLogInitializer::Sink/ConsoleSink do.
//
// Data plane: one bounded ring per producing thread instead of one shared queue.
// A producer only ever touches its own ring's tail and its own slots, so logging
// from N threads costs N independent single-producer/single-consumer hand-offs
// instead of N threads contending on one mutex. The feeding worker is the only
// consumer; it serves the ring whose oldest pending line has the earliest
// timestamp, so the file keeps a global chronological order.
//
// Memory: a ring is allocated the first time a thread logs, so the footprint is
// (number of logging threads) x CapacityPerShard slots. A node that logs from
// ~40 threads holds 40 x 4096 slots, in the same range as the single shared pool
// this replaces; MaxQueueShards only bounds the number of rings.
template <typename BackendT, std::size_t CapacityV = 4096, std::size_t MaxShardsV = 128>
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
    explicit LineAsyncSink(boost::shared_ptr<BackendT> _backend)
      : m_backend(std::move(_backend)), m_sinkId(s_nextSinkId.fetch_add(1))
    {
        LineFeeder::instance().addClient(this);
    }
    LineAsyncSink(LineAsyncSink const&) = delete;
    LineAsyncSink& operator=(LineAsyncSink const&) = delete;
    ~LineAsyncSink() override { stop(); }

    LockedBackend locked_backend() { return LockedBackend(m_backend, m_backendMutex); }

    // Lines dropped on ring overflow since the last marker was emitted.
    std::uint64_t droppedLines() const noexcept
    {
        return m_droppedLines.load(std::memory_order_relaxed);
    }

    // Every line dropped on ring overflow since construction; never reset,
    // suitable for an operator-facing metric.
    std::uint64_t droppedTotal() const noexcept override
    {
        return m_droppedTotal.load(std::memory_order_relaxed);
    }

    /// Lines enqueued but not yet handed to the backend. Useful as a backlog
    /// metric, and for asserting drain behaviour in tests.
    std::size_t pendingLines() const noexcept
    {
        std::size_t pending = 0;
        auto const count = m_shardCount.load(std::memory_order_acquire);
        for (std::size_t i = 0; i < count; ++i)
        {
            QueueShard const* const shard = m_shards[i].get();
            if (shard == nullptr)
            {
                continue;
            }
            pending += shard->tail.load(std::memory_order_acquire) -
                       shard->head.load(std::memory_order_acquire);
        }
        if (m_overflowUsed.load(std::memory_order_acquire))
        {
            QueueShard const* const shard = m_overflowShard.get();
            pending += shard->tail.load(std::memory_order_acquire) -
                       shard->head.load(std::memory_order_acquire);
        }
        return pending;
    }

    void writeLine(LogLevel _level, std::string_view _prefix, std::string_view _message) override
    {
        // FATAL bypasses the rings: a queued FATAL could be dropped on overflow
        // or sit behind a backlog, and the abort lives in the backend's
        // consumeLine, so it is written synchronously. The backlog is drained
        // first so the lines that explain the FATAL are already in the file.
        // Both waits are bounded: a wedged backend must not turn the fail-stop
        // into a hang. A backend whose write itself blocks can still stall the
        // abort; closing that residual is a design change tracked in #5701.
        if (_level == LogLevel::FATAL)
        {
            {
                std::unique_lock<std::mutex> lock(m_controlMutex);
                if (m_stop)
                {
                    unlockAndDrain(lock);
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
                    // The backend is wedged: the worker holds m_backendMutex
                    // inside consumeLine and may never release it, so the abort
                    // would never run. Keep the fail-stop total: emit to stderr
                    // and abort directly.
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

        enqueue(_level, nullptr, _prefix, _message, false);
    }

    // Enqueues a record whose prefix is rendered by the feeding thread: the
    // producer only copies the message, which is the part it had to build
    // anyway. FATAL falls back to writeLine because it must be synchronous.
    void writeRecord(LineRecord const& _record, std::string_view _message) override
    {
        if (_record.level == LogLevel::FATAL)
        {
            thread_local std::string prefix;
            prefix.clear();
            appendRecordPrefix(prefix, _record);
            writeLine(_record.level, prefix, _message);
            return;
        }
        enqueue(_record.level, &_record, {}, _message, true);
    }

    // Waits until every buffered line is fed to the backend, then flushes it.
    // Bounded: under a sustained flood the worker's drained branch never runs,
    // so the acknowledgement can take arbitrarily long; on timeout the tail
    // still reaches the file via the flood-path maybeFlush().
    void flush()
    {
        std::unique_lock<std::mutex> lock(m_controlMutex);
        if (m_stop.load(std::memory_order_relaxed))
        {
            unlockAndDrain(lock);
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
            std::lock_guard<std::mutex> lock(m_controlMutex);
            if (m_stop.load(std::memory_order_relaxed))
            {
                return;
            }
            m_stop.store(true, std::memory_order_relaxed);
        }
        {
            // The worker will not drain for us after removeClient returns, so
            // flush the backlog inline first.
            std::unique_lock<std::mutex> lock(m_controlMutex);
            unlockAndDrain(lock);
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
    // Slots per ring and number of rings (one per producing thread). Both are
    // template parameters so tests can instantiate a tiny sink and still
    // exercise the shared-overflow path. Capacity stays the SECOND parameter:
    // existing code spells this type as LineAsyncSink<Backend, N> and means N
    // slots, so reordering would silently change its meaning; production keeps
    // the defaults.
    static constexpr std::size_t CapacityPerShard = CapacityV;
    static constexpr std::size_t MaxQueueShards = MaxShardsV;
    // A slot that once held a line this large does not keep the heap buffer when
    // it is recycled. Worst-case retained heap per sink is
    // MaxQueueShards x CapacityPerShard x this, but only for slots that actually
    // held such a line.
    static constexpr std::size_t MaxRetainedLineCapacity = 4096;
    // Lines handed to the backend per consumeLine call. The backend appends the
    // newline, so a batch is simply the lines joined by '\n'.
    static constexpr std::size_t BatchSize = 32;
    // Upper bound on one assembled batch, so a burst of long lines cannot make
    // the worker's buffer grow without limit.
    static constexpr std::size_t MaxBatchBytes = 64 * 1024;
    // Staleness bound under a continuous flood, where no ring ever drains and
    // the drain-time flush never runs.
    static constexpr auto FlushInterval = std::chrono::milliseconds(100);
    // Where the flood path checks the clock: one clock read per batch.
    static constexpr std::uint64_t FlushCheckEvery = BatchSize;
    // Bounded wait for the pre-FATAL backlog drain.
    static constexpr auto FatalDrainTimeout = std::chrono::seconds(1);
    // Bounded wait for flush(): under a sustained flood the drained branch (the
    // only place requests are acknowledged) never runs.
    static constexpr auto FlushWaitTimeout = std::chrono::seconds(5);
    // Upper bound on the lines one pass drains. A pass must terminate so that
    // removeClient() (sink shutdown) and the other registered sinks make
    // progress: with a continuous flood the rings refill faster than they
    // drain, and "drain until empty" would never return. The bound is far above
    // any realistic burst, so a normal pass still drains everything.
    static constexpr std::size_t MaxLinesPerPass = 64 * 1024;

    struct Slot
    {
        std::string line;  // complete line, or just the message when needsPrefix
        LogLevel level = LogLevel::TRACE;
        LineRecord record;
        /// True when line holds the message only and the prefix still has to be
        /// rendered on the feeding thread.
        bool needsPrefix = false;
    };

    // One single-producer/single-consumer ring. head is advanced only by the
    // worker, tail only by the owning producer, so neither side needs a lock.
    // alignas keeps each shard's counters off its neighbours' cache lines.
    //
    // When there are more producing threads than shards, several threads share
    // the overflow shard. That breaks the single-producer assumption, so such a
    // shard is marked `shared` and its producers serialise on producerMutex and
    // bypass cachedHead (which is only meaningful for a private producer).
    struct alignas(128) QueueShard
    {
        std::atomic<std::size_t> head{0};
        std::atomic<std::size_t> tail{0};
        /// Producer-private copy of head, refreshed only when the ring looks full.
        std::size_t cachedHead = 0;
        std::atomic<std::uint64_t> dropped{0};
        std::unique_ptr<Slot[]> ring;
        /// True for the shared overflow shard; enqueue() then takes producerMutex.
        /// Const and fixed at construction so no thread ever observes a change.
        bool const shared;
        std::mutex producerMutex;

        explicit QueueShard(bool _shared = false)
          : ring(std::make_unique<Slot[]>(CapacityPerShard)), shared(_shared)
        {}
    };

    // Per-thread binding to a shard. m_sinkId guards against a sink being
    // destroyed and a new one landing at the same address.
    struct ThreadShard
    {
        std::uint64_t sinkId = 0;
        QueueShard* shard = nullptr;
    };
    static inline thread_local ThreadShard s_threadShard{};
    static inline std::atomic<std::uint64_t> s_nextSinkId{1};

    /// Appends one line to the calling thread's ring.
    void enqueue(LogLevel _level, LineRecord const* _record, std::string_view _prefix,
        std::string_view _message, bool _needsPrefix)
    {
        if (m_stop.load(std::memory_order_relaxed))
        {
            // The sink has been stopped and detached from the worker, so a line
            // handed in now would sit in a ring nobody drains. Count it instead
            // of swallowing it silently: this is the "leaky sink + late
            // producer" window that the registry's never-destroy policy allows.
            countDrop(nullptr);
            return;
        }
        QueueShard* const shard = shardForProducer();
        if (shard->shared)
        {
            // Several threads share this ring; the single-producer fast path
            // below is invalid for it.
            std::lock_guard<std::mutex> lock(shard->producerMutex);
            pushLocked(shard, _level, _record, _prefix, _message, _needsPrefix, false);
            return;
        }
        pushLocked(shard, _level, _record, _prefix, _message, _needsPrefix, true);
    }

    /// Appends into _shard. _useCachedHead is true only when this thread is the
    /// shard's sole producer.
    void pushLocked(QueueShard* _shard, LogLevel _level, LineRecord const* _record,
        std::string_view _prefix, std::string_view _message, bool _needsPrefix,
        bool _useCachedHead)
    {
        // The producer owns tail, so a plain load is enough. cachedHead is only
        // refreshed when the ring looks full, so the common path never touches
        // the consumer's cache line.
        std::size_t const tail = _shard->tail.load(std::memory_order_relaxed);
        if (_useCachedHead)
        {
            if (tail - _shard->cachedHead >= CapacityPerShard)
            {
                _shard->cachedHead = _shard->head.load(std::memory_order_acquire);
                if (tail - _shard->cachedHead >= CapacityPerShard)
                {
                    countDrop(_shard);
                    return;
                }
            }
        }
        else
        {
            // Shared ring: cachedHead is meaningless (several producers), so the
            // capacity check must read the live head every time. Using the tail
            // as a stand-in would make the check never fire and let producers
            // overwrite slots the worker has not copied yet.
            if (tail - _shard->head.load(std::memory_order_acquire) >= CapacityPerShard)
            {
                countDrop(_shard);
                return;
            }
        }

        Slot& slot = _shard->ring[tail % CapacityPerShard];
        slot.level = _level;
        slot.needsPrefix = _needsPrefix;
        if (_needsPrefix)
        {
            slot.record = *_record;
            slot.line.clear();
            slot.line.reserve(_message.size());
            slot.line.append(_message);
        }
        else
        {
            slot.line.clear();
            slot.line.reserve(_prefix.size() + _message.size());
            slot.line.append(_prefix);
            slot.line.append(_message);
        }
        // Publish: the release store makes every slot write visible to the
        // worker, which reads tail with acquire before touching the slot.
        _shard->tail.store(tail + 1, std::memory_order_release);
        LineFeeder::instance().wake();
    }

    /// Counts one dropped line exactly once: the marker is emitted from
    /// m_droppedLines, so the per-shard counter must not be summed into it again.
    void countDrop(QueueShard* _shard)
    {
        if (_shard != nullptr)
        {
            _shard->dropped.fetch_add(1, std::memory_order_relaxed);
        }
        m_droppedLines.fetch_add(1, std::memory_order_relaxed);
        m_droppedTotal.fetch_add(1, std::memory_order_relaxed);
    }

    QueueShard* shardForProducer()
    {
        if (s_threadShard.sinkId == m_sinkId)
        {
            return s_threadShard.shard;
        }
        // First line from this thread: claim a shard. Control plane, runs once
        // per thread.
        std::lock_guard<std::mutex> lock(m_shardMutex);
        auto const index = m_shardCount.load(std::memory_order_relaxed);
        QueueShard* shard = nullptr;
        if (index < MaxQueueShards)
        {
            // Allocated on first use: a thread that never logs never pays for a
            // ring, which keeps a 40-thread node at ~40 x CapacityPerShard slots
            // instead of MaxQueueShards x CapacityPerShard.
            m_shards[index] = std::make_unique<QueueShard>();
            shard = m_shards[index].get();
            m_shardCount.store(index + 1, std::memory_order_release);
        }
        else
        {
            // More producing threads than shards: several threads share the
            // overflow ring, so it is marked shared and its producers serialise
            // on producerMutex. Unreachable with MaxQueueShards = 128 on any real
            // node, but it must stay correct rather than corrupt a
            // single-producer ring.
            shard = m_overflowShard.get();
            m_overflowUsed.store(true, std::memory_order_release);
        }
        s_threadShard.sinkId = m_sinkId;
        s_threadShard.shard = shard;
        return shard;
    }

    // Picks the non-empty ring whose head line has the earliest timestamp so the
    // file keeps a global chronological order across producing threads.
    bool pickEarliest(QueueShard*& _shard, std::size_t& _index)
    {
        _shard = nullptr;
        auto const count =
            m_shardCount.load(std::memory_order_acquire) +
            (m_overflowUsed.load(std::memory_order_acquire) ? 1U : 0U);
        for (std::size_t i = 0; i < count; ++i)
        {
            QueueShard* const shard =
                i < m_shardCount.load(std::memory_order_relaxed) ? m_shards[i].get() :
                                                                   m_overflowShard.get();
            if (shard == nullptr)
            {
                continue;  // never claimed by a producing thread
            }
            std::size_t const head = shard->head.load(std::memory_order_relaxed);
            if (head == shard->tail.load(std::memory_order_acquire))
            {
                continue;  // empty
            }
            if (_shard == nullptr)
            {
                _shard = shard;
                _index = head;
                continue;
            }
            Slot const& candidate = shard->ring[head % CapacityPerShard];
            Slot const& best = _shard->ring[_index % CapacityPerShard];
            if (candidate.record.secs < best.record.secs ||
                (candidate.record.secs == best.record.secs &&
                    candidate.record.micros < best.record.micros))
            {
                _shard = shard;
                _index = head;
            }
        }
        return _shard != nullptr;
    }

    // LineFeeder::Client: runs on the shared worker thread. Drains every ring and
    // returns; the worker moves on to the next sink afterwards.
    void feedOnWorker() override
    {
        // Only one consumer may walk the rings at a time: the worker and stop()'s
        // inline drain would otherwise interleave their head updates.
        std::lock_guard<std::mutex> drainLock(m_drainMutex);
        bool fedAnything = false;
        std::uint64_t linesSinceFlushCheck = 0;
        std::size_t linesThisPass = 0;
        for (;;)
        {
            if (hasPendingLines())
            {
                fedAnything = true;
                linesThisPass += consumeBatch(linesSinceFlushCheck);
                if (linesThisPass >= MaxLinesPerPass || m_stop.load(std::memory_order_relaxed))
                {
                    // Bounded pass: yield so shutdown and other sinks can run.
                    // Nothing is lost — the remaining lines keep their rings and
                    // wake() brings the worker straight back.
                    return;
                }
                continue;
            }

            // Every ring looked empty. Snapshot the flush generation FIRST:
            // taking m_controlMutex synchronises with flush(), so every line
            // enqueued before that call is visible to the emptiness re-check
            // below. Without this re-check an early pass (one that started before
            // the producers were done, so it saw a stale shard count) would
            // acknowledge a later flush() while lines were still buffered —
            // measured as "flush() returned with lines pending".
            std::uint64_t flushReqGen = 0;
            {
                std::lock_guard<std::mutex> lock(m_controlMutex);
                flushReqGen = m_flushRequestGen;
            }
            if (hasPendingLines())
            {
                continue;
            }

            // Still empty: surface dropped lines, then flush so the tail of a
            // burst reaches the file right away. Under a flood this branch never
            // runs; that case is rate-limited by maybeFlush().
            std::uint64_t dropped = takeDropped();
            if (dropped != 0)
            {
                reportDropped(dropped);
            }
            // An explicit flush() request must reach the backend even when this
            // pass fed nothing: the caller asked for the file to be flushed, not
            // merely for the queue to be empty.
            bool const needFlush = fedAnything || m_flushDoneGen < flushReqGen;
            if (needFlush)
            {
                std::lock_guard<std::mutex> lock(m_controlMutex);
                m_lastFlush = std::chrono::steady_clock::now();
            }
            if (needFlush)
            {
                flushBackend();
            }
            {
                std::lock_guard<std::mutex> lock(m_controlMutex);
                // Acknowledge only the requests seen at the snapshot: a flush()
                // raised after it stays pending for the next pass (its wake()
                // guarantees one), because its own lines may not have been fed
                // by this pass's flushBackend() above.
                if (m_flushDoneGen < flushReqGen)
                {
                    m_flushDoneGen = flushReqGen;
                    m_flushDone.notify_all();
                }
            }
            return;
        }
    }

    bool hasPendingLines()
    {
        auto const count = m_shardCount.load(std::memory_order_acquire);
        for (std::size_t i = 0; i < count; ++i)
        {
            QueueShard* const shard = m_shards[i].get();
            if (shard == nullptr)
            {
                continue;
            }
            if (shard->head.load(std::memory_order_relaxed) !=
                shard->tail.load(std::memory_order_acquire))
            {
                return true;
            }
        }
        // The shared overflow ring is not part of m_shards, so it has to be
        // checked explicitly: forgetting it stranded every line written by a
        // thread beyond MaxQueueShards.
        if (m_overflowUsed.load(std::memory_order_acquire))
        {
            QueueShard* const shard = m_overflowShard.get();
            if (shard->head.load(std::memory_order_relaxed) !=
                shard->tail.load(std::memory_order_acquire))
            {
                return true;
            }
        }
        return false;
    }

    // Consumes up to BatchSize lines, merged across rings so the batch is in
    // timestamp order, and writes it to the backend in one call.
    // Returns the number of lines written.
    std::size_t consumeBatch(std::uint64_t& _linesSinceFlushCheck)
    {
        thread_local std::string batch;
        batch.clear();

        std::size_t written = 0;
        LogLevel firstLevel = LogLevel::INFO;
        for (std::size_t n = 0; n < BatchSize && batch.size() < MaxBatchBytes; ++n)
        {
            QueueShard* shard = nullptr;
            std::size_t index = 0;
            if (!pickEarliest(shard, index))
            {
                break;
            }
            // Read and copy the slot BEFORE advancing head. Advancing head is
            // what hands the slot back to the producer, so publishing the claim
            // first would let the producer overwrite the line while it is still
            // being copied — dropped and duplicated lines, which is exactly what
            // the concurrency tests caught. Only one consumer runs at a time
            // (m_drainMutex), so head can move with a plain store.
            Slot& slot = shard->ring[index % CapacityPerShard];
            if (written == 0)
            {
                firstLevel = slot.level;
            }
            if (!batch.empty())
            {
                batch += '\n';
            }
            appendSlotLine(batch, slot);
            // Release an oversized buffer while we still own the slot.
            if (slot.line.capacity() > MaxRetainedLineCapacity)
            {
                std::string().swap(slot.line);
            }
            shard->head.store(index + 1, std::memory_order_release);
            ++written;
        }
        if (written == 0)
        {
            return 0;
        }

        try
        {
            std::lock_guard<std::timed_mutex> lock(m_backendMutex);
            // The backend only inspects the level for FATAL, which never reaches
            // a ring, so the first line's level is representative.
            m_backend->consumeLine(firstLevel, batch);
        }
        catch (...)
        {
            // FIB-184: a backend exception must never kill the shared feeding
            // thread.
        }


        _linesSinceFlushCheck += written;
        if (_linesSinceFlushCheck >= FlushCheckEvery)
        {
            _linesSinceFlushCheck = 0;
            maybeFlush();
        }
        return written;
    }

    // Takes the pending drop count for the marker. m_droppedLines is the single
    // authoritative counter (countDrop() raises it once per dropped line); the
    // per-shard counters are informational and must not be summed in here.
    std::uint64_t takeDropped()
    {
        return m_droppedLines.exchange(0, std::memory_order_relaxed);
    }

    // Appends one slot's line (rendering the prefix first when deferred).
    void appendSlotLine(std::string& _out, Slot& _slot)
    {
        if (_slot.needsPrefix)
        {
            appendRecordPrefix(_out, _slot.record);
            _out.append(_slot.line);
        }
        else
        {
            _out.append(_slot.line);
        }
    }

    // Drains every ring inline on the caller thread. On entry _lock holds
    // m_controlMutex; it is released for the backend IO and re-held on return so
    // the caller keeps its invariant.
    void unlockAndDrain(std::unique_lock<std::mutex>& _lock)
    {
        _lock.unlock();
        {
            // Take over the consumer role from the worker for the duration.
            std::lock_guard<std::mutex> drainLock(m_drainMutex);
            std::uint64_t ignored = 0;
            for (;;)
            {
                if (!hasPendingLines())
                {
                    break;
                }
                consumeBatch(ignored);
            }
        }
        _lock.lock();
    }

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
    // inside the guard as well: this function is reached from stop() (i.e. the
    // destructor), where an escaping bad_alloc would terminate.
    void reportDropped(std::uint64_t _dropped)
    {
        try
        {
            std::string marker = "[log] dropped ";
            marker += std::to_string(_dropped);
            marker += " log line(s): ring buffer full";
            // formatLogLine follows the installed log.format layout, so the
            // marker keeps the same line layout as producer-formatted lines.
            std::string line = formatLogLine(LogLevel::WARNING, marker);
            std::lock_guard<std::timed_mutex> backendLock(m_backendMutex);
            m_backend->consumeLine(LogLevel::WARNING, line);
        }
        catch (...)
        {}
    }

    // Locked variant for the cold paths (flush(), stop()): m_controlMutex is
    // already held there and producer latency is not a concern.
    void flushBackendLocked()
    {
        m_lastFlush = std::chrono::steady_clock::now();
        flushBackend();
    }

    // Caller holds m_controlMutex.
    void reportDroppedLocked()
    {
        auto const dropped = takeDropped();
        if (dropped != 0)
        {
            reportDropped(dropped);
        }
    }

    // Flood-path flush: only the bookkeeping runs under m_controlMutex, the
    // backend flush itself runs after releasing it, so a slow file flush does
    // not stall producers.
    void maybeFlush()
    {
        {
            std::lock_guard<std::mutex> lock(m_controlMutex);
            if (std::chrono::steady_clock::now() - m_lastFlush < FlushInterval)
            {
                return;
            }
            m_lastFlush = std::chrono::steady_clock::now();
        }
        flushBackend();
    }

    boost::shared_ptr<BackendT> m_backend;
    // timed_mutex so the FATAL path can bound its acquisition: a wedged backend
    // must not turn the fail-stop into a hang.
    std::timed_mutex m_backendMutex;

    std::uint64_t const m_sinkId;
    std::array<std::unique_ptr<QueueShard>, MaxQueueShards> m_shards;
    std::atomic<std::size_t> m_shardCount{0};
    /// Set once a producing thread had to fall back to the shared overflow ring.
    std::atomic<bool> m_overflowUsed{false};
    // Shard for producing threads beyond MaxQueueShards, guarded by m_shardMutex.
    std::unique_ptr<QueueShard> m_overflowShard = std::make_unique<QueueShard>(true);
    std::mutex m_shardMutex;

    // Serialises the consumers: the shared worker and stop()'s inline drain.
    // Never taken on the producer data path.
    std::mutex m_drainMutex;
    // Control plane only (flush generations, stop flag, flush pacing). Never
    // taken on the producer data path.
    mutable std::mutex m_controlMutex;
    std::condition_variable m_flushDone;
    std::atomic<std::uint64_t> m_droppedLines{0};
    // Cumulative count of every line dropped on ring overflow since
    // construction; unlike m_droppedLines it is never reset by marker emission,
    // so it can be exported as an operator-facing metric.
    std::atomic<std::uint64_t> m_droppedTotal{0};
    std::chrono::steady_clock::time_point m_lastFlush{};
    std::atomic<bool> m_stop{false};
    // Flush handshake, guarded by m_controlMutex: a requester (flush(), the
    // FATAL drain) bumps m_flushRequestGen and waits for m_flushDoneGen to reach
    // its own generation; the worker acknowledges only the generation it
    // snapshotted before flushBackend(), so a request raised mid-pass is never
    // satisfied early by a flush that predates it.
    std::uint64_t m_flushRequestGen = 0;
    std::uint64_t m_flushDoneGen = 0;
};

// Sinks that do not run a worker render the prefix on the caller thread; the
// asynchronous sink overrides this to defer the rendering to its worker.
inline void LineSinkWriter::writeRecord(LineRecord const& _record, std::string_view _message)
{
    thread_local std::string prefix;
    prefix.clear();
    appendRecordPrefix(prefix, _record);
    writeLine(_record.level, prefix, _message);
}
}  // namespace bcos::log
