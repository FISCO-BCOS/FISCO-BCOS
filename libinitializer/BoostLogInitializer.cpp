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
#include <boost/core/null_deleter.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/log/core/core.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/utility/exception_handler.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <exception>
#include <stdexcept>


using namespace dev::initializer;
/// handler to solve log rotate
bool LogInitializer::canRotate(size_t const& _index)
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
void LogInitializer::initStatLog(boost::property_tree::ptree const& _pt,
    std::string const& _channel, std::string const& _logPrefix)
{
    std::string logPath = "./stat";

    /// set log level
    unsigned logLevel = getLogLevel(_pt.get<std::string>("log.level", "info"));
    auto sink = initLogSink(_pt, logLevel, logPath, _logPrefix, _channel);

    setStatLogLevel((LogLevel)logLevel);

    /// set file format
    /// log-level|timestamp | message
    sink->set_formatter(
        boost::log::expressions::stream
        << boost::log::expressions::attr<boost::log::trivial::severity_level>("Severity") << "|"
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
void LogInitializer::initLog(boost::property_tree::ptree const& _pt, std::string const& _channel,
    std::string const& _logPrefix)
{
    // get log level
    unsigned logLevel = getLogLevel(_pt.get<std::string>("log.level", "info"));
    bool consoleLog = _pt.get<bool>("log.enable_console_output", false);
    if (consoleLog)
    {
        boost::shared_ptr<console_sink_t> sink = initConsoleLogSink(_pt, logLevel, _channel);
        setLogFormatter(sink);
    }
    else
    {
        std::string logPath = _pt.get<std::string>("log.log_path", "log");
        boost::shared_ptr<sink_t> sink = initLogSink(_pt, logLevel, logPath, _logPrefix, _channel);
        setLogFormatter(sink);
    }
    setFileLogLevel((LogLevel)logLevel);
}

boost::shared_ptr<console_sink_t> LogInitializer::initConsoleLogSink(
    boost::property_tree::ptree const& _pt, unsigned const& _logLevel, std::string const& channel)
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

boost::shared_ptr<sink_t> LogInitializer::initLogSink(boost::property_tree::ptree const& pt,
    unsigned const& _logLevel, std::string const& _logPath, std::string const& _logPrefix,
    std::string const& channel)
{
    m_currentHourVec.push_back(
        (int)boost::posix_time::second_clock::local_time().time_of_day().hours());
    /// set file name
    std::string fileName = _logPath + "/" + _logPrefix + "_%Y%m%d%H.%M.log";
    boost::shared_ptr<sink_t> sink(new sink_t());

    sink->locked_backend()->set_open_mode(std::ios::app);
    sink->locked_backend()->set_time_based_rotation(
        boost::bind(&LogInitializer::canRotate, this, (m_currentHourVec.size() - 1)));
    sink->locked_backend()->set_file_name_pattern(fileName);
    /// set rotation size MB
    uint64_t rotation_size = pt.get<uint64_t>("log.max_log_file_size", 200) * 1048576;
    sink->locked_backend()->set_rotation_size(rotation_size);
    /// set auto-flush according to log configuration
    bool need_flush = pt.get<bool>("log.flush", true);
    sink->locked_backend()->auto_flush(need_flush);

    struct LogExpHandler
    {
        void operator()(std::runtime_error const& e) const
        {
            std::cout << "std::runtime_error: " << e.what() << std::endl;
            throw;
        }
        void operator()(std::logic_error const& e) const
        {
            std::cout << "std::logic_error: " << e.what() << std::endl;
            throw;
        }
    };

    sink->set_filter(boost::log::expressions::attr<std::string>("Channel") == channel &&
                     boost::log::trivial::severity >= _logLevel);

    boost::log::core::get()->set_exception_handler(
        boost::log::make_exception_handler<std::runtime_error, std::logic_error>(LogExpHandler()));

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