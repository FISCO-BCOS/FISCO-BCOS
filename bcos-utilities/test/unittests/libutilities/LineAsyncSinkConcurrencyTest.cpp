/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Concurrency and edge-case unit tests for the whole-line async sink.
 *
 *  These target the lock-removal work on LineAsyncSink and the sink registry:
 *    - per-thread rings: several producers must not lose, duplicate or corrupt
 *      lines, including when there are more producing threads than rings (the
 *      shared overflow ring);
 *    - wake protocol: a bounded pass must let stop() and other sinks make
 *      progress while producers keep writing (a regression that used to hang
 *      shutdown under a log flood);
 *    - drop accounting: droppedTotal() must match the lines that never reached
 *      the backend, counted once;
 *    - registry: lock-free publish/unpublish must not tear while producers log.
 *
 *  The sinks here are instantiated with a small ring count and capacity so the
 *  tests stay fast; production uses the defaults (128 x 4096).
 *
 * @file LineAsyncSinkConcurrencyTest.cpp
 */

#include "bcos-utilities/LineAsyncSink.h"
#include "bcos-utilities/LogStream.h"
#include <boost/smart_ptr/make_shared_object.hpp>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <atomic>
#include <functional>
#include <chrono>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace bcos::test
{
namespace
{
using namespace std::chrono_literals;

// Collects every line it is given. Optional per-call delay stands in for slow
// disk IO so the producer can outrun the worker.
class CollectingBackend
{
public:
    explicit CollectingBackend(std::chrono::microseconds delay = 0us) : m_delay(delay) {}

    void consumeLine(bcos::LogLevel /*level*/, std::string const& _line)
    {
        if (m_delay.count() > 0)
        {
            std::this_thread::sleep_for(m_delay);
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        // The sink hands the backend a batch of lines joined by '\n' (one write
        // per batch), so split it here to observe individual lines.
        std::size_t begin = 0;
        while (true)
        {
            auto const end = _line.find('\n', begin);
            if (end == std::string::npos)
            {
                m_lines.emplace_back(_line.substr(begin));
                break;
            }
            m_lines.emplace_back(_line.substr(begin, end - begin));
            begin = end + 1;
        }
    }

    void flush() { m_flushCount.fetch_add(1, std::memory_order_relaxed); }

    std::vector<std::string> take()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<std::string> payload;
        payload.reserve(m_lines.size());
        for (auto& line : m_lines)
        {
            // Drop markers are emitted with the normal line prefix, so they are
            // not payload lines; keep them out of the accounting below.
            if (line.find("[log] dropped ") != std::string::npos)
            {
                ++m_dropMarkers;
                continue;
            }
            payload.push_back(std::move(line));
        }
        m_lines.clear();
        return payload;
    }

    /// Number of "[log] dropped N log line(s)" markers seen so far.
    std::size_t dropMarkers()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_dropMarkers;
    }

    std::uint64_t flushCount() const { return m_flushCount.load(std::memory_order_relaxed); }

private:
    std::chrono::microseconds m_delay;
    std::mutex m_mutex;
    std::vector<std::string> m_lines;
    std::size_t m_dropMarkers = 0;
    std::atomic<std::uint64_t> m_flushCount{0};
};

// Small sink: few rings and slots, so overflow and ring sharing are reachable.
template <std::size_t Capacity = 64, std::size_t Shards = 4>
using SmallSink = bcos::log::LineAsyncSink<CollectingBackend, Capacity, Shards>;

/// Counts how many times each expected tag appears in _lines, and reports tags
/// that appeared with a wrong multiplicity.
std::multiset<std::string> tagCounts(std::vector<std::string> const& _lines)
{
    std::multiset<std::string> counts;
    for (auto const& line : _lines)
    {
        counts.insert(line);
    }
    return counts;
}

/// Runs _fn on a helper thread and returns false when it does not finish within
/// _timeout. On timeout the caller is expected to unblock the work (the helper
/// thread is joined by the caller afterwards).
bool finishesWithin(std::chrono::milliseconds _timeout, std::function<void()> _fn,
    std::thread& _worker, std::atomic<bool>& _done)
{
    _worker = std::thread([fn = std::move(_fn), &_done] {
        fn();
        _done.store(true, std::memory_order_release);
    });
    auto const deadline = std::chrono::steady_clock::now() + _timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (_done.load(std::memory_order_acquire))
        {
            _worker.join();
            return true;
        }
        std::this_thread::sleep_for(1ms);
    }
    return false;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(LineAsyncSinkConcurrencyTest)

// Several producers, one worker: every line must arrive exactly once. This is
// the basic contract the per-thread rings have to preserve.
BOOST_AUTO_TEST_CASE(concurrentProducersDeliverEveryLineExactlyOnce)
{
    constexpr int kThreads = 8;
    constexpr int kLinesPerThread = 300;
    // Capacity per ring exceeds the per-thread burst, so nothing may be dropped
    // and the "every line exactly once" assertion is deterministic.
    auto sink = std::make_shared<SmallSink<1024, 8>>();

    std::atomic<int> ready{0};
    std::vector<std::thread> producers;
    producers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t)
    {
        producers.emplace_back([&, t] {
            ++ready;
            while (ready.load() != kThreads)
            {
                std::this_thread::yield();
            }
            for (int i = 0; i < kLinesPerThread; ++i)
            {
                sink->writeLine(bcos::LogLevel::INFO, "", "t" + std::to_string(t) + "-" +
                                                             std::to_string(i));
            }
        });
    }
    for (auto& p : producers)
    {
        p.join();
    }
    sink->flush();

    auto lines = sink->locked_backend()->take();
    BOOST_REQUIRE_EQUAL(lines.size(), static_cast<std::size_t>(kThreads * kLinesPerThread));
    BOOST_CHECK_EQUAL(sink->droppedTotal(), 0U);

    auto counts = tagCounts(lines);
    for (int t = 0; t < kThreads; ++t)
    {
        for (int i = 0; i < kLinesPerThread; ++i)
        {
            auto const tag = "t" + std::to_string(t) + "-" + std::to_string(i);
            BOOST_REQUIRE_MESSAGE(counts.count(tag) == 1, "line " << tag << " seen "
                                                                 << counts.count(tag)
                                                                 << " times");
        }
    }
}

// More producing threads than rings: the surplus threads share the overflow
// ring, which must serialise rather than corrupt a single-producer ring.
BOOST_AUTO_TEST_CASE(moreProducersThanRingsStayCorrect)
{
    constexpr int kThreads = 8;  // two private rings + one shared overflow ring
    constexpr int kLinesPerThread = 200;
    auto sink = std::make_shared<SmallSink<256, 2>>();

    std::atomic<int> ready{0};
    std::vector<std::thread> producers;
    for (int t = 0; t < kThreads; ++t)
    {
        producers.emplace_back([&, t] {
            ++ready;
            while (ready.load() != kThreads)
            {
                std::this_thread::yield();
            }
            for (int i = 0; i < kLinesPerThread; ++i)
            {
                sink->writeLine(bcos::LogLevel::INFO, "", "s" + std::to_string(t) + "-" +
                                                             std::to_string(i));
            }
        });
    }
    for (auto& p : producers)
    {
        p.join();
    }
    sink->flush();

    auto lines = sink->locked_backend()->take();
    auto counts = tagCounts(lines);
    // Every line that was not dropped must be complete and unique: a corrupted
    // shared ring shows up as duplicated or missing tags.
    for (int t = 0; t < kThreads; ++t)
    {
        for (int i = 0; i < kLinesPerThread; ++i)
        {
            auto const tag = "s" + std::to_string(t) + "-" + std::to_string(i);
            BOOST_REQUIRE_MESSAGE(
                counts.count(tag) <= 1, "line " << tag << " duplicated " << counts.count(tag));
        }
    }
    BOOST_CHECK_EQUAL(lines.size() + sink->droppedTotal(),
        static_cast<std::size_t>(kThreads * kLinesPerThread));
}

// Regression: stop() used to wait for the worker to drain "until empty", which
// never happens while producers keep the rings fed. Shutdown must complete.
BOOST_AUTO_TEST_CASE(stopReturnsWhileProducersAreFlooding)
{
    auto backend = boost::make_shared<CollectingBackend>(50us);
    auto sink = std::make_shared<SmallSink<64, 8>>(backend);

    std::atomic<bool> stopProducing{false};
    std::atomic<uint64_t> produced{0};
    std::atomic<int> ready{0};
    constexpr int kThreads = 6;
    std::vector<std::thread> producers;
    for (int t = 0; t < kThreads; ++t)
    {
        producers.emplace_back([&, t] {
            ++ready;
            while (ready.load() != kThreads)
            {
                std::this_thread::yield();
            }
            while (!stopProducing.load(std::memory_order_relaxed))
            {
                sink->writeLine(bcos::LogLevel::INFO, "", "f" + std::to_string(t) + "-" +
                                                             std::to_string(produced.load()));
                produced.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    // Let the flood build up so the worker is definitely mid-drain.
    std::this_thread::sleep_for(50ms);

    std::thread stopper;
    std::atomic<bool> stopped{false};
    bool const returned = finishesWithin(10s, [&] { sink->stop(); }, stopper, stopped);
    // Unblock the producers either way, so a failure does not wedge the suite.
    stopProducing.store(true, std::memory_order_relaxed);
    if (!returned)
    {
        stopper.join();
    }
    for (auto& p : producers)
    {
        p.join();
    }
    BOOST_REQUIRE_MESSAGE(returned, "stop() did not return within 10s under a log flood");
    BOOST_CHECK_GT(produced.load(), 0U);
}

// A full ring must count each dropped line exactly once, and the count must
// match the lines the backend never saw.
BOOST_AUTO_TEST_CASE(overflowCountsEachDropOnce)
{
    constexpr std::size_t kCapacity = 32;
    constexpr int kLines = 400;
    // Slow backend so the consumer cannot keep up and the ring fills.
    auto backend = boost::make_shared<CollectingBackend>(2ms);
    auto sink = std::make_shared<SmallSink<kCapacity, 2>>(backend);

    for (int i = 0; i < kLines; ++i)
    {
        sink->writeLine(bcos::LogLevel::INFO, "", "o" + std::to_string(i));
    }
    sink->stop();  // drains what is left

    auto lines = sink->locked_backend()->take();
    auto const dropped = sink->droppedTotal();
    BOOST_CHECK_GT(dropped, 0U);
    BOOST_CHECK_EQUAL(lines.size() + dropped, static_cast<std::size_t>(kLines));
    // No line may be truncated or interleaved.
    for (auto const& line : lines)
    {
        BOOST_REQUIRE_EQUAL(line.rfind('o'), 0U);
    }
}

// Lines handed in after stop() must be counted, not silently swallowed: the
// registry keeps sinks alive, so a late producer can legitimately reach here.
BOOST_AUTO_TEST_CASE(linesAfterStopAreCountedAsDropped)
{
    auto sink = std::make_shared<SmallSink<32, 2>>();
    sink->writeLine(bcos::LogLevel::INFO, "", "before");
    sink->stop();
    auto const droppedBefore = sink->droppedTotal();

    sink->writeLine(bcos::LogLevel::INFO, "", "after-stop");
    BOOST_CHECK_EQUAL(sink->droppedTotal(), droppedBefore + 1);
    // stop() is idempotent and must not crash on a second call.
    sink->stop();
    BOOST_CHECK_EQUAL(sink->droppedTotal(), droppedBefore + 1);
}

// flush() from a producer thread while others write: must not hang, and must
// acknowledge only after the lines enqueued before it reached the backend.
BOOST_AUTO_TEST_CASE(flushWhileProducersWrite)
{
    auto backend = boost::make_shared<CollectingBackend>();
    auto sink = std::make_shared<SmallSink<256, 8>>(backend);

    std::atomic<bool> done{false};
    std::atomic<int> ready{0};
    constexpr int kThreads = 4;
    std::vector<std::thread> producers;
    for (int t = 0; t < kThreads; ++t)
    {
        producers.emplace_back([&, t] {
            ++ready;
            while (ready.load() != kThreads)
            {
                std::this_thread::yield();
            }
            for (int i = 0; i < 200; ++i)
            {
                sink->writeLine(bcos::LogLevel::INFO, "", "q" + std::to_string(t) + "-" +
                                                             std::to_string(i));
            }
        });
    }

    std::thread flusher;
    std::atomic<bool> flushed{false};
    bool const ok = finishesWithin(15s, [&] {
        // A bounded number of flush() calls, spaced out: calling flush() in a
        // tight loop under a flood would legitimately wait out FlushWaitTimeout
        // each time (the worker's drained branch never runs), which is the
        // documented behaviour, not a bug.
        for (int i = 0; i < 5 && !done.load(std::memory_order_acquire); ++i)
        {
            sink->flush();
            std::this_thread::sleep_for(2ms);
        }
    }, flusher, flushed);
    for (auto& p : producers)
    {
        p.join();
    }
    done.store(true, std::memory_order_release);
    if (!ok)
    {
        flusher.join();
    }
    BOOST_REQUIRE_MESSAGE(ok, "flush() loop did not finish within 10s");
    sink->flush();
    BOOST_CHECK_GT(backend->flushCount(), 0U);
    auto lines = sink->locked_backend()->take();
    BOOST_CHECK_EQUAL(lines.size() + sink->droppedTotal(), static_cast<std::size_t>(4 * 200));
}

// A line far beyond the retained-buffer limit must survive intact (the slot
// releases its oversized buffer afterwards, which must not corrupt the copy).
BOOST_AUTO_TEST_CASE(longLinesAreDeliveredIntact)
{
    auto sink = std::make_shared<SmallSink<8, 2>>();
    std::string const huge(20000, 'Z');
    for (int i = 0; i < 3; ++i)
    {
        sink->writeLine(bcos::LogLevel::INFO, "", huge);
        sink->flush();
        auto lines = sink->locked_backend()->take();
        BOOST_REQUIRE_EQUAL(lines.size(), 1U);
        BOOST_CHECK_EQUAL(lines[0].size(), huge.size());
        BOOST_CHECK(lines[0] == huge);
    }
    // Empty message and a single newline are also legal payloads.
    sink->writeLine(bcos::LogLevel::INFO, "", "");
    sink->flush();
    auto lines = sink->locked_backend()->take();
    BOOST_REQUIRE_EQUAL(lines.size(), 1U);
    BOOST_CHECK(lines[0].empty());
}

// writeRecord defers prefix rendering to the worker; the message must still
// arrive verbatim at the end of the line.
BOOST_AUTO_TEST_CASE(writeRecordKeepsMessageVerbatim)
{
    auto sink = std::make_shared<SmallSink<32, 2>>();
    bcos::LineRecord record;
    record.level = bcos::LogLevel::WARNING;
    record.secs = 1735660800;
    record.micros = 123456;
    record.threadId = 0x1234;
    std::snprintf(record.threadName, sizeof(record.threadName), "%s", "ut");
    for (int i = 0; i < 5; ++i)
    {
        sink->writeRecord(record, "m" + std::to_string(i));
    }
    sink->stop();
    auto lines = sink->locked_backend()->take();
    BOOST_REQUIRE_EQUAL(lines.size(), 5U);
    for (int i = 0; i < 5; ++i)
    {
        auto const& line = lines[i];
        auto const msg = "m" + std::to_string(i);
        BOOST_REQUIRE_GE(line.size(), msg.size());
        BOOST_CHECK_EQUAL(line.compare(line.size() - msg.size(), msg.size(), msg), 0);
        BOOST_CHECK_EQUAL(line.rfind("warning|", 0), 0U);
        BOOST_CHECK(line.find("ut-0x") != std::string::npos);
    }
}

// The registry publishes/unpublishes raw pointers while producers walk it. This
// must not tear, use-after-free or lose the still-registered sinks.
BOOST_AUTO_TEST_CASE(registrySurvivesConcurrentRegisterAndLogging)
{
    std::atomic<bool> done{false};
    std::atomic<uint64_t> logged{0};
    std::vector<std::thread> loggers;
    for (int t = 0; t < 4; ++t)
    {
        loggers.emplace_back([&] {
            while (!done.load(std::memory_order_relaxed))
            {
                BCOS_LOG(INFO) << LOG_BADGE("UT") << LOG_KV("n", logged.load());
                logged.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (int round = 0; round < 30; ++round)
    {
        auto backend = boost::make_shared<CollectingBackend>();
        auto sink = std::make_shared<SmallSink<64, 4>>(backend);
        bcos::log::registerLineSink(sink);
        std::this_thread::sleep_for(2ms);
        bcos::log::unregisterLineSink(sink.get());
        sink->stop();
        sink->flush();
    }
    done.store(true, std::memory_order_relaxed);
    for (auto& l : loggers)
    {
        l.join();
    }
    BOOST_CHECK_GT(logged.load(), 0U);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
