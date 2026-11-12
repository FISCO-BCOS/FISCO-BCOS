/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Unit tests for the whole-line logging fast path: LineAsyncSink, the
 *        line-sink registry and the LogStream -> commitLine hand-off.
 * @file LineAsyncSinkTest.cpp
 */

#include "bcos-utilities/LineAsyncSink.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/LogStream.h"
#include <boost/smart_ptr/make_shared_object.hpp>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

using namespace std::chrono_literals;

namespace bcos::test
{
namespace
{
/// Peak resident set size in bytes. Peak, not current, so it never decreases;
/// that is enough for a one-way "did the process retain N megabytes" assertion,
/// and it needs no allocator hook (which would perturb the whole test binary).
long long peakRssBytes()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS info{};
    if (::GetProcessMemoryInfo(::GetCurrentProcess(), &info, sizeof(info)) == 0)
    {
        return 0;
    }
    return static_cast<long long>(info.PeakWorkingSetSize);
#else
    rusage usage{};
    if (::getrusage(RUSAGE_SELF, &usage) != 0)
    {
        return 0;
    }
#ifdef __APPLE__
    return static_cast<long long>(usage.ru_maxrss);  // bytes on macOS
#else
    return static_cast<long long>(usage.ru_maxrss) * 1024;  // KiB elsewhere
#endif
#endif
}

struct FakeBackend
{
    std::mutex mutex;
    std::vector<std::pair<LogLevel, std::string>> lines;
    std::size_t flushes = 0;
    // throw on the next N consumeLine / flush calls (FIB-184 simulation)
    std::atomic<int> throwOnConsume{0};
    std::atomic<int> throwOnFlush{0};
    // while false, consumeLine blocks the feeding thread (pool exhaustion)
    std::atomic<bool> gate{true};
    // Number of consumeLine entries. A test that needs to know the feeding thread
    // is parked inside the backend (rather than racing with it) waits for this to
    // advance: once a batch is being consumed, the rings are frozen for the
    // duration.
    std::atomic<int> consumeEntered{0};
    // flush() entry count and a gate to hold the worker inside flushBackend
    std::atomic<int> flushEntered{0};
    std::atomic<bool> flushGate{true};

    static bool shouldThrow(std::atomic<int>& counter)
    {
        auto n = counter.load(std::memory_order_relaxed);
        while (n > 0 && !counter.compare_exchange_weak(n, n - 1))
        {
        }
        return n > 0;
    }

    void consumeLine(LogLevel level, std::string const& line)
    {
        consumeEntered.fetch_add(1, std::memory_order_release);
        while (!gate.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
        if (shouldThrow(throwOnConsume))
        {
            throw std::runtime_error("simulated backend failure");
        }
        std::lock_guard lock(mutex);
        lines.emplace_back(level, line);
    }

    void flush()
    {
        flushEntered.fetch_add(1, std::memory_order_release);
        while (!flushGate.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
        if (shouldThrow(throwOnFlush))
        {
            throw std::runtime_error("simulated flush failure");
        }
        std::lock_guard lock(mutex);
        ++flushes;
    }

    std::vector<std::string> snapshot()
    {
        std::lock_guard lock(mutex);
        std::vector<std::string> out;
        out.reserve(lines.size());
        // The sink hands the backend a batch of lines joined by '\n' (one write
        // per batch), so expose individual lines to the assertions.
        for (auto const& [level, line] : lines)
        {
            std::size_t begin = 0;
            while (true)
            {
                auto const end = line.find('\n', begin);
                if (end == std::string::npos)
                {
                    out.emplace_back(line.substr(begin));
                    break;
                }
                out.emplace_back(line.substr(begin, end - begin));
                begin = end + 1;
            }
        }
        return out;
    }

    std::size_t flushCount()
    {
        std::lock_guard lock(mutex);
        return flushes;
    }

    // Number of consumeLine calls, i.e. backend writes (a batch is one write).
    std::size_t writeCount()
    {
        std::lock_guard lock(mutex);
        return lines.size();
    }
};

using FakeSink = bcos::log::LineAsyncSink<FakeBackend>;

// Splits "severity|timestamp|thread|message" into its four fields.
std::vector<std::string> splitFields(std::string const& line)
{
    std::vector<std::string> fields;
    std::size_t pos = 0;
    for (int i = 0; i < 3; ++i)
    {
        auto sep = line.find('|', pos);
        if (sep == std::string::npos)
        {
            return {};
        }
        fields.emplace_back(line.substr(pos, sep - pos));
        pos = sep + 1;
    }
    fields.emplace_back(line.substr(pos));
    return fields;
}

// Types and operators for the formatting tests below. They live in their own
// namespace so the operators are found by ADL exactly as a real type's would be.
namespace fmt
{
// Scoped enum WITHOUT a stream operator: must print as its underlying value.
enum class Colour : int
{
    Red = 1,
    Green = 2,
};

// Scoped enum WITH a stream operator: must print through that operator. A scoped
// enum is not implicitly convertible to its underlying type, so `os << v` is only
// valid when a real overload exists — which is what the dispatch keys off.
enum class Status : int
{
    Ok = 7,
    OutOfGas = 12,
};

inline std::ostream& operator<<(std::ostream& _out, Status _status)
{
    return _out << (_status == Status::Ok ? "Ok" : "OutOfGas");
}

// Reports the integer base the stream is currently using.
struct BaseProbe
{
};

inline std::ostream& operator<<(std::ostream& _out, BaseProbe const&)
{
    auto const base = _out.flags() & std::ios_base::basefield;
    return _out << (base == std::ios_base::hex ? "hex"
                                               : base == std::ios_base::oct ? "oct" : "dec");
}

// A sloppy user operator that switches the stream to hex and never restores it.
struct HexLeaker
{
};

inline std::ostream& operator<<(std::ostream& _out, HexLeaker const&)
{
    _out.setf(std::ios_base::hex, std::ios_base::basefield);
    return _out << "1f";
}

// Puts the stream into a failed state, as a naive operator<< could.
struct BreaksStream
{
};

inline std::ostream& operator<<(std::ostream& _out, BreaksStream const&)
{
    _out.setstate(std::ios_base::failbit);
    return _out;
}

// Plain custom type, used to observe whether the bridge still works at all.
struct Plain
{
};

inline std::ostream& operator<<(std::ostream& _out, Plain const&)
{
    return _out << "plain";
}
}  // namespace fmt

/// Formats one statement through a real LogStream + registered sink and returns
/// the message part of the logged line.
template <class Fn>
std::string loggedMessage(LogLevel _level, Fn&& _fill)
{
    auto backend = boost::make_shared<FakeBackend>();
    auto sink = std::make_shared<FakeSink>(backend);
    bcos::log::registerLineSink(sink);
    {
        bcos::LogStream stream(_level);
        _fill(stream);
    }
    sink->flush();
    bcos::log::unregisterLineSink(sink.get());
    auto const lines = backend->snapshot();
    sink->stop();
    if (lines.empty())
    {
        return {};
    }
    auto const fields = splitFields(lines.front());
    return fields.size() == 4 ? fields[3] : std::string{};
}
}  // namespace

BOOST_AUTO_TEST_SUITE(LineAsyncSinkTest)

BOOST_AUTO_TEST_CASE(prefixLayoutMatchesDocumentedFormat)
{
    auto backend = boost::make_shared<FakeBackend>();
    auto sink = std::make_shared<FakeSink>(backend);
    bcos::log::registerLineSink(sink);

    bcos::log::commitLine(
        bcos::log::sampleLineRecord(LogLevel::WARNING), "prefixLayoutMessage");
    sink->flush();
    bcos::log::unregisterLineSink(sink.get());

    auto lines = backend->snapshot();
    BOOST_REQUIRE_EQUAL(lines.size(), 1u);
    auto fields = splitFields(lines.front());
    BOOST_REQUIRE_EQUAL(fields.size(), 4u);
    BOOST_CHECK_EQUAL(fields[0], "warning");
    // YYYY-MM-DD HH:MM:SS.ffffff
    BOOST_REQUIRE_EQUAL(fields[1].size(), 26u);
    BOOST_CHECK_EQUAL(fields[1][4], '-');
    BOOST_CHECK_EQUAL(fields[1][13], ':');
    BOOST_CHECK_EQUAL(fields[1][19], '.');
    // "<threadName>-0x<tid>"
    BOOST_CHECK_NE(fields[2].find("-0x"), std::string::npos);
    BOOST_CHECK_EQUAL(fields[3], "prefixLayoutMessage");
    sink->stop();
}

BOOST_AUTO_TEST_CASE(unnamedThreadKeepsUnnamedThreadField)
{
    auto backend = boost::make_shared<FakeBackend>();
    auto sink = std::make_shared<FakeSink>(backend);
    bcos::log::registerLineSink(sink);

    std::thread worker([]() {
        bcos::pthread_setThreadName("");  // unnamed thread (always the case on Windows)
        bcos::log::commitLine(
            bcos::log::sampleLineRecord(LogLevel::INFO), "unnamedThreadMessage");
    });
    worker.join();
    sink->flush();
    bcos::log::unregisterLineSink(sink.get());

    auto lines = backend->snapshot();
    BOOST_REQUIRE_EQUAL(lines.size(), 1u);
    auto fields = splitFields(lines.front());
    BOOST_REQUIRE_EQUAL(fields.size(), 4u);
    BOOST_CHECK_EQUAL(fields[2].substr(0, 7), "Unnamed");
    BOOST_CHECK_EQUAL(fields[3], "unnamedThreadMessage");
    sink->stop();
}

BOOST_AUTO_TEST_CASE(longLineSpillsPastInlineArena)
{
    auto backend = boost::make_shared<FakeBackend>();
    auto sink = std::make_shared<FakeSink>(backend);
    bcos::log::registerLineSink(sink);

    std::string const big(2000, 'x');  // LogStream's inline arena is 512 bytes
    {
        bcos::LogStream stream(LogLevel::INFO);
        stream << big;
    }
    sink->flush();
    bcos::log::unregisterLineSink(sink.get());

    auto lines = backend->snapshot();
    BOOST_REQUIRE_EQUAL(lines.size(), 1u);
    auto fields = splitFields(lines.front());
    BOOST_REQUIRE_EQUAL(fields.size(), 4u);
    BOOST_CHECK_EQUAL(fields[3], big);
    sink->stop();
}

// The spill buffer is heap-allocated for lines over InlineCapacity and must be
// released with the statement. Asserting only the rendered text (as the case
// above does) leaves the whole suite green if the `delete[]` is ever dropped, so
// this measures peak RSS instead: 20000 statements of 2 KB would otherwise retain
// ~80 MB (roughly twice the line length each) until the process exits.
BOOST_AUTO_TEST_CASE(longLinesDoNotRetainHeap)
{
    // No sink registered: commitLine is skipped, but the statement still builds
    // (and therefore spills) its buffer, which is the allocation under test.
    std::string const big(2048, 'x');

    // Warm up so one-off allocator and page-fault costs are already paid.
    for (int i = 0; i < 256; ++i)
    {
        bcos::LogStream stream(LogLevel::INFO);
        stream << big;
    }

    long long const before = peakRssBytes();
    BOOST_REQUIRE_GT(before, 0);  // the measurement itself must work
    for (int i = 0; i < 20000; ++i)
    {
        bcos::LogStream stream(LogLevel::INFO);
        stream << big << i;
    }
    long long const after = peakRssBytes();

    // A leak grows the peak by ~80 MB. 16 MB of slack absorbs allocator behaviour
    // while still failing loudly if the buffer is retained.
    BOOST_CHECK_LT(after - before, 16LL * 1024 * 1024);
}

BOOST_AUTO_TEST_CASE(overflowDropsAreCountedReportedAndFatalSurvives)
{
    constexpr std::size_t Capacity = 8;
    auto backend = boost::make_shared<FakeBackend>();
    backend->gate = false;  // feeding thread blocks inside consumeLine holding one slot
    bcos::log::LineAsyncSink<FakeBackend, Capacity> sink(backend);

    // Park the worker inside consumeLine BEFORE the measured burst. Without this
    // the test raced the consumer: whenever the worker started its pass late it
    // swallowed the whole burst in one batch, the ring never filled and the drop
    // assertions below became a coin toss.
    sink.writeLine(LogLevel::INFO, "p|", "seed");
    auto const deadline = std::chrono::steady_clock::now() + 2s;
    while (backend->consumeEntered.load(std::memory_order_acquire) == 0 &&
           std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::yield();
    }
    BOOST_REQUIRE(backend->consumeEntered.load(std::memory_order_acquire) > 0);

    // The worker now holds "seed" and will not touch the rings until the gate
    // opens, so exactly Capacity lines fit and the rest are dropped.
    for (int i = 0; i < 12; ++i)
    {
        sink.writeLine(LogLevel::INFO, "p|", "queued");
    }
    BOOST_CHECK_EQUAL(sink.droppedLines(), 12u - Capacity);

    // FATAL bypasses the exhausted pool: it is written synchronously, so this
    // call blocks until the backend frees up but is never dropped.
    std::thread fatalWriter([&] { sink.writeLine(LogLevel::FATAL, "p|", "fatalMessage"); });
    std::this_thread::sleep_for(100ms);
    backend->gate = true;
    fatalWriter.join();
    sink.flush();

    auto lines = backend->snapshot();
    std::size_t normal = 0, fatal = 0, marker = 0;
    for (auto const& line : lines)
    {
        if (line.find("fatalMessage") != std::string::npos)
        {
            ++fatal;
        }
        else if (line.find("dropped 4 log line(s)") != std::string::npos)
        {
            ++marker;
        }
        else
        {
            ++normal;
        }
    }
    // one seed + everything the ring could hold
    BOOST_CHECK_EQUAL(normal, Capacity + 1);
    BOOST_CHECK_EQUAL(fatal, 1u);
    BOOST_CHECK_EQUAL(marker, 1u);
    BOOST_CHECK_EQUAL(sink.droppedLines(), 0u);              // reset by the marker
    BOOST_CHECK_EQUAL(sink.droppedTotal(), 12u - Capacity);  // cumulative, never reset
    sink.stop();
}

// default-capacity sinks are ~1.3 MB of slot array: keep them off the stack
BOOST_AUTO_TEST_CASE(throwingBackendNeverKillsFeedingThread)
{
    auto backend = boost::make_shared<FakeBackend>();
    // The sink writes a batch per consumeLine call, so flush after each line to
    // keep "one throwing call loses one line" meaningful.
    backend->throwOnConsume = 1;
    backend->throwOnFlush = 1;
    auto sinkPtr = std::make_shared<FakeSink>(backend);
    auto& sink = *sinkPtr;

    sink.writeLine(LogLevel::INFO, "p|", "msg0");  // this consumeLine throws
    sink.flush();                                  // the flush throw is swallowed as well
    BOOST_CHECK_EQUAL(backend->snapshot().size(), 0u);

    // the feeding thread is still alive and draining
    sink.writeLine(LogLevel::INFO, "p|", "afterThrow");
    sink.flush();
    auto lines = backend->snapshot();
    BOOST_REQUIRE_EQUAL(lines.size(), 1u);
    BOOST_CHECK(lines.back().find("afterThrow") != std::string::npos);
    sink.stop();
}

BOOST_AUTO_TEST_CASE(fatalDeliveredAfterStop)
{
    auto backend = boost::make_shared<FakeBackend>();
    auto sinkPtr = std::make_shared<FakeSink>(backend);
    auto& sink = *sinkPtr;
    sink.stop();

    sink.writeLine(LogLevel::INFO, "p|", "droppedAfterStop");
    sink.writeLine(LogLevel::FATAL, "p|", "fatalAfterStop");

    auto lines = backend->snapshot();
    // The post-stop INFO is dropped, counted, and reported as a marker line, so
    // the payload line is the FATAL one.
    std::vector<std::string> payload;
    for (auto const& line : lines)
    {
        if (line.find("[log] dropped ") == std::string::npos)
        {
            payload.push_back(line);
        }
    }
    BOOST_REQUIRE_EQUAL(payload.size(), 1u);
    BOOST_CHECK(payload.front().find("fatalAfterStop") != std::string::npos);
    BOOST_CHECK_EQUAL(sink.droppedTotal(), 1u);
}

// The drain branch must flush unconditionally: a line that lands inside the
// flood-path rate-limit window still reaches the backend without an explicit
// flush() call.
BOOST_AUTO_TEST_CASE(queueDrainFlushesWithoutExplicitFlush)
{
    auto backend = boost::make_shared<FakeBackend>();
    {
        auto sinkPtr = std::make_shared<FakeSink>(backend);
        auto& sink = *sinkPtr;
        sink.writeLine(LogLevel::INFO, "p|", "first");
        for (int i = 0; i < 200 && backend->flushCount() == 0; ++i)
        {
            std::this_thread::sleep_for(10ms);
        }
        BOOST_REQUIRE_GE(backend->flushCount(), 1u);
        auto const flushesAfterFirst = backend->flushCount();

        sink.writeLine(LogLevel::INFO, "p|", "second");
        for (int i = 0; i < 200 && backend->flushCount() == flushesAfterFirst; ++i)
        {
            std::this_thread::sleep_for(10ms);
        }
        BOOST_CHECK_GT(backend->flushCount(), flushesAfterFirst);
        sink.stop();
    }
    auto lines = backend->snapshot();
    BOOST_REQUIRE_EQUAL(lines.size(), 2u);
    BOOST_CHECK(lines.back().find("second") != std::string::npos);
}

// Regression for the flush handshake: a flush() raised while the worker is
// inside flushBackend() must not be satisfied by that in-flight pass — its
// own lines may still be queued. The worker acknowledges only the request
// generation it snapshotted; a later request waits for the next pass (which
// its own wake() guarantees).
BOOST_AUTO_TEST_CASE(flushRaisedMidPassIsNotSatisfiedByTheInflightFlush)
{
    auto backend = boost::make_shared<FakeBackend>();
    auto sinkPtr = std::make_shared<FakeSink>(backend);
    auto& sink = *sinkPtr;

    sink.writeLine(LogLevel::INFO, "p|", "warmup");
    sink.flush();  // returns once the worker is idle

    backend->flushGate = false;
    auto const flushesBefore = backend->flushEntered.load(std::memory_order_acquire);

    // First request: the worker snapshots its generation, then blocks inside
    // the gated backend flush.
    std::thread first([&] { sink.flush(); });
    for (int i = 0;
        i < 500 && backend->flushEntered.load(std::memory_order_acquire) == flushesBefore; ++i)
    {
        std::this_thread::sleep_for(1ms);
    }
    BOOST_REQUIRE_GT(backend->flushEntered.load(std::memory_order_acquire), flushesBefore);

    // Raised while the in-flight flush is still blocked: must not return
    // until a later pass has fed lateLine to the backend.
    sink.writeLine(LogLevel::INFO, "p|", "lateLine");
    bool lateLineFedBeforeReturn = false;
    std::thread second([&] {
        sink.flush();
        auto const lines = backend->snapshot();
        lateLineFedBeforeReturn = std::any_of(lines.begin(), lines.end(),
            [](auto const& line) { return line.find("lateLine") != std::string::npos; });
    });
    std::this_thread::sleep_for(50ms);  // let the second request register
    backend->flushGate = true;
    first.join();
    second.join();
    BOOST_CHECK(lateLineFedBeforeReturn);
    sink.stop();
}

// A FATAL first drains the queued backlog, so the lines logged before it keep
// their ordering ahead of the abort (base behaviour of the single FIFO).
BOOST_AUTO_TEST_CASE(fatalDrainsQueuedBacklogBeforeWriting)
{
    auto backend = boost::make_shared<FakeBackend>();
    backend->gate = false;  // hold the feeding thread inside consumeLine
    auto sinkPtr = std::make_shared<FakeSink>(backend);
    auto& sink = *sinkPtr;
    for (int i = 0; i < 3; ++i)
    {
        sink.writeLine(LogLevel::INFO, "p|", "queued" + std::to_string(i));
    }
    std::thread fatalWriter([&] { sink.writeLine(LogLevel::FATAL, "p|", "fatalLine"); });
    std::this_thread::sleep_for(100ms);
    backend->gate = true;
    fatalWriter.join();

    auto lines = backend->snapshot();
    BOOST_REQUIRE_EQUAL(lines.size(), 4u);
    BOOST_CHECK(lines.back().find("fatalLine") != std::string::npos);
    sink.stop();
}

// Multi-producer stress for the hand-off protocol: slot-pool accounting, drop
// counting and the wake/notify pairing under genuine contention, with a small
// Capacity so the pool genuinely overflows. The pinned invariant is
// conservation — every line is consumed exactly once (unique and untruncated)
// or counted as dropped. This is the case to run under ThreadSanitizer.
BOOST_AUTO_TEST_CASE(multiProducerConservationUnderOverflow)
{
    constexpr std::size_t Capacity = 64;
    constexpr unsigned Producers = 8;
    constexpr unsigned LinesPerProducer = 20000;
    auto backend = boost::make_shared<FakeBackend>();
    bcos::log::LineAsyncSink<FakeBackend, Capacity> sink(backend);

    std::vector<std::thread> producers;
    producers.reserve(Producers);
    for (unsigned p = 0; p < Producers; ++p)
    {
        producers.emplace_back([p, &sink] {
            for (unsigned i = 0; i < LinesPerProducer; ++i)
            {
                sink.writeLine(LogLevel::INFO, "p|", std::to_string(p) + "-" + std::to_string(i));
            }
        });
    }
    for (auto& producer : producers)
    {
        producer.join();
    }
    sink.flush();

    auto lines = backend->snapshot();
    std::unordered_set<std::string> seen;
    seen.reserve(static_cast<std::size_t>(Producers) * LinesPerProducer);
    std::uint64_t droppedByMarker = 0;
    for (auto const& line : lines)
    {
        // dropped-lines marker: "...[log] dropped N log line(s): ..."
        if (line.find("[log] dropped ") != std::string::npos)
        {
            auto const begin = line.find("dropped ") + 8;
            auto const end = line.find(" log line(s)", begin);
            droppedByMarker += std::stoull(line.substr(begin, end - begin));
            continue;
        }
        // a duplicated line breaks the slot-pool accounting
        BOOST_REQUIRE(seen.insert(line).second);
        // a truncated or corrupted line fails the "p|<p>-<i>" layout
        auto const sep = line.find('-', 2);
        BOOST_REQUIRE_NE(sep, std::string::npos);
        BOOST_REQUIRE_LT(sep + 1, line.size());
        BOOST_CHECK_LT(std::stoul(line.substr(2, sep - 2)), Producers);
        BOOST_CHECK_LT(std::stoul(line.substr(sep + 1)), LinesPerProducer);
    }
    auto const total = static_cast<std::uint64_t>(Producers) * LinesPerProducer;
    BOOST_CHECK_EQUAL(seen.size() + droppedByMarker + sink.droppedLines(), total);
    sink.stop();
}

// F3: the custom log.format path renders the whole line on the producing thread
// and hands it over as prefix + empty message. That path used to leave the queue
// slot's LineRecord default-constructed, so every ring compared equal and the
// merge silently degraded to ring-index order. This asserts the merge still works
// there: the line that was STAMPED EARLIER must come out first even though it was
// enqueued LATER and lives in a higher-numbered ring.
BOOST_AUTO_TEST_CASE(layoutPathLinesAreMergedByTimestamp)
{
    constexpr std::size_t Capacity = 64;
    auto backend = boost::make_shared<FakeBackend>();
    backend->gate = false;
    bcos::log::LineAsyncSink<FakeBackend, Capacity> sink(backend);

    // Park the worker so nothing is consumed until both lines are published.
    bcos::LineRecord seedRecord;
    seedRecord.secs = std::chrono::duration_cast<std::chrono::seconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
    sink.writeLine(LogLevel::INFO, "seed|", "", &seedRecord);
    auto const deadline = std::chrono::steady_clock::now() + 2s;
    while (backend->consumeEntered.load(std::memory_order_acquire) < 1 &&
           std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::yield();
    }
    BOOST_REQUIRE_GE(backend->consumeEntered.load(std::memory_order_acquire), 1);

    // Thread A claims ring 0 and stamps a LATER time; thread B claims ring 1 and
    // stamps an EARLIER time but publishes second. Ring-index order would emit
    // A then B; only a timestamp merge emits B then A.
    //
    // Both threads must stay alive until both have written: a thread that exits
    // releases its ring for reuse, and B would then take over A's ring, putting
    // both lines in one ring where the order is plain FIFO.
    bcos::LineRecord later;
    later.secs = 2'000'000;
    later.micros = 0;
    bcos::LineRecord earlier;
    earlier.secs = 1'000'000;
    earlier.micros = 0;

    std::atomic<bool> aPublished{false};
    std::atomic<bool> aMayExit{false};
    std::thread a([&] {
        sink.writeLine(LogLevel::INFO, "A-newer|", "", &later);
        aPublished.store(true, std::memory_order_release);
        while (!aMayExit.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
    });
    while (!aPublished.load(std::memory_order_acquire))
    {
        std::this_thread::yield();
    }
    std::thread b([&] { sink.writeLine(LogLevel::INFO, "B-older|", "", &earlier); });
    b.join();
    aMayExit.store(true, std::memory_order_release);
    a.join();

    backend->gate = true;
    sink.flush();

    auto lines = backend->snapshot();
    BOOST_REQUIRE_EQUAL(lines.size(), 3u);
    // The seed was already inside the parked consumeLine call, so it is written
    // first. The point of the test is the next two: B was stamped earlier but
    // published later, and it must still come out first.
    BOOST_CHECK_EQUAL(lines[0], "seed|");
    BOOST_CHECK_EQUAL(lines[1], "B-older|");
    BOOST_CHECK_EQUAL(lines[2], "A-newer|");
    sink.stop();
}

// morebtcg #2: a producer that is already past its own stop check when stop() runs
// used to publish into a ring the worker had detached from, so the line was
// neither consumed nor counted. Building a large message widens that window from
// nanoseconds to milliseconds, which is how the reviewer reproduced it.
BOOST_AUTO_TEST_CASE(stopRacingAnInFlightProducerLosesNothing)
{
    auto backend = boost::make_shared<FakeBackend>();
    backend->gate = false;
    bcos::log::LineAsyncSink<FakeBackend, 64> sink(backend);

    // Park the worker so the backlog stays in the ring.
    sink.writeLine(LogLevel::INFO, "p|", "seed");
    auto const deadline = std::chrono::steady_clock::now() + 2s;
    while (backend->consumeEntered.load(std::memory_order_acquire) < 1 &&
           std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::yield();
    }
    BOOST_REQUIRE_GE(backend->consumeEntered.load(std::memory_order_acquire), 1);

    // The copy must still be running when stop() begins, otherwise the line is
    // published and drained by stop()'s own drain and the post-publish m_stop
    // re-check is never load-bearing — with an 8 MB line the copy finished in
    // 1-3 ms while this thread slept 50 ms, so disabling the re-check kept the
    // case green. 128 MB takes roughly 40-80 ms, and the wait below is 1 ms, so
    // stop() reliably lands while this thread is inside the copy. Verified by
    // mutation: with the re-check disabled this now leaves the line stranded.
    std::string const huge(128u * 1024u * 1024u, 'x');
    std::thread producer([&] { sink.writeLine(LogLevel::INFO, "p|", huge); });
    // Just long enough for the producer to be past its stop check and inside the
    // copy; far shorter than the copy itself.
    std::this_thread::sleep_for(1ms);

    backend->gate = true;
    sink.stop();
    producer.join();

    // Whatever the interleaving, the line must not stay invisible in a ring.
    BOOST_CHECK_EQUAL(sink.pendingLines(), 0u);
    auto const delivered = backend->snapshot().size();
    BOOST_CHECK_GE(delivered + sink.droppedTotal(), 2u);  // seed + the racing line
}

// morebtcg #3: once a pass hits MaxLinesPerPass it must wake itself. A burst can
// be fully enqueued before the pass starts, so no producer will wake the worker
// again and the tail would sit there until the next unrelated log statement.
//
// The bound is shrunk to a handful of lines and the worker is parked inside the
// backend's flush() — i.e. at the END of a pass, after it already cleared the
// pending-work flag — so the state is exact: the producer's wake is the only one,
// the next pass clears it, and nothing re-raises it afterwards. That is precisely
// the interleaving in which a missing self-wake strands the remainder.
BOOST_AUTO_TEST_CASE(boundedPassRewakesItselfAndDrainsTheRest)
{
    constexpr std::size_t LinesPerPass = 8;
    constexpr int TotalLines = 200;
    auto backend = boost::make_shared<FakeBackend>();
    backend->flushGate = false;
    bcos::log::LineAsyncSink<FakeBackend, 1024, 128, LinesPerPass> sink(backend);

    // Seed one line so the worker runs a pass and parks in flush().
    sink.writeLine(LogLevel::INFO, "p|", "seed");
    auto const parkDeadline = std::chrono::steady_clock::now() + 2s;
    while (backend->flushEntered.load(std::memory_order_acquire) == 0 &&
           std::chrono::steady_clock::now() < parkDeadline)
    {
        std::this_thread::yield();
    }
    BOOST_REQUIRE_GT(backend->flushEntered.load(std::memory_order_acquire), 0);

    // Queue far more than one pass can drain. The worker is parked, so the whole
    // burst is enqueued before the pass that will hit the bound.
    for (int i = 0; i < TotalLines; ++i)
    {
        sink.writeLine(LogLevel::INFO, "p|", "line");
    }
    BOOST_REQUIRE_EQUAL(sink.pendingLines(), static_cast<std::size_t>(TotalLines));

    // Release the worker and wait WITHOUT calling flush()/wake(): either would
    // wake the feeder and hide a missing self-wake.
    backend->flushGate = true;
    auto const drainDeadline = std::chrono::steady_clock::now() + 10s;
    while (sink.pendingLines() != 0 && std::chrono::steady_clock::now() < drainDeadline)
    {
        std::this_thread::sleep_for(5ms);
    }
    BOOST_CHECK_EQUAL(sink.pendingLines(), 0u);
    // The seed plus the whole burst reached the backend.
    BOOST_CHECK_EQUAL(backend->snapshot().size(), static_cast<std::size_t>(TotalLines) + 1);
    sink.stop();
}

// F5: log.flush=true must stay per-line. Batching turns it into "durable in groups
// of 32", which would lose up to 31 already-built lines on a crash.
BOOST_AUTO_TEST_CASE(perLinePersistenceDisablesBatching)
{
    auto backend = boost::make_shared<FakeBackend>();
    bcos::log::LineAsyncSink<FakeBackend, 64> sink(backend);
    sink.setPerLinePersistence(true);

    for (int i = 0; i < 8; ++i)
    {
        sink.writeLine(LogLevel::INFO, "p|", "line" + std::to_string(i));
    }
    sink.flush();

    // One consumeLine call per line: 8 backend writes, not 1 joined batch.
    BOOST_CHECK_EQUAL(backend->writeCount(), 8u);
    for (auto const& line : backend->snapshot())
    {
        BOOST_CHECK_EQUAL(line.find('\n'), std::string::npos);
    }
    sink.stop();
}

// morebtcg #1: an enum with its own operator<< must be formatted by that operator.
// Unconditional enum -> underlying conversion silently bypassed it, so
// TransactionStatus::OutOfGas printed as "12" and BlockVersion as an integer.
BOOST_AUTO_TEST_CASE(enumWithStreamOperatorUsesIt)
{
    BOOST_CHECK_EQUAL(
        loggedMessage(LogLevel::INFO, [](bcos::LogStream& s) { s << fmt::Status::OutOfGas; }),
        "OutOfGas");
    BOOST_CHECK_EQUAL(
        loggedMessage(LogLevel::INFO, [](bcos::LogStream& s) { s << fmt::Status::Ok; }), "Ok");
}

// The complement: an enum with no operator of its own still prints its numeric
// value, which is what the old pipeline produced via integral promotion.
BOOST_AUTO_TEST_CASE(enumWithoutStreamOperatorPrintsValue)
{
    BOOST_CHECK_EQUAL(
        loggedMessage(LogLevel::INFO, [](bcos::LogStream& s) { s << fmt::Colour::Green; }), "2");
}

// F6: the std::ostream bridge is thread_local, so it must present the state the old
// single-stream pipeline would have shown: the base selected by earlier operands of
// the SAME statement, and nothing left over from a previous statement.
BOOST_AUTO_TEST_CASE(bridgeSeesTheStatementsBase)
{
    BOOST_CHECK_EQUAL(
        loggedMessage(LogLevel::INFO, [](bcos::LogStream& s) { s << fmt::BaseProbe{}; }), "dec");
    BOOST_CHECK_EQUAL(loggedMessage(LogLevel::INFO,
                         [](bcos::LogStream& s) { s << std::hex << fmt::BaseProbe{}; }),
        "hex");
    BOOST_CHECK_EQUAL(loggedMessage(LogLevel::INFO,
                         [](bcos::LogStream& s) { s << std::oct << fmt::BaseProbe{}; }),
        "oct");
}

BOOST_AUTO_TEST_CASE(bridgeStateDoesNotLeakBetweenStatements)
{
    // A sloppy operator leaves the bridge in hex. The next statement must not
    // inherit that: an integer operand is formatted by LogStream itself, which is
    // unaffected, but a bridged operand must see the default base again.
    BOOST_CHECK_EQUAL(
        loggedMessage(LogLevel::INFO, [](bcos::LogStream& s) { s << fmt::HexLeaker{}; }), "1f");
    BOOST_CHECK_EQUAL(
        loggedMessage(LogLevel::INFO, [](bcos::LogStream& s) { s << fmt::BaseProbe{}; }), "dec");
    BOOST_CHECK_EQUAL(
        loggedMessage(LogLevel::INFO, [](bcos::LogStream& s) { s << 31; }), "31");
}

// Within one statement a custom operator's flags must survive for the operands
// that follow it, exactly as they did when the whole line went through a single
// std::ostream. Resetting the bridge per operand wiped them, so the operand after
// `HexLeaker{}` printed in the wrong base — and because the bridge's base was
// never folded back, the fast path after it disagreed too.
BOOST_AUTO_TEST_CASE(customOperatorFlagsComposeWithinOneStatement)
{
    // Old pipeline: "1f" + hex-seeing probe + 255 in hex.
    BOOST_CHECK_EQUAL(loggedMessage(LogLevel::INFO,
                         [](bcos::LogStream& s) { s << fmt::HexLeaker{} << fmt::BaseProbe{}; }),
        "1fhex");
    // The base a custom operator set must reach the fast path as well.
    BOOST_CHECK_EQUAL(loggedMessage(LogLevel::INFO,
                         [](bcos::LogStream& s) { s << fmt::HexLeaker{} << 255; }),
        "1fff");
    // And an explicit manipulator must keep applying after a bridged operand.
    BOOST_CHECK_EQUAL(loggedMessage(LogLevel::INFO,
                         [](bcos::LogStream& s) { s << fmt::Plain{} << std::hex << fmt::BaseProbe{}; }),
        "plainhex");
}

// A custom operator that leaves the stream in a failed state must not silence the
// rest of the log pipeline for the life of the thread: the bridge is never
// destroyed, so a sticky failbit would make every later insertion a no-op.
BOOST_AUTO_TEST_CASE(failedBridgeOperandDoesNotSilenceLaterOnes)
{
    BOOST_CHECK_EQUAL(loggedMessage(LogLevel::INFO,
                         [](bcos::LogStream& s) { s << fmt::BreaksStream{} << fmt::Plain{}; }),
        "plain");
    // ...and the next statement on the same thread is unaffected too.
    BOOST_CHECK_EQUAL(
        loggedMessage(LogLevel::INFO, [](bcos::LogStream& s) { s << fmt::Plain{}; }), "plain");
}

// F4: a thread that exits releases its ring for reuse, but only once that ring is
// EMPTY. Handing a new producer a ring that still holds the previous owner's
// undrained lines would present it as already full, so its first lines would be
// dropped silently — a bug this guards against.
BOOST_AUTO_TEST_CASE(reclaimedRingMustBeEmptyBeforeReuse)
{
    constexpr std::size_t Capacity = 64;
    auto backend = boost::make_shared<FakeBackend>();
    backend->gate = false;
    // Three rings: one for the seed, one for the thread that exits, one for the
    // thread that must get a full ring.
    bcos::log::LineAsyncSink<FakeBackend, Capacity, 3> sink(backend);

    sink.writeLine(LogLevel::INFO, "p|", "seed");
    auto const deadline = std::chrono::steady_clock::now() + 2s;
    while (backend->consumeEntered.load(std::memory_order_acquire) < 1 &&
           std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::yield();
    }
    BOOST_REQUIRE_GE(backend->consumeEntered.load(std::memory_order_acquire), 1);

    // This thread claims a ring, fills it half way and exits, so the ring is left
    // non-empty and its owner dead.
    std::thread first([&] {
        for (std::size_t i = 0; i < Capacity / 2; ++i)
        {
            sink.writeLine(LogLevel::INFO, "p|", "old");
        }
    });
    first.join();

    // A new thread must get a ring with its full capacity, not the half-full one.
    std::thread second([&] {
        for (std::size_t i = 0; i < Capacity; ++i)
        {
            sink.writeLine(LogLevel::INFO, "p|", "new");
        }
    });
    second.join();

    BOOST_CHECK_EQUAL(sink.droppedTotal(), 0u);
    backend->gate = true;
    sink.flush();
    sink.stop();
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
