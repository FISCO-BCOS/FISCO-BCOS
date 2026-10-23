/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Unit tests for custom log.format support: parseLineLayout /
 *        appendLayoutLine rendering, the commitLine layout branch and the
 *        BoostLogInitializer installation path.
 * @file LineLayoutTest.cpp
 */

#include "bcos-utilities/LineLayout.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/BoostLogInitializer.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/LineAsyncSink.h"
#include <boost/make_shared.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace bcos::test
{
namespace
{
// NOTE: this file is unity-built together with the other suites in this
// directory, so every helper carries a Layout prefix to keep the shared
// anonymous namespace collision-free.

// Restores the process-global logging state mutated by these cases so other
// suites in the shared test binary keep seeing the built-in layout.
struct LayoutLogStateGuard
{
    LayoutLogStateGuard() : m_level(bcos::c_fileLogLevel.load(std::memory_order_relaxed)) {}
    ~LayoutLogStateGuard()
    {
        bcos::log::setLineLayout(nullptr);
        bcos::setFileLogLevel(m_level);
    }
    LogLevel m_level;
};

std::string layoutRender(std::string_view _format, LogLevel _level, std::string_view _message)
{
    std::string error;
    auto layout = bcos::log::parseLineLayout(_format, error);
    BOOST_REQUIRE_MESSAGE(layout != nullptr, error);
    std::string out;
    bcos::log::appendLayoutLine(out, *layout, _level, _message);
    return out;
}

void installTestLayout(std::string_view _format)
{
    std::string error;
    auto layout = bcos::log::parseLineLayout(_format, error);
    BOOST_REQUIRE_MESSAGE(layout != nullptr, error);
    bcos::log::setLineLayout(std::move(layout));
}

class LayoutCaptureWriter : public bcos::log::LineSinkWriter
{
public:
    struct Entry
    {
        LogLevel level;
        std::string prefix;
        std::string message;
    };
    void writeLine(LogLevel _level, std::string_view _prefix, std::string_view _message) override
    {
        std::lock_guard lock(m_mutex);
        m_entries.push_back({_level, std::string(_prefix), std::string(_message)});
    }
    std::vector<Entry> entries()
    {
        std::lock_guard lock(m_mutex);
        return m_entries;
    }

private:
    std::mutex m_mutex;
    std::vector<Entry> m_entries;
};

struct LayoutGatedBackend
{
    std::mutex mutex;
    std::vector<std::pair<LogLevel, std::string>> lines;
    std::atomic<bool> gate{true};

    void consumeLine(LogLevel _level, std::string const& _line)
    {
        while (!gate.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
        std::lock_guard lock(mutex);
        lines.emplace_back(_level, _line);
    }
    void flush() {}

    std::vector<std::string> snapshot()
    {
        std::lock_guard lock(mutex);
        std::vector<std::string> out;
        for (auto const& [level, line] : lines)
        {
            out.push_back(line);
        }
        return out;
    }
};

boost::property_tree::ptree layoutFromIni(std::string const& _ini)
{
    boost::property_tree::ptree pt;
    std::stringstream ss(_ini);
    boost::property_tree::read_ini(ss, pt);
    return pt;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(LineLayoutTest)

BOOST_AUTO_TEST_CASE(literalAndMessage)
{
    LayoutLogStateGuard guard;
    BOOST_CHECK_EQUAL(layoutRender("hello %Message%", LogLevel::INFO, "world"), "hello world");
    BOOST_CHECK_EQUAL(layoutRender("%Message%", LogLevel::INFO, "only"), "only");
    BOOST_CHECK_EQUAL(
        layoutRender("no placeholders", LogLevel::INFO, "dropped"), "no placeholders");
}

BOOST_AUTO_TEST_CASE(escapedPercent)
{
    LayoutLogStateGuard guard;
    BOOST_CHECK_EQUAL(layoutRender("100%% %Message%", LogLevel::INFO, "done"), "100% done");
}

BOOST_AUTO_TEST_CASE(severityNames)
{
    LayoutLogStateGuard guard;
    BOOST_CHECK_EQUAL(layoutRender("%Severity%", LogLevel::TRACE, ""), "trace");
    BOOST_CHECK_EQUAL(layoutRender("%Severity%", LogLevel::DEBUG, ""), "debug");
    BOOST_CHECK_EQUAL(layoutRender("%Severity%", LogLevel::WARNING, ""), "warning");
    BOOST_CHECK_EQUAL(layoutRender("%Severity%|%Message%", LogLevel::ERROR, "x"), "error|x");
    BOOST_CHECK_EQUAL(layoutRender("%Severity%", LogLevel::FATAL, ""), "fatal");
}

BOOST_AUTO_TEST_CASE(timestampDefaultShape)
{
    LayoutLogStateGuard guard;
    // A bare %TimeStamp% renders like boost's to_simple_string at microsecond
    // resolution: YYYY-MM-DD HH:MM:SS.ffffff
    auto const line = layoutRender("%TimeStamp%|%Message%", LogLevel::INFO, "m");
    std::regex const re(R"(^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{6}\|m$)");
    BOOST_CHECK_MESSAGE(std::regex_match(line, re), line);
}

BOOST_AUTO_TEST_CASE(timestampCustomFormat)
{
    LayoutLogStateGuard guard;
    auto const line =
        layoutRender(R"(%TimeStamp(format="%H:%M:%S.%f")%|%Message%)", LogLevel::INFO, "m");
    std::regex const re(R"(^\d{2}:\d{2}:\d{2}\.\d{6}\|m$)");
    BOOST_CHECK_MESSAGE(std::regex_match(line, re), line);

    auto const ymd = layoutRender(R"(%TimeStamp(format="%y/%m/%d")%)", LogLevel::INFO, "");
    std::regex const re2(R"(^\d{2}/\d{2}/\d{2}$)");
    BOOST_CHECK_MESSAGE(std::regex_match(ymd, re2), ymd);

    // literal percent inside the time format
    auto const pct = layoutRender(R"(%TimeStamp(format="%H%%")%)", LogLevel::INFO, "");
    std::regex const re3(R"(^\d{2}%$)");
    BOOST_CHECK_MESSAGE(std::regex_match(pct, re3), pct);
}

BOOST_AUTO_TEST_CASE(threadFields)
{
    LayoutLogStateGuard guard;
    auto const previousName = bcos::pthread_getThreadNameRef();
    bcos::pthread_setThreadName("layoutTest");
    auto const line = layoutRender("%ThreadName%|%ThreadID%|%Message%", LogLevel::INFO, "m");
    bcos::pthread_setThreadName(previousName);
    std::regex const re(R"(^layoutTest\|0x[0-9a-f]+\|m$)");
    BOOST_CHECK_MESSAGE(std::regex_match(line, re), line);
}

BOOST_AUTO_TEST_CASE(lineIdIncrements)
{
    LayoutLogStateGuard guard;
    std::string error;
    auto layout = bcos::log::parseLineLayout("%LineID%|%Message%", error);
    BOOST_REQUIRE_MESSAGE(layout != nullptr, error);
    std::string first, second;
    bcos::log::appendLayoutLine(first, *layout, LogLevel::INFO, "x");
    bcos::log::appendLayoutLine(second, *layout, LogLevel::INFO, "x");
    auto const idOf = [](std::string const& s) { return std::stoull(s.substr(0, s.find('|'))); };
    BOOST_CHECK_EQUAL(idOf(first) + 1, idOf(second));
}

BOOST_AUTO_TEST_CASE(processIdMatchesGetpid)
{
    LayoutLogStateGuard guard;
#ifdef _WIN32
    auto const pid = static_cast<unsigned long long>(::GetCurrentProcessId());
#else
    auto const pid = static_cast<unsigned long long>(::getpid());
#endif
    BOOST_CHECK_EQUAL(layoutRender("%ProcessID%", LogLevel::INFO, ""), std::to_string(pid));
}

BOOST_AUTO_TEST_CASE(rejectsBadFormats)
{
    LayoutLogStateGuard guard;
    std::string error;
    BOOST_CHECK(bcos::log::parseLineLayout("%Bogus%|%Message%", error) == nullptr);
    BOOST_CHECK(!error.empty());
    error.clear();
    BOOST_CHECK(bcos::log::parseLineLayout(R"(%Severity(format="x")%)", error) == nullptr);
    BOOST_CHECK(!error.empty());
    error.clear();
    // %j (day of year) is not in the supported date_time flag set
    BOOST_CHECK(bcos::log::parseLineLayout(R"(%TimeStamp(format="%j")%)", error) == nullptr);
    BOOST_CHECK(!error.empty());
    error.clear();
    BOOST_CHECK(bcos::log::parseLineLayout("%Severity", error) == nullptr);
    BOOST_CHECK(!error.empty());
    error.clear();
    BOOST_CHECK(bcos::log::parseLineLayout(R"(%TimeStamp(format="%Y-%m-%d)%")", error) == nullptr);
    BOOST_CHECK(!error.empty());
}

BOOST_AUTO_TEST_CASE(commitLineUsesInstalledLayout)
{
    LayoutLogStateGuard guard;
    bcos::setFileLogLevel(LogLevel::TRACE);
    installTestLayout("%LineID% | %Severity% | %Message%");
    auto sink = std::make_shared<LayoutCaptureWriter>();
    bcos::log::registerLineSink(sink);
    BCOS_LOG(INFO) << "layoutMarker";
    bcos::log::unregisterLineSink(sink.get());

    auto const captured = sink->entries();
    BOOST_REQUIRE_EQUAL(captured.size(), 1u);
    // the layout consumes the message: it arrives as prefix + empty message
    BOOST_CHECK(captured[0].message.empty());
    std::regex const re(R"(^\d+ \| info \| layoutMarker$)");
    BOOST_CHECK_MESSAGE(std::regex_match(captured[0].prefix, re), captured[0].prefix);
}

BOOST_AUTO_TEST_CASE(commitLineBuiltInWithoutLayout)
{
    LayoutLogStateGuard guard;
    bcos::log::setLineLayout(nullptr);
    bcos::setFileLogLevel(LogLevel::TRACE);
    auto sink = std::make_shared<LayoutCaptureWriter>();
    bcos::log::registerLineSink(sink);
    BCOS_LOG(INFO) << "plainMarker";
    bcos::log::unregisterLineSink(sink.get());

    auto const captured = sink->entries();
    BOOST_REQUIRE_EQUAL(captured.size(), 1u);
    BOOST_CHECK_EQUAL(captured[0].message, "plainMarker");
    std::regex const re(R"(^info\|\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{6}\|.+\|$)");
    BOOST_CHECK_MESSAGE(std::regex_match(captured[0].prefix, re), captured[0].prefix);
}

BOOST_AUTO_TEST_CASE(initLogInstallsCustomLayout)
{
    LayoutLogStateGuard guard;
    auto pt = layoutFromIni("[log]\nenable=false\nformat=%LineID%|%Severity%|%Message%\n");
    {
        BoostLogInitializer init;
        init.initLog(pt, bcos::FileLogger, "test");
        auto sink = std::make_shared<LayoutCaptureWriter>();
        bcos::log::registerLineSink(sink);
        BCOS_LOG(INFO) << "viaInit";
        bcos::log::unregisterLineSink(sink.get());

        auto const captured = sink->entries();
        BOOST_REQUIRE_EQUAL(captured.size(), 1u);
        BOOST_CHECK(captured[0].message.empty());
        std::regex const re(R"(^\d+\|info\|viaInit$)");
        BOOST_CHECK_MESSAGE(std::regex_match(captured[0].prefix, re), captured[0].prefix);
    }
}

BOOST_AUTO_TEST_CASE(initLogFallsBackOnBadFormat)
{
    LayoutLogStateGuard guard;
    auto pt = layoutFromIni("[log]\nenable=false\nformat=%Bogus%|%Message%\n");
    {
        BoostLogInitializer init;
        // must not throw; the built-in layout is used instead
        init.initLog(pt, bcos::FileLogger, "test");
        auto sink = std::make_shared<LayoutCaptureWriter>();
        bcos::log::registerLineSink(sink);
        BCOS_LOG(INFO) << "fallbackMarker";
        bcos::log::unregisterLineSink(sink.get());

        auto const captured = sink->entries();
        BOOST_REQUIRE_EQUAL(captured.size(), 1u);
        BOOST_CHECK_EQUAL(captured[0].message, "fallbackMarker");
        std::regex const re(R"(^info\|\d{4}-\d{2}-\d{2} .+\|$)");
        BOOST_CHECK_MESSAGE(std::regex_match(captured[0].prefix, re), captured[0].prefix);
    }
}

BOOST_AUTO_TEST_CASE(droppedMarkerFollowsCustomLayout)
{
    LayoutLogStateGuard guard;
    installTestLayout("MARK %Severity% %Message%");
    auto backend = boost::make_shared<LayoutGatedBackend>();
    {
        bcos::log::LineAsyncSink<LayoutGatedBackend, 2> sink(backend);
        // hold the feeding thread inside consumeLine so both slots stay in
        // flight and the third write deterministically overflows the pool
        backend->gate.store(false, std::memory_order_release);
        sink.writeLine(LogLevel::INFO, "MARK info one", "");
        sink.writeLine(LogLevel::INFO, "MARK info two", "");
        sink.writeLine(LogLevel::INFO, "MARK info three", "");
        backend->gate.store(true, std::memory_order_release);
        sink.flush();  // drains the backlog, then emits the dropped marker
    }
    auto const lines = backend->snapshot();
    BOOST_REQUIRE_EQUAL(lines.size(), 3u);
    BOOST_CHECK_EQUAL(lines[0], "MARK info one");
    BOOST_CHECK_EQUAL(lines[1], "MARK info two");
    BOOST_CHECK_EQUAL(lines[2], "MARK warning [log] dropped 1 log line(s): slot pool exhausted");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
