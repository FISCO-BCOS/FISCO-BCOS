/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-ops/OpsError.h>
#include <bcos-ops/log/LogFiles.h>
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include <fstream>

namespace bcos::ops::test
{
namespace
{
struct TempLogDir
{
    std::filesystem::path dir;
    TempLogDir()
    {
        dir = std::filesystem::temp_directory_path() /
              ("bcos_ops_logfiles_" + std::to_string(::getpid()));
        std::filesystem::create_directories(dir);
    }
    ~TempLogDir() { std::filesystem::remove_all(dir); }
    void touch(std::string const& _name, std::string const& _content = "")
    {
        std::ofstream out(dir / _name);
        out << _content;
    }
};
}  // namespace

BOOST_AUTO_TEST_SUITE(LogFilesTest)

BOOST_AUTO_TEST_CASE(bothNamePatternsParse)
{
    auto a = logFileStartMs("log_20261010_0144.log");
    auto b = logFileStartMs("log_2026101001.44.log");
    BOOST_REQUIRE(a);
    BOOST_REQUIRE(b);
    BOOST_CHECK_EQUAL(*a, *b);
    BOOST_CHECK(!logFileStartMs("log_20261010.log"));
    BOOST_CHECK(!logFileStartMs("stat_2026101001.44.log"));
    BOOST_CHECK(!logFileStartMs("log_2026101001.44.log.gz"));
}

BOOST_AUTO_TEST_CASE(sinceSelectsRecentFilesPlusTheOneBefore)
{
    TempLogDir temp;
    // five files, two naming styles; "now" is 2026-10-10 05:30 local
    temp.touch("log_20261010_0000.log");
    temp.touch("log_20261010_0100.log");
    temp.touch("log_2026101002.00.log");
    temp.touch("log_2026101004.45.log");
    temp.touch("log_2026101005.00.log");
    temp.touch("other.txt");
    auto now = *logFileStartMs("log_2026101005.30.log");
    auto files = selectLogFiles(temp.dir.string(), 3600 * 1000, now);
    BOOST_REQUIRE_EQUAL(files.size(), 3U);  // 04:45 and 05:00 are inside the hour, 02:00 spans the
                                            // boundary
    BOOST_CHECK(files[0].ends_with("log_2026101002.00.log"));
    BOOST_CHECK(files[1].ends_with("log_2026101004.45.log"));
    BOOST_CHECK(files[2].ends_with("log_2026101005.00.log"));
    auto everything = selectLogFiles(temp.dir.string(), 24 * 3600 * 1000, now);
    BOOST_CHECK_EQUAL(everything.size(), 5U);
    auto latestOnly = selectLogFiles(temp.dir.string(), 60 * 1000, now);
    BOOST_REQUIRE_EQUAL(latestOnly.size(), 1U);  // nothing inside the minute: just the one before
    BOOST_CHECK(latestOnly[0].ends_with("log_2026101005.00.log"));
}

BOOST_AUTO_TEST_CASE(readEventsSkipsGarbage)
{
    TempLogDir temp;
    temp.touch("log_2026101005.00.log",
        "info|2026-10-10 05:00:01.000000|x|[CONSENSUS][PBFT]CheckpointSent,index=1,hash=ab\n"
        "garbage line\n"
        "debug|2026-10-10 05:00:02.000000|x|[TXPOOL]TxAdmitted,tx=0x1\n");
    auto events = readEvents({(temp.dir / "log_2026101005.00.log").string()});
    BOOST_REQUIRE_EQUAL(events.size(), 2U);
    BOOST_CHECK_EQUAL(events[0].name, "CheckpointSent");
    BOOST_CHECK_EQUAL(events[1].name, "TxAdmitted");
}

BOOST_AUTO_TEST_CASE(durations)
{
    BOOST_CHECK_EQUAL(parseDurationMs("90s"), 90000);
    BOOST_CHECK_EQUAL(parseDurationMs("30m"), 1800000);
    BOOST_CHECK_EQUAL(parseDurationMs("1h"), 3600000);
    BOOST_CHECK_EQUAL(parseDurationMs("2d"), 172800000);
    BOOST_CHECK_EQUAL(parseDurationMs("5"), 5000);
    BOOST_CHECK_THROW(parseDurationMs("1x"), OpsError);
    BOOST_CHECK_THROW(parseDurationMs(""), OpsError);
}

BOOST_AUTO_TEST_CASE(lastEventOfEachKeepsOnePerNameAndChannel)
{
    TempLogDir temp;
    temp.touch("log_2026101005.00.log",
        "info|2026-10-10 05:00:01.000000|x|[CONSENSUS][PBFT][METRIC]Report,committedIndex=1\n"
        "info|2026-10-10 05:00:02.000000|x|[BLOCK SYNC]SyncStarted,number=1,highest=9\n"
        "info|2026-10-10 05:00:03.000000|x|[CONSENSUS][PBFT][STORAGE]BlockCommitted,index=2\n"
        "info|2026-10-10 05:00:04.000000|x|[BLOCK SYNC][METRIC]BlockCommitted,number=2\n"
        "info|2026-10-10 05:00:05.000000|x|[TXPOOL][METRIC]TxsFetched,count=3\n"
        "info|2026-10-10 05:00:06.000000|x|[CONSENSUS][PBFT][METRIC]Report,committedIndex=2\n");
    auto files = std::vector<std::string>{(temp.dir / "log_2026101005.00.log").string()};
    auto keep = namedEvents({"Report", "SyncStarted", "BlockCommitted"});
    auto events = readLastEventOfEach(files, keep);
    // one Report (the later), one SyncStarted, BlockCommitted once per channel; TxsFetched dropped
    BOOST_REQUIRE_EQUAL(events.size(), 4U);
    BOOST_CHECK_EQUAL(events[0].name, "SyncStarted");
    BOOST_CHECK_EQUAL(events[1].name, "BlockCommitted");
    BOOST_CHECK(events[1].hasBadge("CONSENSUS"));
    BOOST_CHECK_EQUAL(events[2].name, "BlockCommitted");
    BOOST_CHECK(events[2].hasBadge("BLOCK SYNC"));
    BOOST_CHECK_EQUAL(events[3].name, "Report");
    BOOST_CHECK_EQUAL(events[3].get("committedIndex"), "2");
    auto all = readEventsMatching(files, keep);
    BOOST_CHECK_EQUAL(all.size(), 5U);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
