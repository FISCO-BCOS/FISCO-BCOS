/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-utilities/BoostLogInitializer.h>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include <sstream>

using namespace bcos;

namespace bcos::test
{
namespace
{
// Each initializer adds sinks to the global boost::log core and removes them in
// its destructor (stopLogging), so instances are scoped tightly per-case to keep
// global log state clean. log.enable=false keeps the sinks from emitting.
boost::property_tree::ptree fromIni(std::string const& ini)
{
    boost::property_tree::ptree pt;
    std::stringstream ss(ini);
    boost::property_tree::read_ini(ss, pt);
    return pt;
}

std::string makeTempLogDir(std::string const& tag)
{
    auto dir = std::filesystem::temp_directory_path() / ("bcos_log_test_" + tag);
    std::filesystem::create_directories(dir);
    return dir.string();
}
}  // namespace

BOOST_AUTO_TEST_SUITE(BoostLogInitializerTest)

BOOST_AUTO_TEST_CASE(getLogLevelMapsAllSeverities)
{
    using lvl = boost::log::trivial::severity_level;
    BOOST_CHECK_EQUAL(BoostLogInitializer::getLogLevel("trace"), (unsigned)lvl::trace);
    BOOST_CHECK_EQUAL(BoostLogInitializer::getLogLevel("DEBUG"), (unsigned)lvl::debug);
    BOOST_CHECK_EQUAL(BoostLogInitializer::getLogLevel("warning"), (unsigned)lvl::warning);
    BOOST_CHECK_EQUAL(BoostLogInitializer::getLogLevel("error"), (unsigned)lvl::error);
    BOOST_CHECK_EQUAL(BoostLogInitializer::getLogLevel("fatal"), (unsigned)lvl::fatal);
    BOOST_CHECK_EQUAL(BoostLogInitializer::getLogLevel("info"), (unsigned)lvl::info);
    // unrecognised string falls through to the info default
    BOOST_CHECK_EQUAL(BoostLogInitializer::getLogLevel("nonsense"), (unsigned)lvl::info);
}

BOOST_AUTO_TEST_CASE(initLogHourlyRotation)
{
    auto path = makeTempLogDir("hourly");
    auto pt = fromIni("[log]\nenable=false\nlevel=debug\nlog_path=" + path +
                      "\nenable_rotate_by_hour=true\nmax_log_file_size=200\n");
    BoostLogInitializer init;
    BOOST_CHECK_NO_THROW(init.initLog(pt, bcos::FileLogger, "test"));
    std::filesystem::remove_all(path);
}

BOOST_AUTO_TEST_CASE(initLogNonHourlyWithArchive)
{
    auto path = makeTempLogDir("nonhourly");
    auto pt = fromIni("[log]\nenable=false\nlevel=info\nlog_path=" + path +
                      "\nenable_rotate_by_hour=false\nmax_log_file_size=150\n"
                      "max_archive_size=512\nmax_archive_files=10\ncompress_archive_file=true\n");
    BoostLogInitializer init;
    BOOST_CHECK_NO_THROW(init.initLog(pt, bcos::FileLogger, "test"));
    std::filesystem::remove_all(path);
}

BOOST_AUTO_TEST_CASE(initLogConsole)
{
    auto pt = fromIni(
        "[log]\nenable=false\nenable_console_output=true\nlevel=info\n"
        "max_log_file_size=200\nenable_rate_collector=true\n");
    BoostLogInitializer init;
    BOOST_CHECK_NO_THROW(init.initLog(pt, bcos::FileLogger, "test"));
}

BOOST_AUTO_TEST_CASE(initLogRejectsBadRotateTimePoint)
{
    auto pt = fromIni("[log]\nenable=false\nrotate_time_point=bad\nmax_log_file_size=200\n");
    BoostLogInitializer init;
    BOOST_CHECK_THROW(init.initLog(pt, bcos::FileLogger, "test"), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(initLogRejectsTooSmallFileSize)
{
    auto pt = fromIni("[log]\nenable=false\nmax_log_file_size=50\n");  // < 100MB
    BoostLogInitializer init;
    BOOST_CHECK_THROW(init.initLog(pt, bcos::FileLogger, "test"), std::runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
