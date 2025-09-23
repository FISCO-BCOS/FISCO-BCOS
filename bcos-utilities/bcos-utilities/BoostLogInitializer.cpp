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
 * @brief: setting log
 *
 * @file: BoostLogInitializer.cpp
 * @author: yujiechen
 */
#include "BoostLogInitializer.h"
#include "BoostLogThreadNameAttribute.h"
#include <bcos-framework/Common.h>
#include <bcos-framework/bcos-framework/protocol/GlobalConfig.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/RateCollector.h>
#include <boost/algorithm/string/split.hpp>
#include <boost/core/null_deleter.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/log/core/core.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>

using namespace bcos;

namespace logging = boost::log;
namespace expr = boost::log::expressions;

constexpr int MB_IN_BYTES = 1048576;

// register SIGUSE2 for dynamic reset log level
struct BoostLogLevelResetHandler
{
    static void handle(int sig)
    {
        std::unique_lock<std::mutex> lock(protocol::g_BCOSConfig.signalMutex());
        BCOS_LOG(INFO) << LOG_BADGE("BoostLogInitializer::Signal")
                       << LOG_DESC("receive SIGUSE2 sig");

        try
        {
            boost::property_tree::ptree pt;
            boost::property_tree::read_ini(configFile, pt);

            auto logLevelStr = pt.get<std::string>("log.level", "info");
            /// set log level
            unsigned logLevel = bcos::BoostLogInitializer::getLogLevel(logLevelStr);

            setFileLogLevel((LogLevel)logLevel);

            BCOS_LOG(INFO) << LOG_BADGE("BoostLogInitializer::Signal") << LOG_DESC("set log level")
                           << LOG_KV("logLevelStr", logLevelStr) << LOG_KV("logLevel", logLevel);
        }
        catch (...)
        {}
    }

    static std::string configFile;
};

std::string BoostLogLevelResetHandler::configFile;

/// handler to solve log rotate
bool BoostLogInitializer::canRotate(size_t const& _index)
{
    const boost::posix_time::ptime now = boost::posix_time::second_clock::local_time();
    int hour = (int)now.time_of_day().hours();
    if (hour != m_currentHourVec[_index])
    {
        m_currentHourVec[_index] = hour;
        return true;
    }
    return false;
}

// init statLog
void BoostLogInitializer::initStatLog(boost::property_tree::ptree const& _pt,
    std::string const& _logger, std::string const& _logPrefix)
{
    m_running.store(true);
    // not set the log path before init
    if (m_logPath.size() == 0)
    {
        m_logPath = _pt.get<std::string>("log.log_path", "log");
    }
    /// set log level
    unsigned logLevel = getLogLevel(_pt.get<std::string>("log.level", "info"));
    auto sink = initLogSink(m_logPath, _logger);

    setStatLogLevel((LogLevel)logLevel);

    /// set file format
    /// log-level|timestamp | message
    sink->set_formatter(expr::stream
                        << boost::log::expressions::attr<boost::log::trivial::severity_level>(
                               "Severity")
                        << "|"
                        << boost::log::expressions::format_date_time<boost::posix_time::ptime>(
                               "TimeStamp", "%Y-%m-%d %H:%M:%S.%f")
                        << "|" << boost::log::expressions::smessage);
}

boost::shared_ptr<bcos::BoostLogInitializer::console_sink_t>
BoostLogInitializer::initConsoleLogSink(
    boost::property_tree::ptree const& _pt, unsigned _logLevel, std::string const& channel)
{
    boost::log::add_common_attributes();
    boost::shared_ptr<console_sink_t> consoleSink(new console_sink_t());
    consoleSink->locked_backend()->add_stream(
        boost::shared_ptr<std::ostream>(&std::cout, boost::null_deleter()));

    bool need_flush = _pt.get<bool>("log.flush", true);
    consoleSink->locked_backend()->auto_flush(need_flush);
    consoleSink->set_filter(boost::log::expressions::attr<std::string>("Channel") == channel &&
                            boost::log::trivial::severity >= _logLevel);
    boost::log::core::get()->add_sink(consoleSink);
    m_consoleSinks.push_back(consoleSink);
    bool enable_log = _pt.get<bool>("log.enable", true);
    boost::log::core::get()->set_logging_enabled(enable_log);
    return consoleSink;
}

void BoostLogInitializer::initLog(
    const std::string& _configFile, std::string const& _logger, std::string const& _logPrefix)
{
    boost::property_tree::ptree pt;
    boost::property_tree::read_ini(_configFile, pt);

    initLog(pt, _logger, _logPrefix);

    // register SIGUSR2 for reset boost log level
    BoostLogLevelResetHandler::configFile = _configFile;
#ifndef _WIN32
    signal(BOOST_LOG_RELOAD_LOG_LEVEL, BoostLogLevelResetHandler::handle);
#endif
}

void BoostLogInitializer::initStatLog(
    const std::string& _configFile, std::string const& _logger, std::string const& _logPrefix)
{
    boost::property_tree::ptree pt;
    boost::property_tree::read_ini(_configFile, pt);

    return initStatLog(pt, _logger, _logPrefix);
}

/**
 * @brief: set log for specified channel
 *
 * @param pt: ptree that contains the log configuration
 * @param channel: channel name
 * @param logType: log prefix
 */
void BoostLogInitializer::initLog(boost::property_tree::ptree const& _pt,
    std::string const& _logger, std::string const& _logPrefix)
{
    m_running.store(true);
    // get log level
    m_logLevel = getLogLevel(_pt.get<std::string>("log.level", "info"));
    m_consoleLog = _pt.get<bool>("log.enable_console_output", false);
    m_logFormat = _pt.get<std::string>("log.format", "");
    m_logNamePattern = _pt.get<std::string>("log.log_name_pattern", "log_%Y%m%d_%H%M.log");
    m_compressArchive = _pt.get<bool>("log.compress_archive_file", false);
    m_archivePath = _pt.get<std::string>("log.archive_path", "");
    m_rotateFileNamePattern =
        _pt.get<std::string>("log.rotate_name_pattern", "log_%Y%m%d%H.%M.log");
    auto rotateTimePoint = _pt.get<std::string>("log.rotate_time_point", "0:0:0");
    std::vector<std::string> rotateTimePointVec;
    boost::split(rotateTimePointVec, rotateTimePoint, boost::is_any_of(":"));
    if (rotateTimePointVec.size() != 3)
    {
        throw std::runtime_error(
            "log.rotate_time_point must be in format of HH:MM:SS, current is " + rotateTimePoint);
    }
    for (size_t i = 0; i < rotateTimePointVec.size(); i++)
    {
        m_rotateTimePoint[i] = std::stoi(rotateTimePointVec[i]);
    }
    m_rotateSize = _pt.get<uint64_t>("log.max_log_file_size", 1024) * MB_IN_BYTES;
    if (m_rotateSize < 100 * MB_IN_BYTES)
    {
        throw std::runtime_error("log.max_log_file_size must be greater than 100M");
    }
    m_autoFlush = _pt.get<bool>("log.flush", true);
    m_enableLog = _pt.get<bool>("log.enable", true);
    m_maxArchiveSize = _pt.get<uint64_t>("log.max_archive_size", 0) * MB_IN_BYTES;
    if (m_maxArchiveSize == 0)
    {
        m_maxArchiveSize = std::numeric_limits<uint64_t>::max();
    }
    m_maxArchiveFiles = _pt.get<uint32_t>("log.max_archive_files", 0);
    if (m_maxArchiveFiles == 0)
    {
        m_maxArchiveFiles = std::numeric_limits<uint32_t>::max();
    }
    m_minFreeSpace = _pt.get<uint64_t>("log.min_free_space", 0) * MB_IN_BYTES;

    if (m_consoleLog)
    {
        boost::shared_ptr<console_sink_t> sink = initConsoleLogSink(_pt, m_logLevel, _logger);
        setLogFormatter(sink, m_logFormat);
    }
    else
    {
        if (m_logPath.size() == 0)
        {
            m_logPath = _pt.get<std::string>("log.log_path", "log");
        }
        if (m_archivePath.empty())
        {
            m_archivePath = m_logPath;
        }
        // if (!m_archivePath.empty() && m_archivePath == m_logPath)
        // {
        //     throw std::runtime_error("log.archive_path must be different from log.log_path");
        // }
        auto enableRotateByHour = _pt.get<bool>("log.enable_rotate_by_hour", true);
        boost::shared_ptr<sink_t> sink = nullptr;
        if (enableRotateByHour)
        {
            sink = initHourLogSink(m_logPath, _logPrefix, _logger);
        }
        else
        {
            sink = initLogSink(m_logPath, _logger);
        }
        setLogFormatter(sink, m_logFormat);
    }
    setFileLogLevel((LogLevel)m_logLevel);
    boost::log::core::get()->add_global_attribute("ThreadName", bcos::log::thread_name());

    auto enableRateCollector = _pt.get<bool>("log.enable_rate_collector", false);
    if (enableRateCollector)
    {
        bcos::RateCollector::enable();
    }
    else
    {
        bcos::RateCollector::disable();
    }
}

// rotate the log file the log every hour
boost::shared_ptr<bcos::BoostLogInitializer::sink_t> BoostLogInitializer::initHourLogSink(
    std::string const& _logPath, std::string const& _logPrefix, std::string const& channel)
{
    m_currentHourVec.push_back(
        (int)boost::posix_time::second_clock::local_time().time_of_day().hours());
    /// set file name
    std::string fileName = _logPath + "/" + _logPrefix + "_%Y%m%d%H.%M.log";

    boost::shared_ptr<sink_t> sink(new sink_t());
    sink->locked_backend()->set_open_mode(std::ios::ate);
    sink->locked_backend()->set_time_based_rotation(
        boost::bind(&BoostLogInitializer::canRotate, this, (m_currentHourVec.size() - 1)));

    sink->locked_backend()->set_file_name_pattern(fileName);
    /// set rotation size MB
    sink->locked_backend()->set_rotation_size(m_rotateSize);
    /// set auto-flush according to log configuration
    sink->locked_backend()->auto_flush(m_autoFlush);
    sink->set_filter(boost::log::expressions::attr<std::string>("Channel") == channel);

    boost::log::core::get()->add_sink(sink);
    m_sinks.push_back(sink);
    boost::log::core::get()->set_logging_enabled(m_enableLog);
    // add attributes
    boost::log::add_common_attributes();
    return sink;
}

boost::shared_ptr<bcos::BoostLogInitializer::sink_t> BoostLogInitializer::initLogSink(
    std::string const& _logPath, std::string const& channel)
{
    /// set file name
    // std::string fileName = _logPath + "/" + "log_%Y%m%d_%H%M.log";
    boost::shared_ptr<sink_t> sink(new sink_t());
    sink->locked_backend()->enable_final_rotation(false);
    sink->locked_backend()->set_open_mode(std::ios::app);
    sink->locked_backend()->set_time_based_rotation(boost::log::sinks::file::rotation_at_time_point(
        m_rotateTimePoint[0], m_rotateTimePoint[1], m_rotateTimePoint[2]));
    sink->locked_backend()->set_file_name_pattern(_logPath + "/" + m_logNamePattern);
    sink->locked_backend()->set_target_file_name_pattern(_logPath + "/" + m_rotateFileNamePattern);
    /// set rotation size MB
    sink->locked_backend()->set_rotation_size(m_rotateSize);
    /// set auto-flush according to log configuration
    sink->locked_backend()->auto_flush(m_autoFlush);
    sink->set_filter(boost::log::expressions::attr<std::string>("Channel") == channel);
    if (!m_archivePath.empty())
    {
#if 0
        sink->locked_backend()->set_file_collector(boost::log::sinks::file::make_collector(
            boost::log::keywords::target = m_archivePath,           // to store rotated files
            boost::log::keywords::max_size = m_maxArchiveSize,      // maximum size(bytes)
            boost::log::keywords::min_free_space = m_minFreeSpace,  // minimum free space(bytes)
            boost::log::keywords::max_files = m_maxArchiveFiles  // maximum number of stored files
            ));
#endif
        boost::filesystem::path targetDir(m_archivePath);
        sink->locked_backend()->set_file_collector(
            bcos::log::make_collector(targetDir,  // where to store rotated files
                m_maxArchiveSize,                 // maximum size, in bytes
                m_minFreeSpace,                   // minimum free space, in bytes
                m_maxArchiveFiles,                // maximum number of stored files
                m_compressArchive));
    }
    sink->locked_backend()->scan_for_files();
    boost::log::core::get()->add_sink(sink);
    m_sinks.push_back(sink);
    boost::log::core::get()->set_logging_enabled(m_enableLog);
    // add attributes
    boost::log::add_common_attributes();
    return sink;
}

/**
 * @brief: get log level according to given string
 *
 * @param levelStr: the given string that should be transformed to boost log level
 * @return unsigned: the log level
 */
unsigned BoostLogInitializer::getLogLevel(std::string const& levelStr)
{
    if (boost::iequals(levelStr, "trace"))
    {
        return boost::log::trivial::severity_level::trace;
    }
    if (boost::iequals(levelStr, "debug"))
    {
        return boost::log::trivial::severity_level::debug;
    }
    if (boost::iequals(levelStr, "warning"))
    {
        return boost::log::trivial::severity_level::warning;
    }
    if (boost::iequals(levelStr, "error"))
    {
        return boost::log::trivial::severity_level::error;
    }
    if (boost::iequals(levelStr, "fatal"))
    {
        return boost::log::trivial::severity_level::fatal;
    }
    /// default log level is info
    return boost::log::trivial::severity_level::info;
}

/// stop and remove all sinks after the program exit
void BoostLogInitializer::stopLogging()
{
    if (!m_running)
    {
        return;
    }
    m_running.store(false);
    for (auto const& sink : m_sinks)
    {
        stopLogging(sink);
    }
    m_sinks.clear();

    for (auto const& sink : m_consoleSinks)
    {
        stopLogging(sink);
    }
    m_consoleSinks.clear();
}

/// stop a single sink
void BoostLogInitializer::stopLogging(boost::shared_ptr<sink_t> sink)
{
    if (!sink)
    {
        return;
    }
    // remove the sink from the core, so that no records are passed to it
    if (boost::log::core::get())
    {
        boost::log::core::get()->remove_sink(sink);
    }
    // break the feeding loop
    sink->stop();
    // flush all log records that may have left buffered
    sink->flush();
    sink.reset();
}
void bcos::BoostLogInitializer::Sink::consume(
    const boost::log::record_view& rec, const std::string& str)
{
    boost::log::sinks::text_file_backend::consume(rec, str);
    auto severity = rec.attribute_values()[boost::log::aux::default_attribute_names::severity()]
                        .extract<boost::log::trivial::severity_level>();
    // bug fix: determine m_ptr before get the log level
    //          serverity.get() will call  BOOST_ASSERT(m_ptr)
    if (severity.get_ptr() && severity.get() == boost::log::trivial::severity_level::fatal)
    {
        // abort if encounter fatal, will generate coredump
        // must make sure only use LOG(FATAL) when encounter the most serious problem
        // forbid use LOG(FATAL) in the function that should exit normally
        std::abort();
    }
}
void bcos::BoostLogInitializer::ConsoleSink::consume(
    const boost::log::record_view& rec, const std::string& str)
{
    boost::log::sinks::text_ostream_backend::consume(rec, str);
    auto severity = rec.attribute_values()[boost::log::aux::default_attribute_names::severity()]
                        .extract<boost::log::trivial::severity_level>();
    // bug fix: determine m_ptr before get the log level
    //          serverity.get() will call  BOOST_ASSERT(m_ptr)
    if (severity.get_ptr() && severity.get() == boost::log::trivial::severity_level::fatal)
    {
        // abort if encounter fatal, will generate coredump
        // must make sure only use LOG(FATAL) when encounter the most serious problem
        // forbid use LOG(FATAL) in the function that should exit normally
        std::abort();
    }
}
