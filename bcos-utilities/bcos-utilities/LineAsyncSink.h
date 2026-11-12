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
 *  The hand-off is one bounded ring buffer per producing thread (QueueShard),
 *  single-producer/single-consumer: a producer only writes its own ring's tail
 *  and its own slots, the feeding thread only advances heads. Logging from N
 *  threads therefore costs N independent hand-offs instead of N threads
 *  contending on one mutex.
 *
 *  Ordering. The feeding thread repeatedly picks the non-empty ring whose head
 *  line carries the earliest timestamp, so lines from different producers are
 *  merged by event time. This REDUCES the number of inversions relative to a
 *  single shared queue; it does NOT restore a global order. A thread that
 *  samples its timestamp at t1 and is then descheduled can enqueue after a
 *  thread that sampled t2 > t1, and the feeder cannot wait for a line it cannot
 *  see. Identical timestamps are broken by ring order. Within one thread the
 *  order is exact.
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
 *  Persistence. log.flush=true used to flush after every line; a batch is one
 *  backend write, so with log.flush=true the pipeline emits each line as its own
 *  batch instead of joining up to BatchSize of them (see batchLimitFor).
 *  Otherwise a crash could lose up to BatchSize - 1 already-built lines, which
 *  are exactly the ones a post-mortem needs.
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
#include <memory>
#include <mutex>
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
    //
    // _record, when not null, is the context sampled on the producing thread.
    // The sink stamps its queue slot with it so the feeding thread can merge
    // rings by event time. The custom log.format path renders the whole line
    // before handing it over, so it has no prefix to defer and must pass the
    // record explicitly; without it every slot would carry a zero timestamp and
    // the merge would silently degrade to ring-index order.
    virtual void writeLine(LogLevel _level, std::string_view _prefix, std::string_view _message,
        LineRecord const* _record = nullptr) = 0;
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
// Client management is a plain mutex + condition variable, and deliberately so.
// It is control plane: addClient() runs once per sink, removeClient() once at
// teardown, and the worker takes the lock once per pass. Producers never touch
// it — a log call only raises the atomic m_dirty flag — so the lock is never on
// the hot path. An earlier lock-free version (per-slot atomics plus an in-flight
// flag and an epoch counter) was both harder to review and unsound: worker and
// remover each stored one variable and then loaded the other, which is the
// Dekker pattern, and release/acquire does not provide the StoreLoad barrier
// that pattern needs. Both sides could therefore miss each other's store and
// the worker could call into a client the remover had already started
// destroying. Holding the mutex for the "is it still registered?" check and the
// "mark in flight" update makes that impossible, because both now happen in one
// critical section.
//
// The instance is intentionally leaked: line sinks may live in statics whose
// destruction order against a static worker is undefined, and an idle worker
// costs nothing but a thread sleeping on its wait channel. Note that leaking the
// worker is also what makes sink teardown safe — with an ordinary static, the
// worker would be destroyed *before* the sink that lazily created it (the sink's
// constructor is what first calls instance()), so removeClient() during the
// sink's destruction would touch a dead object.
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

    // Registration and removal are rare (once per sink lifetime), so neither is
    // worth optimising at the cost of the reasoning above.
    void addClient(Client* _client)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_clients.push_back(_client);
    }

    // Wakes the worker for one pass over all registered clients. Cheap when a
    // wake is already pending or the worker is mid-pass: one atomic exchange
    // plus, only for the thread that flips the flag, one notify.
    //
    // m_dirty doubles as the wait predicate and the wake channel: the worker
    // blocks in m_dirty.wait(false) and this notify_one releases it. There is
    // no separate condition variable, so no mutex has to be acquired just to
    // pair the flag change with the notification. A notify that arrives while
    // the worker is busy is not lost: m_dirty stays true, so the next wait()
    // returns immediately and the worker runs another (possibly empty) pass.
    void wake()
    {
        if (!m_dirty.exchange(true, std::memory_order_acq_rel))
        {
            m_dirty.notify_one();
        }
    }

    // Removes _client, blocking until the worker is not inside its
    // feedOnWorker(); once this returns the worker never touches _client again,
    // so the caller may destroy it. This is a concurrency barrier, not just
    // bookkeeping: the worker is a long-lived thread, so it races with sink
    // teardown no matter how the two objects are ordered.
    //
    // Erasing under the mutex is what makes it sound. The worker marks a client
    // in flight under the SAME mutex only after re-checking that it is still
    // registered, so either the worker sees the erase (and skips the client
    // after this function returns) or this function sees the in-flight mark (and
    // waits). One critical section, one total order, no fences needed.
    void removeClient(Client* _client)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        auto const it = std::find(m_clients.begin(), m_clients.end(), _client);
        if (it == m_clients.end())
        {
            return;
        }
        m_clients.erase(it);
        m_idleCv.wait(
            lock, [this, _client] { return std::find(m_active.begin(), m_active.end(), _client) ==
                                           m_active.end(); });
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

            // Snapshot under the lock, then call the clients with it released:
            // feedOnWorker() does disk IO and must not block addClient() or
            // removeClient() for its duration.
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_snapshot.assign(m_clients.begin(), m_clients.end());
            }
            for (Client* const client : m_snapshot)
            {
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    // Re-check membership: the client may have been removed
                    // between the snapshot and now. Removing it also invalidated
                    // the pointer, so it must not be dereferenced here.
                    if (std::find(m_clients.begin(), m_clients.end(), client) == m_clients.end())
                    {
                        continue;
                    }
                    m_active.push_back(client);
                }
                try
                {
                    client->feedOnWorker();
                }
                catch (...)
                {}
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    auto const it = std::find(m_active.begin(), m_active.end(), client);
                    if (it != m_active.end())
                    {
                        m_active.erase(it);
                    }
                }
                m_idleCv.notify_all();
            }
        }
    }

    std::mutex m_mutex;
    /// Signalled whenever a client leaves the in-flight set; the wait channel for
    /// removeClient().
    std::condition_variable m_idleCv;
    /// Registered clients, guarded by m_mutex.
    std::vector<Client*> m_clients;
    /// Clients the worker is currently inside feedOnWorker() for, guarded by
    /// m_mutex.
    std::vector<Client*> m_active;
    /// Worker-private pass snapshot; a member so a pass does not allocate.
    std::vector<Client*> m_snapshot;
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
// timestamp.
//
// That merge REDUCES cross-thread inversions relative to the single shared queue
// (measured: 0.13% vs 3.99% on the writeRecord path); it does not eliminate them.
// A thread that samples its timestamp and is then descheduled enqueues after a
// thread with a later timestamp, and the worker cannot know it is coming. Lines
// from one thread are always in order, and equal timestamps are broken by ring
// index. Any consumer that needs a strict global order or cross-thread causality
// cannot get it from this design.
//
// Memory: a ring is allocated the first time a thread logs, so the footprint is
// (threads that have logged) x CapacityPerShard slots. A node logging from ~40
// threads holds ~40 x 4096 slots, the same range as the single shared pool this
// replaces. Rings are 4096 slots; the old pool had 32768 in total, so a node with
// more than 8 logging threads uses more memory than before, not less.
//
// A ring whose owning thread has exited is reclaimed (see ThreadToken): the next
// thread that needs one reuses it instead of allocating, so thread churn cannot
// consume the shard budget and silently push every producer onto the shared
// overflow ring.
template <typename BackendT, std::size_t CapacityV = 4096, std::size_t MaxShardsV = 128,
    std::size_t MaxLinesPerPassV = 64 * 1024>
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

    /// Tells the sink that the operator configured log.flush=true, i.e. that a
    /// line must be durable by the time the producing statement returns. Batching
    /// would turn that into "durable in groups of 32", so the sink switches to one
    /// line per backend write. Called from initLog before the sink is registered.
    void setPerLinePersistence(bool _enabled) noexcept
    {
        m_perLinePersistence.store(_enabled, std::memory_order_relaxed);
    }

    /// Number of private rings this sink has allocated. Diagnostic: it tells an
    /// operator whether the node is within MaxQueueShards and whether rings are
    /// being reused instead of growing without bound.
    std::size_t ringCount() const noexcept
    {
        return m_shardCount.load(std::memory_order_acquire);
    }

    /// Lines enqueued but not yet handed to the backend. Useful as a backlog
    /// metric, and for asserting drain behaviour in tests.
    std::size_t pendingLines() const noexcept
    {
        auto const pendingOf = [](QueueShard const* _shard) -> std::size_t {
            return _shard == nullptr ? 0 : _shard->tail.load(std::memory_order_acquire) -
                                                _shard->head.load(std::memory_order_acquire);
        };
        std::size_t pending = 0;
        auto const count = m_shardCount.load(std::memory_order_acquire);
        for (std::size_t i = 0; i < count; ++i)
        {
            pending += pendingOf(m_shards[i].get());
        }
        pending += pendingOf(m_overflowShard.load(std::memory_order_acquire));
        return pending;
    }

    void writeLine(LogLevel _level, std::string_view _prefix, std::string_view _message,
        LineRecord const* _record = nullptr) override
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

        enqueue(_level, _record, _prefix, _message, false);
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
        // A producer that was already past its own m_stop check when the flag was
        // set publishes its line after the drain above, so drain once more now
        // that the worker can no longer touch this sink. Producers that publish
        // after this point see m_stop and drain the line themselves (see
        // enqueue()), which is what keeps the line from being stranded.
        {
            std::unique_lock<std::mutex> lock(m_controlMutex);
            unlockAndDrain(lock);
            reportDroppedLocked();
            flushBackendLocked();
        }
    }

    /// Drains the backlog on a producing thread that lost the race with stop().
    /// The line it just published would otherwise sit in a ring the worker has
    /// already detached from: neither consumed nor counted as dropped.
    void drainInlineAfterStop()
    {
        std::unique_lock<std::mutex> lock(m_controlMutex);
        unlockAndDrain(lock);
        reportDroppedLocked();
        flushBackendLocked();
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
    // any realistic burst, so a normal pass still drains everything. A pass that
    // hits it wakes the worker again (see feedOnWorker) so the remainder is not
    // stranded when no producer logs again. Configurable so a test can shrink it
    // to a few lines and exercise that path deterministically.
    static constexpr std::size_t MaxLinesPerPass = MaxLinesPerPassV;

    struct Slot
    {
        std::string line;  // complete line, or just the message when needsPrefix
        LogLevel level = LogLevel::TRACE;
        LineRecord record;
        /// True when line holds the message only and the prefix still has to be
        /// rendered on the feeding thread.
        bool needsPrefix = false;
    };

    // Identifies one thread lifetime, so a ring can tell whether the thread that
    // claimed it is still running. Heap-owned and referenced by every ring that
    // points at it, so it outlives the thread: the TLS holder only clears
    // `alive`, it never destroys the object. That is what makes reclamation safe
    // — the check reads memory that is still valid after the thread is gone.
    struct ThreadToken
    {
        std::atomic<bool> alive{true};
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
        std::unique_ptr<Slot[]> ring;
        /// True for the shared overflow shard; enqueue() then takes producerMutex.
        /// Const and fixed at construction so no thread ever observes a change.
        bool const shared;
        std::mutex producerMutex;
        /// Thread lifetime that claimed this ring. Written only under m_shardMutex;
        /// read there too (never by the data path). A ring whose owner has exited
        /// is handed to the next thread that needs one.
        std::shared_ptr<ThreadToken> owner;
        /// Set once a producing thread had to fall back to this ring, which makes
        /// its producers serialise. Only used to report the condition once.
        std::atomic<bool> everShared{false};

        explicit QueueShard(bool _shared = false)
          : ring(std::make_unique<Slot[]>(CapacityPerShard)), shared(_shared)
        {}
    };

    // Per-thread cache of (sink -> ring) bindings, and the token describing this
    // thread's lifetime.
    //
    // More than one slot because two live sinks of the same type each need their
    // own binding (a test sink plus the initializer's, or console + file while a
    // config reload swaps them). With a single slot every alternating line
    // re-entered the claim path, and the claim path allocated a fresh 4096-slot
    // ring each time — the reviewer's "79 ns statement becomes 100 us while
    // holding m_shardMutex". A tiny array makes rebinding a non-event.
    struct ThreadBinding
    {
        std::uint64_t sinkId = 0;
        QueueShard* shard = nullptr;
    };
    static constexpr std::size_t MaxThreadBindings = 4;

    struct ThreadLocalState
    {
        ThreadBinding bindings[MaxThreadBindings]{};
        std::size_t nextVictim = 0;
        std::shared_ptr<ThreadToken> token = std::make_shared<ThreadToken>();

        ~ThreadLocalState() { token->alive.store(false, std::memory_order_release); }
    };

    // Function-local, not a class-scope `static inline thread_local` member.
    // A class-scope one with dynamic initialisation (the shared_ptr above) makes
    // GCC emit a file-scope `__tls_guard` symbol; a unity build that also pulls
    // in another dynamically-initialised thread_local from the same TU (for
    // example Common.cpp's `thread_local std::string c_threadName`) then fails
    // with "redefinition of 'bool __tls_guard'" at this line. Verified with
    // g++ 16 on the real unity TU. The per-thread lookup is a single TLS address
    // computation plus an inline guard check, and the fast path calls it once
    // per log statement.
    static ThreadLocalState& threadLocalState()
    {
        static thread_local ThreadLocalState state{};
        return state;
    }
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
            countDrop();
            return;
        }
        // The record pointer is passed straight through: push() stamps the slot
        // once the line is known to fit, and only samples a timestamp when the
        // caller supplied none. Nothing is copied on the hot path here.
        QueueShard* const shard = shardForProducer();
        bool published = false;
        if (shard->shared)
        {
            // Several threads share this ring; the single-producer fast path
            // below is invalid for it.
            std::lock_guard<std::mutex> lock(shard->producerMutex);
            published = push(shard, _level, _record, _prefix, _message, _needsPrefix, false);
        }
        else
        {
            published = push(shard, _level, _record, _prefix, _message, _needsPrefix, true);
        }
        if (!published)
        {
            return;
        }
        // The entering check above cannot rule out a stop() that lands while this
        // line is being built: a producer that copies a large message can lose
        // milliseconds there, long enough for stop() to set the flag, drain
        // (seeing nothing) and detach from the worker. The line would then sit in
        // a ring nobody drains — not consumed, not counted as dropped, invisible.
        //
        // The re-check below NARROWS that window, it does not close it: this
        // store (release) followed by the load of m_stop and stop()'s store
        // (relaxed) followed by its load of the tail are a Dekker pattern, and
        // release/acquire provides no StoreLoad barrier, so the standard permits
        // both sides to miss each other. What actually makes it unreachable in
        // practice is the ordering AFTER this point, and both steps are
        // load-bearing:
        //   1. a producer that reads m_stop == false here goes on to call
        //      wake(), whose m_dirty.exchange(acq_rel) is an RMW that commits the
        //      tail store above within nanoseconds;
        //   2. stop() takes m_controlMutex (an acquiring RMW) before its first
        //      drain, and runs a third drain after removeClient(), which itself
        //      takes a mutex.
        // A stranded line therefore needs this thread to be descheduled between
        // the publish and this load for that whole sequence, and a context switch
        // is a full barrier. Closing the gap formally would cost a seq_cst fence
        // on the per-line hot path. Do not remove the post-removeClient() drain,
        // and do not turn wake() into a plain store: either reopens this silently.
        //
        // The branch that DOES see the flag runs drainInlineAfterStop(), i.e.
        // backend file I/O plus a flush on a producing thread. That breaks the
        // "producer never touches the backend" rule, but only during shutdown and
        // only for the line that lost the race.
        if (m_stop.load(std::memory_order_acquire))
        {
            drainInlineAfterStop();
            return;
        }
        LineFeeder::instance().wake();
    }

    /// Appends into _shard. _useCachedHead is true only when this thread is the
    /// shard's sole producer. Returns false when the line was dropped because the
    /// ring was full.
    bool push(QueueShard* _shard, LogLevel _level, LineRecord const* _record,
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
                    countDrop();
                    return false;
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
                countDrop();
                return false;
            }
        }

        Slot& slot = _shard->ring[tail % CapacityPerShard];
        slot.level = _level;
        slot.needsPrefix = _needsPrefix;
        // Stamped on both paths, and only once the line is known to fit: the merge
        // across rings reads it unconditionally. A caller that formatted the whole
        // line itself has no deferred prefix but still supplies the stamp; a direct
        // writeLine that supplies none gets one sampled here, so the slot is never
        // left with a zero timestamp.
        if (_record != nullptr)
        {
            slot.record = *_record;
        }
        else
        {
            slot.record = sampleLineRecord(_level);
        }
        if (_needsPrefix)
        {
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
        return true;
    }

    /// Counts one dropped line exactly once. m_droppedLines is the single
    /// authoritative counter that the marker line is built from; per-shard
    /// counters were removed because nothing ever read them.
    void countDrop()
    {
        m_droppedLines.fetch_add(1, std::memory_order_relaxed);
        m_droppedTotal.fetch_add(1, std::memory_order_relaxed);
    }

    QueueShard* shardForProducer()
    {
        // Fast path: this thread already holds a ring for this sink.
        auto& tls = threadLocalState();
        for (auto const& binding : tls.bindings)
        {
            if (binding.sinkId == m_sinkId)
            {
                return binding.shard;
            }
        }
        // First line from this thread for this sink: claim a ring. Control plane,
        // runs once per (thread, sink) pair.
        std::lock_guard<std::mutex> lock(m_shardMutex);
        auto const count = m_shardCount.load(std::memory_order_relaxed);
        QueueShard* shard = nullptr;
        // Reuse the ring of a thread that has exited before allocating a new one:
        // otherwise thread churn (temporary std::thread objects, a rebuilt IO
        // pool) consumes the ring budget permanently, and once it is exhausted
        // every producer silently lands on the shared overflow ring and loses all
        // of the concurrency this design exists for.
        for (std::size_t i = 0; i < count; ++i)
        {
            QueueShard* const candidate = m_shards[i].get();
            if (candidate->owner->alive.load(std::memory_order_acquire))
            {
                continue;
            }
            // Only an EMPTY ring may be handed over. A dead thread can leave lines
            // behind that the worker has not drained yet; giving such a ring to a
            // new producer would hand it a ring that already looks full, so its
            // first lines would be dropped. Leave it to drain and reclaim it later.
            if (candidate->head.load(std::memory_order_acquire) !=
                candidate->tail.load(std::memory_order_acquire))
            {
                continue;
            }
            // Anything the previous owner left in the ring stays there and is
            // drained normally: head/tail keep counting, only the producer
            // changes. cachedHead is producer-private and must start from the live
            // head rather than the dead thread's stale copy.
            candidate->owner = tls.token;
            candidate->cachedHead = candidate->head.load(std::memory_order_acquire);
            shard = candidate;
            break;
        }
        if (shard == nullptr && count < MaxQueueShards)
        {
            // Allocated on first use: a thread that never logs never pays for a
            // ring.
            m_shards[count] = std::make_unique<QueueShard>();
            m_shards[count]->owner = tls.token;
            shard = m_shards[count].get();
            m_shardCount.store(count + 1, std::memory_order_release);
        }
        if (shard == nullptr)
        {
            // More producing threads than rings, and none of the owners has
            // exited: several threads now share the overflow ring, so it is marked
            // shared and its producers serialise on producerMutex. Correct, but it
            // gives up the per-thread hand-off, so it is worth reporting once.
            shard = overflowShard();
            if (!shard->everShared.exchange(true, std::memory_order_relaxed))
            {
                std::fprintf(stderr,
                    "LineAsyncSink: %zu rings in use, further producers share one ring\n",
                    MaxQueueShards);
            }
        }
        // Round-robin replacement: all bindings were misses, so any slot is cold.
        auto& slot = tls.bindings[tls.nextVictim];
        slot.sinkId = m_sinkId;
        slot.shard = shard;
        tls.nextVictim = (tls.nextVictim + 1) % MaxThreadBindings;
        return shard;
    }

    /// The shared ring used when every private ring is claimed. Caller holds
    /// m_shardMutex. Allocated on first use: it is unreachable with
    /// MaxQueueShards = 128 on any real node, so paying ~400 KB per sink for it
    /// up front contradicted the lazily-allocated design of the private rings.
    QueueShard* overflowShard()
    {
        if (m_overflowShard.load(std::memory_order_relaxed) == nullptr)
        {
            m_overflowOwner = std::make_unique<QueueShard>(true);
            m_overflowShard.store(m_overflowOwner.get(), std::memory_order_release);
        }
        return m_overflowShard.load(std::memory_order_relaxed);
    }

    // Picks the non-empty ring whose head line has the earliest timestamp, which
    // merges the per-thread rings by event time.
    //
    // One helper and one source of truth for "does this sink have more rings":
    // the overflow ring is either allocated or not, and every enumeration goes
    // through consider()/pending(). The previous version summed a separate flag
    // into a loop bound with unspecified operand evaluation order, so this
    // function and hasPendingLines() could disagree about whether the overflow
    // ring existed — which showed up as a drain loop that made no progress.
    bool pickEarliest(QueueShard*& _shard, std::size_t& _index)
    {
        _shard = nullptr;
        _index = 0;
        auto const consider = [&_shard, &_index](QueueShard* _candidate) {
            if (_candidate == nullptr)
            {
                return;
            }
            std::size_t const head = _candidate->head.load(std::memory_order_relaxed);
            if (head == _candidate->tail.load(std::memory_order_acquire))
            {
                return;  // empty
            }
            if (_shard == nullptr)
            {
                _shard = _candidate;
                _index = head;
                return;
            }
            Slot const& candidate = _candidate->ring[head % CapacityPerShard];
            Slot const& best = _shard->ring[_index % CapacityPerShard];
            if (candidate.record.secs < best.record.secs ||
                (candidate.record.secs == best.record.secs &&
                    candidate.record.micros < best.record.micros))
            {
                _shard = _candidate;
                _index = head;
            }
        };
        auto const count = m_shardCount.load(std::memory_order_acquire);
        for (std::size_t i = 0; i < count; ++i)
        {
            consider(m_shards[i].get());
        }
        consider(m_overflowShard.load(std::memory_order_acquire));
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
                std::size_t const written = consumeBatch(linesSinceFlushCheck);
                linesThisPass += written;
                if (written == 0)
                {
                    // No progress even though a ring looked non-empty: a ring
                    // whose tail the consumer has already caught up with, or an
                    // inconsistency between the enumerations. Returning beats
                    // "continue", which would spin on the same condition and burn
                    // a core.
                    return;
                }
                if (linesThisPass >= MaxLinesPerPass)
                {
                    // Bounded pass: yield so shutdown and the other sinks make
                    // progress. The remaining lines stay in their rings, and the
                    // worker has to be woken to come back for them: a burst can be
                    // fully enqueued before this pass starts, in which case no
                    // producer will call wake() again and the tail would sit in
                    // the rings until the next unrelated log statement.
                    LineFeeder::instance().wake();
                    return;
                }
                if (m_stop.load(std::memory_order_relaxed))
                {
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
        auto const hasLine = [](QueueShard const* _shard) {
            // The shared overflow ring is not part of m_shards, so it has to be
            // passed in explicitly: forgetting it stranded every line written by
            // a thread beyond MaxQueueShards.
            return _shard != nullptr && _shard->head.load(std::memory_order_relaxed) !=
                                            _shard->tail.load(std::memory_order_acquire);
        };
        auto const count = m_shardCount.load(std::memory_order_acquire);
        for (std::size_t i = 0; i < count; ++i)
        {
            if (hasLine(m_shards[i].get()))
            {
                return true;
            }
        }
        return hasLine(m_overflowShard.load(std::memory_order_acquire));
    }

    // Consumes up to batchLimitFor() lines, merged across rings so the batch is
    // in timestamp order, and writes it to the backend in one call.
    // Returns the number of lines written.
    std::size_t consumeBatch(std::uint64_t& _linesSinceFlushCheck)
    {
        thread_local std::string batch;
        batch.clear();

        std::size_t const limit = batchLimitFor();
        std::size_t written = 0;
        LogLevel firstLevel = LogLevel::INFO;
        for (std::size_t n = 0; n < limit && batch.size() < MaxBatchBytes; ++n)
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
        // A single outsized line grows the thread_local batch to its size and it
        // would stay that large for the life of the feeding thread. Give the
        // memory back once the batch is no longer representative.
        if (batch.capacity() > MaxBatchBytes * 2)
        {
            std::string().swap(batch);
        }
        return written;
    }

    /// How many lines one backend write may carry.
    ///
    /// BatchSize normally: one write for up to 32 lines is the whole point of
    /// batching. With log.flush=true it drops to 1, because the backend's
    /// auto-flush then runs per consumeLine() call — batching would turn the
    /// configured "flush after every line" into "flush after every 32", so a
    /// crash could lose up to 31 lines that were already built. Those are exactly
    /// the lines a post-mortem needs, and the old pipeline never lost them.
    std::size_t batchLimitFor() const noexcept
    {
        return m_perLinePersistence.load(std::memory_order_relaxed) ? 1 : BatchSize;
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
    /// Owns the shared overflow ring; published through m_overflowShard once it
    /// is allocated (on first fallback, never eagerly).
    std::unique_ptr<QueueShard> m_overflowOwner;
    /// Published pointer to the shared overflow ring, or null while it has not
    /// been needed. This single value is what every reader keys off, so "does this
    /// sink have an overflow ring" has exactly one answer.
    std::atomic<QueueShard*> m_overflowShard{nullptr};
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
    /// True when the operator configured log.flush=true; see batchLimitFor().
    std::atomic<bool> m_perLinePersistence{false};
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
    // Pass the record on: the prefix is rendered here, but the timestamp is still
    // what orders this line against the other producing threads.
    writeLine(_record.level, prefix, _message, &_record);
}
}  // namespace bcos::log
