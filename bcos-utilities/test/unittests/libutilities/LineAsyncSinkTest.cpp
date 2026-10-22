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

using namespace std::chrono_literals;

namespace bcos::test
{
namespace
{
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
        for (auto const& [level, line] : lines)
        {
            out.push_back(line);
        }
        return out;
    }

    std::size_t flushCount()
    {
        std::lock_guard lock(mutex);
        return flushes;
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
}  // namespace

BOOST_AUTO_TEST_SUITE(LineAsyncSinkTest)

BOOST_AUTO_TEST_CASE(prefixLayoutMatchesDocumentedFormat)
{
    auto backend = boost::make_shared<FakeBackend>();
    auto sink = std::make_shared<FakeSink>(backend);
    bcos::log::registerLineSink(sink);

    bcos::log::commitLine(LogLevel::WARNING, "prefixLayoutMessage");
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
        bcos::log::commitLine(LogLevel::INFO, "unnamedThreadMessage");
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

BOOST_AUTO_TEST_CASE(overflowDropsAreCountedReportedAndFatalSurvives)
{
    constexpr std::size_t Capacity = 8;
    auto backend = boost::make_shared<FakeBackend>();
    backend->gate = false;  // feeding thread blocks inside consumeLine holding one slot
    bcos::log::LineAsyncSink<FakeBackend, Capacity> sink(backend);

    // 1 slot blocked in the backend + (Capacity - 1) queued + the rest dropped
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
    BOOST_CHECK_EQUAL(normal, Capacity);
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
    backend->throwOnConsume = 2;
    backend->throwOnFlush = 1;
    auto sinkPtr = std::make_shared<FakeSink>(backend);
    auto& sink = *sinkPtr;

    for (int i = 0; i < 3; ++i)
    {
        sink.writeLine(LogLevel::INFO, "p|", "msg" + std::to_string(i));
    }
    sink.flush();                                       // the flush throw is swallowed as well
    BOOST_CHECK_EQUAL(backend->snapshot().size(), 1u);  // two consumeLine throws swallowed

    // the feeding thread is still alive and draining
    sink.writeLine(LogLevel::INFO, "p|", "afterThrow");
    sink.flush();
    auto lines = backend->snapshot();
    BOOST_REQUIRE_EQUAL(lines.size(), 2u);
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
    BOOST_REQUIRE_EQUAL(lines.size(), 1u);
    BOOST_CHECK(lines.front().find("fatalAfterStop") != std::string::npos);
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

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
