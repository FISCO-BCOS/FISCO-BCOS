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
 * @file: BoostLogInitializer.h
 * @author: yujiechen
 */
#pragma once

#include "Common.h"
#include <sys/types.h>
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
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/shared_ptr.hpp>

namespace bcos
{
class BoostLogInitializer
{
public:
    class Sink : public boost::log::sinks::text_file_backend
    {
    public:
        void consume(const boost::log::record_view& rec, const std::string& str);
    };
    class ConsoleSink : public boost::log::sinks::text_ostream_backend
    {
    public:
        void consume(const boost::log::record_view& rec, const std::string& str);
    };
    using Ptr = std::shared_ptr<BoostLogInitializer>;
    using sink_t = boost::log::sinks::asynchronous_sink<Sink>;
    using console_sink_t = boost::log::sinks::asynchronous_sink<ConsoleSink>;
    virtual ~BoostLogInitializer() { stopLogging(); }
    BoostLogInitializer() = default;

    void initLog(const std::string& _configFile, std::string const& _logger = bcos::FileLogger,
        std::string const& _logPrefix = "log");

    void initLog(boost::property_tree::ptree const& _pt,
        std::string const& _logger = bcos::FileLogger, std::string const& _logPrefix = "log");

    void stopLogging();

    static unsigned getLogLevel(std::string const& levelStr);

    void setLogPath(std::string const& _logPath) { m_logPath = _logPath; }
    std::string logPath() const { return m_logPath; }

private:
    void initStatLog(boost::property_tree::ptree const& _pt,
        std::string const& _logger = bcos::StatFileLogger, std::string const& _logPrefix = "stat");
    void initStatLog(const std::string& _configFile,
        std::string const& _logger = bcos::StatFileLogger, std::string const& _logPrefix = "stat");
    bool canRotate(size_t const& _index);

    boost::shared_ptr<sink_t> initLogSink(std::string const& _logPath, std::string const& channel);

    // rotate the log file the log every hour
    boost::shared_ptr<sink_t> initHourLogSink(
        std::string const& _logPath, std::string const& _logPrefix, std::string const& channel);

    boost::shared_ptr<console_sink_t> initConsoleLogSink(
        boost::property_tree::ptree const& _pt, unsigned _logLevel, std::string const& channel);

    template <typename T>
    void setLogFormatter(T _sink, const std::string& format = "")
    {
        /// set file format
        /// log-level|timestamp|thread-id|message
        if (!format.empty())
        {
            try
            {
                auto formatter = boost::log::parse_formatter(format);
                _sink->set_formatter(formatter);
            }
            catch (std::exception& e)
            {
                std::cout << "set log format failed, error = " << e.what() << std::endl;
            }
        }
        else
        {
            _sink->set_formatter(
                boost::log::expressions::stream
                << boost::log::expressions::attr<boost::log::trivial::severity_level>("Severity")
                << "|"
                << boost::log::expressions::format_date_time<boost::posix_time::ptime>(
                       "TimeStamp", "%Y-%m-%d %H:%M:%S.%f")
                << "|" << boost::log::expressions::attr<std::string>("ThreadName") << "-"
                << boost::log::expressions::attr<
                       boost::log::attributes::current_thread_id::value_type>("ThreadID")
                << "|" << boost::log::expressions::smessage);
        }
    }

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

    void stopLogging(boost::shared_ptr<sink_t> sink);
    std::vector<boost::shared_ptr<sink_t>> m_sinks;
    std::vector<boost::shared_ptr<console_sink_t>> m_consoleSinks;

    std::vector<int> m_currentHourVec;
    std::string m_logPath;
    unsigned m_logLevel = 2;
    bool m_consoleLog = false;
    std::string m_logNamePattern;
    std::string m_logFormat;
    bool m_compressArchive = false;
    std::string m_archivePath;
    std::string m_rotateFileNamePattern;
    uint64_t m_rotateSize = 0;
    uint64_t m_maxArchiveSize = 0;
    uint64_t m_minFreeSpace = 0;
    uint32_t m_maxArchiveFiles = 0;
    bool m_autoFlush = true;
    bool m_enableLog = true;
    std::atomic_bool m_running = {false};
};
}  // namespace bcos
