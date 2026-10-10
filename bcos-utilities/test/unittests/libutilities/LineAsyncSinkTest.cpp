/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Unit tests for the whole-line logging fast path: LineAsyncSink, the
 *        line-sink registry and the LogStream -> commitLine hand-off.
 * @file LineAsyncSinkTest.cpp
 */

#include "bcos-utilities/Common.h"
#include "bcos-utilities/LineAsyncSink.h"
#include "bcos-utilities/LogStream.h"
#include <boost/smart_ptr/make_shared_object.hpp>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
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

    std::thread worker(
        []()
        {
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
    std::thread fatalWriter(
        [&] { sink.writeLine(LogLevel::FATAL, "p|", "fatalMessage"); });
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
    BOOST_CHECK_EQUAL(sink.droppedLines(), 0u);  // reset by the marker
    sink.stop();
}

BOOST_AUTO_TEST_CASE(throwingBackendNeverKillsFeedingThread)
{
    auto backend = boost::make_shared<FakeBackend>();
    backend->throwOnConsume = 2;
    backend->throwOnFlush = 1;
    FakeSink sink(backend);

    for (int i = 0; i < 3; ++i)
    {
        sink.writeLine(LogLevel::INFO, "p|", "msg" + std::to_string(i));
    }
    sink.flush();  // the flush throw is swallowed as well
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
    FakeSink sink(backend);
    sink.stop();

    sink.writeLine(LogLevel::INFO, "p|", "droppedAfterStop");
    sink.writeLine(LogLevel::FATAL, "p|", "fatalAfterStop");

    auto lines = backend->snapshot();
    BOOST_REQUIRE_EQUAL(lines.size(), 1u);
    BOOST_CHECK(lines.front().find("fatalAfterStop") != std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
