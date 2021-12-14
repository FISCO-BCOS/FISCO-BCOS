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
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/log/core/core.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>

using namespace bcos;

namespace logging = boost::log;
namespace expr = boost::log::expressions;

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
    auto sink = initLogSink(_pt, logLevel, m_logPath, _logPrefix, _logger);

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
    // not set the log path before init
    if (m_logPath.size() == 0)
    {
        m_logPath = _pt.get<std::string>("log.log_path", "log");
    }
    /// set log level
    unsigned logLevel = getLogLevel(_pt.get<std::string>("log.level", "info"));
    auto sink = initLogSink(_pt, logLevel, m_logPath, _logPrefix, _logger);

    setFileLogLevel((LogLevel)logLevel);

    /// set file format
    /// log-level|timestamp |[g:groupId] message
    sink->set_formatter(expr::stream
                        << boost::log::expressions::attr<boost::log::trivial::severity_level>(
                               "Severity")
                        << "|"
                        << boost::log::expressions::format_date_time<boost::posix_time::ptime>(
                               "TimeStamp", "%Y-%m-%d %H:%M:%S.%f")
                        << "|" << boost::log::expressions::smessage);
}

boost::shared_ptr<bcos::BoostLogInitializer::sink_t> BoostLogInitializer::initLogSink(
    boost::property_tree::ptree const& pt, unsigned const& _logLevel, std::string const& _logPath,
    std::string const& _logPrefix, std::string const& channel)
{
    m_currentHourVec.push_back(
        (int)boost::posix_time::second_clock::local_time().time_of_day().hours());
    /// set file name
    std::string fileName = _logPath + "/" + _logPrefix + "_%Y%m%d%H.%M.log";
    boost::shared_ptr<sink_t> sink(new sink_t());

    sink->locked_backend()->set_open_mode(std::ios::app);
    sink->locked_backend()->set_time_based_rotation(
        boost::bind(&BoostLogInitializer::canRotate, this, (m_currentHourVec.size() - 1)));
    sink->locked_backend()->set_file_name_pattern(fileName);
    /// set rotation size MB
    uint64_t rotation_size = pt.get<uint64_t>("log.max_log_file_size", 200) * 1048576;
    sink->locked_backend()->set_rotation_size(rotation_size);
    /// set auto-flush according to log configuration
    bool need_flush = pt.get<bool>("log.flush", true);
    sink->locked_backend()->auto_flush(need_flush);


    sink->set_filter(boost::log::expressions::attr<std::string>("Channel") == channel &&
                     boost::log::trivial::severity >= _logLevel);


    boost::log::core::get()->add_sink(sink);
    m_sinks.push_back(sink);
    bool enable_log = pt.get<bool>("log.enable", true);
    boost::log::core::get()->set_logging_enabled(enable_log);
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
    if (bcos::stringCmpIgnoreCase(levelStr, "trace") == 0)
        return boost::log::trivial::severity_level::trace;
    if (bcos::stringCmpIgnoreCase(levelStr, "debug") == 0)
        return boost::log::trivial::severity_level::debug;
    if (bcos::stringCmpIgnoreCase(levelStr, "warning") == 0)
        return boost::log::trivial::severity_level::warning;
    if (bcos::stringCmpIgnoreCase(levelStr, "error") == 0)
        return boost::log::trivial::severity_level::error;
    if (bcos::stringCmpIgnoreCase(levelStr, "fatal") == 0)
        return boost::log::trivial::severity_level::fatal;
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
