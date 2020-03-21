/**
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 *
 * @brief: setting log
 *
 * @file: LogInitializer.cpp
 * @author: yujiechen
 * @date 2018-11-07
 */
#include "BoostLogInitializer.h"
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/log/core/core.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
namespace logging = boost::log;
namespace expr = boost::log::expressions;

BOOST_LOG_ATTRIBUTE_KEYWORD(group_id, "GroupId", std::string)

using namespace dev::initializer;
int LogInitializer::m_currentHour =
    (int)boost::posix_time::second_clock::local_time().time_of_day().hours();
int LogInitializer::m_index = 0;
/// handler to solve log rotate
static bool canRotate()
{
    const boost::posix_time::ptime now = boost::posix_time::second_clock::local_time();
    int hour = (int)now.time_of_day().hours();
    if (hour != LogInitializer::m_currentHour)
    {
        LogInitializer::m_currentHour = hour;
        return true;
    }
    return false;
}

// init statLog
void LogInitializer::initStatLog(boost::property_tree::ptree const& _pt,
    std::string const& _channel, std::string const& _logPrefix)
{
    std::string logPath = _pt.get<std::string>("log.stat_log_path", "stat");
    initLogSink(_pt, logPath, _logPrefix, _channel);
}
/**
 * @brief: set log for specified channel
 *
 * @param pt: ptree that contains the log configuration
 * @param channel: channel name
 * @param logType: log prefix
 * @ TODO: improve the log flush performance
 */
void LogInitializer::initLog(boost::property_tree::ptree const& _pt, std::string const& _channel,
    std::string const& _logPrefix)
{
    std::string logPath = _pt.get<std::string>("log.log_path", "log");
    initLogSink(_pt, logPath, _logPrefix, _channel);
}

void LogInitializer::initLogSink(boost::property_tree::ptree const& pt, std::string const& _logPath,
    std::string const& _logPrefix, std::string const& channel)
{
    /// set file name

    std::string fileName = _logPath + "/" + _logPrefix + "_%Y%m%d%H.%M.log";
    boost::shared_ptr<sink_t> sink(new sink_t());

    sink->locked_backend()->set_open_mode(std::ios::app);
    sink->locked_backend()->set_time_based_rotation(&canRotate);
    sink->locked_backend()->set_file_name_pattern(fileName);
    /// set rotation size MB
    uint64_t rotation_size = pt.get<uint64_t>("log.max_log_file_size", 200) * 1048576;
    sink->locked_backend()->set_rotation_size(rotation_size);
    /// set auto-flush according to log configuration
    bool need_flush = pt.get<bool>("log.flush", true);
    sink->locked_backend()->auto_flush(need_flush);

    /// set file format
    /// log-level|timestamp |[g:groupId] message
    sink->set_formatter(
        expr::stream
        << boost::log::expressions::attr<boost::log::trivial::severity_level>("Severity") << "|"
        << boost::log::expressions::format_date_time<boost::posix_time::ptime>(
               "TimeStamp", "%Y-%m-%d %H:%M:%S.%f")
        << "|" << expr::if_(expr::has_attr(group_id))[expr::stream << "[g:" << group_id << "]"]
        << boost::log::expressions::smessage);
    /// set log level
    unsigned log_level = getLogLevel(pt.get<std::string>("log.level", "info"));
    sink->set_filter(boost::log::expressions::attr<std::string>("Channel") == channel &&
                     boost::log::trivial::severity >= log_level);


    boost::log::core::get()->add_sink(sink);
    m_sinks.push_back(sink);
    bool enable_log = pt.get<bool>("log.enable", true);
    boost::log::core::get()->set_logging_enabled(enable_log);
    // add attributes
    boost::log::add_common_attributes();
}

/**
 * @brief: get log level according to given string
 *
 * @param levelStr: the given string that should be transformed to boost log level
 * @return unsigned: the log level
 */
unsigned LogInitializer::getLogLevel(std::string const& levelStr)
{
    if (dev::stringCmpIgnoreCase(levelStr, "trace") == 0)
        return boost::log::trivial::severity_level::trace;
    if (dev::stringCmpIgnoreCase(levelStr, "debug") == 0)
        return boost::log::trivial::severity_level::debug;
    if (dev::stringCmpIgnoreCase(levelStr, "warning") == 0)
        return boost::log::trivial::severity_level::warning;
    if (dev::stringCmpIgnoreCase(levelStr, "error") == 0)
        return boost::log::trivial::severity_level::error;
    if (dev::stringCmpIgnoreCase(levelStr, "fatal") == 0)
        return boost::log::trivial::severity_level::fatal;
    /// default log level is info
    return boost::log::trivial::severity_level::info;
}

/// stop and remove all sinks after the program exit
void LogInitializer::stopLogging()
{
    for (auto const& sink : m_sinks)
        stopLogging(sink);
    m_sinks.clear();
}

/// stop a single sink
void LogInitializer::stopLogging(boost::shared_ptr<sink_t> sink)
{
    if (!sink)
        return;
    // remove the sink from the core, so that no records are passed to it
    boost::log::core::get()->remove_sink(sink);
    // break the feeding loop
    sink->stop();
    // flush all log records that may have left buffered
    sink->flush();
    sink.reset();
}
