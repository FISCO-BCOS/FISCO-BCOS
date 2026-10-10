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

#include <cstddef>
#include <memory>
#include <vector>

#include "BoostLog.h"
#include "LineAsyncSink.h"
#include <sys/types.h>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/shared_ptr.hpp>
#include <atomic>

namespace bcos
{
class BoostLogInitializer
{
public:
    class Sink : public boost::log::sinks::text_file_backend
    {
    public:
        // _line is the complete formatted line; the text backend never reads
        // a record. FATAL still aborts after the line is written.
        void consumeLine(LogLevel _level, const std::string& _line);
    };
    class ConsoleSink : public boost::log::sinks::text_ostream_backend
    {
    public:
        void consumeLine(LogLevel _level, const std::string& _line);
    };
    using Ptr = std::shared_ptr<BoostLogInitializer>;
    using line_sink_t = bcos::log::LineAsyncSink<Sink>;
    using console_line_sink_t = bcos::log::LineAsyncSink<ConsoleSink>;
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
    bool canRotate(size_t const& _index);

    std::shared_ptr<line_sink_t> initLineLogSink(std::string const& _logPath);
    std::shared_ptr<line_sink_t> initHourLineLogSink(
        std::string const& _logPath, std::string const& _logPrefix);
    std::shared_ptr<console_line_sink_t> initLineConsoleLogSink(
        boost::property_tree::ptree const& _pt);

    std::vector<std::shared_ptr<line_sink_t>> m_lineSinks;
    std::vector<std::shared_ptr<console_line_sink_t>> m_consoleLineSinks;

    std::vector<int> m_currentHourVec;
    std::string m_logPath;
    unsigned m_logLevel = 2;
    bool m_consoleLog = false;
    std::string m_logNamePattern;
    bool m_compressArchive = false;
    std::string m_archivePath;
    std::string m_rotateFileNamePattern;
    uint64_t m_rotateSize = 0;
    uint64_t m_maxArchiveSize = 0;
    uint64_t m_minFreeSpace = 0;
    uint32_t m_maxArchiveFiles = 0;
    bool m_autoFlush = false;
    bool m_enableLog = true;
    std::atomic_bool m_running = {false};
    std::vector<int> m_rotateTimePoint = {0, 0, 0};
};
}  // namespace bcos
