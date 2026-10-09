/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-ops/tui/LogTail.h>
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
              ("bcos_ops_logtail_" + std::to_string(::getpid()));
        std::filesystem::create_directories(dir);
    }
    ~TempLogDir() { std::filesystem::remove_all(dir); }
    void append(std::string const& _name, std::string const& _text)
    {
        std::ofstream out(dir / _name, std::ios::app);
        out << _text;
    }
};
}  // namespace

BOOST_AUTO_TEST_SUITE(LogTailTest)

BOOST_AUTO_TEST_CASE(pollReturnsOnlyNewLines)
{
    TempLogDir temp;
    temp.append("log_2026101005.00.log",
        "info|2026-10-10 05:00:01.000000|x|[CONSENSUS][PBFT]CheckpointSent,index=1\n"
        "info|2026-10-10 05:00:02.000000|x|[TXPOOL]TxsFetched,count=1\n"
        "info|2026-10-10 05:00:03.000000|x|[CONSENSUS][PBFT]ViewChangeTriggered,reason=restart\n");
    tui::LogTail tail(temp.dir.string());
    auto first = tail.poll();
    BOOST_REQUIRE_EQUAL(first.size(), 3U);
    BOOST_CHECK_EQUAL(first[2].name, "ViewChangeTriggered");
    BOOST_CHECK(tail.poll().empty());
    temp.append(
        "log_2026101005.00.log", "info|2026-10-10 05:00:04.000000|x|[TXPOOL]TxsRemoved,number=2\n");
    auto second = tail.poll();
    BOOST_REQUIRE_EQUAL(second.size(), 1U);
    BOOST_CHECK_EQUAL(second[0].name, "TxsRemoved");
    // a newer file takes over
    temp.append("log_2026101006.00.log",
        "info|2026-10-10 06:00:00.000000|x|[BLOCK SYNC]SyncStarted,number=2\n");
    auto third = tail.poll();
    BOOST_REQUIRE_EQUAL(third.size(), 1U);
    BOOST_CHECK_EQUAL(third[0].name, "SyncStarted");
    BOOST_CHECK(tail.currentFile().ends_with("log_2026101006.00.log"));
}

BOOST_AUTO_TEST_CASE(filterMatchesNameOrChannel)
{
    auto event = *parseLine(
        "info|2026-10-10 05:00:03.000000|x|[CONSENSUS][PBFT]ViewChangeTriggered,reason=restart");
    BOOST_CHECK(tui::LogTail::matches(event, ""));
    BOOST_CHECK(tui::LogTail::matches(event, "ViewChange"));
    BOOST_CHECK(tui::LogTail::matches(event, "viewchange"));
    BOOST_CHECK(tui::LogTail::matches(event, "PBFT"));
    BOOST_CHECK(!tui::LogTail::matches(event, "TXPOOL"));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
