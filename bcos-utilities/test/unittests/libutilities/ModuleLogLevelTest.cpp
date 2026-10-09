/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-utilities/BoostLog.h>
#include <boost/test/unit_test.hpp>

namespace bcos::test
{
namespace
{
struct GlobalLevelGuard
{
    LogLevel saved{c_fileLogLevel};
    GlobalLevelGuard()
    {
        for (size_t i = 0; i < static_cast<size_t>(LogModule::COUNT); ++i)
        {
            resetModuleLogLevel(static_cast<LogModule>(i));
        }
    }
    ~GlobalLevelGuard()
    {
        setFileLogLevel(saved);
        for (size_t i = 0; i < static_cast<size_t>(LogModule::COUNT); ++i)
        {
            resetModuleLogLevel(static_cast<LogModule>(i));
        }
    }
};
}  // namespace

BOOST_FIXTURE_TEST_SUITE(ModuleLogLevelTest, GlobalLevelGuard)

BOOST_AUTO_TEST_CASE(inheritsGlobalWhenUnset)
{
    setFileLogLevel(LogLevel::INFO);
    BOOST_CHECK(!moduleLogEnabled(LogModule::PBFT, LogLevel::DEBUG));
    BOOST_CHECK(moduleLogEnabled(LogModule::PBFT, LogLevel::INFO));
    BOOST_CHECK(moduleLogEnabled(LogModule::PBFT, LogLevel::WARNING));
    BOOST_CHECK(moduleLogLevels().empty());
}

BOOST_AUTO_TEST_CASE(moduleOverrideIsIsolated)
{
    setFileLogLevel(LogLevel::INFO);
    BOOST_REQUIRE(setModuleLogLevel("PBFT", LogLevel::DEBUG));
    BOOST_CHECK(moduleLogEnabled(LogModule::PBFT, LogLevel::DEBUG));
    BOOST_CHECK(!moduleLogEnabled(LogModule::TXPOOL, LogLevel::DEBUG));
    auto levels = moduleLogLevels();
    BOOST_REQUIRE_EQUAL(levels.size(), 1U);
    BOOST_CHECK_EQUAL(levels[0].first, "PBFT");
    BOOST_CHECK(levels[0].second == LogLevel::DEBUG);
}

BOOST_AUTO_TEST_CASE(resetReturnsToInherit)
{
    setFileLogLevel(LogLevel::INFO);
    BOOST_REQUIRE(setModuleLogLevel("PBFT", LogLevel::DEBUG));
    BOOST_REQUIRE(resetModuleLogLevel("PBFT"));
    BOOST_CHECK(!moduleLogEnabled(LogModule::PBFT, LogLevel::DEBUG));
    BOOST_CHECK(moduleLogEnabled(LogModule::PBFT, LogLevel::INFO));
    BOOST_CHECK(moduleLogLevels().empty());
}

BOOST_AUTO_TEST_CASE(parseIsCaseInsensitive)
{
    BOOST_REQUIRE(parseLogModule("pbft").has_value());
    BOOST_CHECK(*parseLogModule("pbft") == LogModule::PBFT);
    BOOST_CHECK(*parseLogModule("TxPool") == LogModule::TXPOOL);
    BOOST_CHECK(!parseLogModule("NOPE").has_value());
    BOOST_CHECK(!parseLogModule("").has_value());
    BOOST_CHECK(!setModuleLogLevel("NOPE", LogLevel::DEBUG));
    BOOST_CHECK(!resetModuleLogLevel("NOPE"));
    BOOST_CHECK_EQUAL(logModuleName(LogModule::GATEWAY), "GATEWAY");
}

BOOST_AUTO_TEST_CASE(explicitModuleIgnoresGlobalChange)
{
    setFileLogLevel(LogLevel::INFO);
    BOOST_REQUIRE(setModuleLogLevel("SYNC", LogLevel::INFO));
    setFileLogLevel(LogLevel::TRACE);
    BOOST_CHECK(!moduleLogEnabled(LogModule::SYNC, LogLevel::DEBUG));
    BOOST_CHECK(moduleLogEnabled(LogModule::SCHEDULER, LogLevel::DEBUG));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
