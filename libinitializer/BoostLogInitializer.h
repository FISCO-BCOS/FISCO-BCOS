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
 * @file: LogInitializer.h
 * @author: yujiechen
 * @date 2018-11-07
 */
#pragma once
#include "Common.h"
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/expressions/formatters/named_scope.hpp>
#include <boost/log/sinks/async_frontend.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/shared_ptr.hpp>

BOOST_LOG_ATTRIBUTE_KEYWORD(group_id, "GroupId", std::string)

namespace dev
{
namespace initializer
{
class Sink : public boost::log::sinks::text_file_backend
{
public:
    void consume(const boost::log::record_view& rec, const std::string& str)
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
};
class ConsoleSink : public boost::log::sinks::text_ostream_backend
{
public:
    void consume(const boost::log::record_view& rec, const std::string& str)
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
};

using sink_t = boost::log::sinks::asynchronous_sink<Sink>;
using console_sink_t = boost::log::sinks::asynchronous_sink<ConsoleSink>;
;
class LogInitializer
{
public:
    using Ptr = std::shared_ptr<LogInitializer>;
    LogInitializer() {}
    virtual ~LogInitializer() { stopLogging(); }

    virtual void initLog(boost::property_tree::ptree const& _pt,
        std::string const& _channel = dev::FileLogger, std::string const& _logPrefix = "log");

    virtual void initStatLog(boost::property_tree::ptree const& _pt,
        std::string const& _channel = dev::StatFileLogger, std::string const& _logPrefix = "stat");

    virtual void stopLogging();

protected:
    virtual unsigned getLogLevel(std::string const& levelStr);
    template <typename T>
    void setLogFormatter(T _sink)
    {
        /// set file format
        /// log-level|timestamp |[g:groupId] message
        _sink->set_formatter(
            boost::log::expressions::stream
            << boost::log::expressions::attr<boost::log::trivial::severity_level>("Severity") << "|"
            << boost::log::expressions::format_date_time<boost::posix_time::ptime>(
                   "TimeStamp", "%Y-%m-%d %H:%M:%S.%f")
            << "|"
            << boost::log::expressions::if_(boost::log::expressions::has_attr(
                   group_id))[boost::log::expressions::stream << "[g:" << group_id << "]"]
            << boost::log::expressions::smessage);
    }

private:
    bool canRotate(size_t const& _index);

    boost::shared_ptr<sink_t> initLogSink(boost::property_tree::ptree const& _pt,
        unsigned const& _logLevel, std::string const& _logPath, std::string const& _logPrefix,
        std::string const& channel);

    boost::shared_ptr<console_sink_t> initConsoleLogSink(boost::property_tree::ptree const& _pt,
        unsigned const& _logLevel, std::string const& channel);

private:
    template <typename T>
    void stopLogging(boost::shared_ptr<T> sink)
    {
        if (!sink)
        {
            return;
        }
        // remove the sink from the core, so that no records are passed to it
        boost::log::core::get()->remove_sink(sink);
        // break the feeding loop
        sink->stop();
        // flush all log records that may have left buffered
        sink->flush();
        sink.reset();
    }

    std::vector<boost::shared_ptr<sink_t>> m_sinks;
    std::vector<boost::shared_ptr<console_sink_t>> m_consoleSinks;

    std::vector<int> m_currentHourVec;
};
}  // namespace initializer
}  // namespace dev
