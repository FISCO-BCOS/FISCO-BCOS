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
#include "EasyLogInitializer.h"
#include <boost/filesystem.hpp>
using namespace dev::initializer;

const std::chrono::seconds LogInitializer::wakeUpDelta = std::chrono::seconds(20);
std::chrono::system_clock::time_point LogInitializer::nextWakeUp = std::chrono::system_clock::now();
static std::map<std::string, unsigned int> s_mlogIndex;
static void inline rolloutHandler(const char* filename, std::size_t)
{
    std::stringstream stream;
    std::map<std::string, unsigned int>::iterator iter = s_mlogIndex.find(filename);
    if (iter != s_mlogIndex.end())
    {
        stream << filename << "." << iter->second++;
        s_mlogIndex[filename] = iter->second++;
    }
    else
    {
        stream << filename << "." << 0;
        s_mlogIndex[filename] = 0;
    }
    boost::filesystem::rename(filename, stream.str().c_str());
}

EasyLogLevel LogInitializer::getLogLevel(std::string const& levelStr)
{
    if (dev::stringCmpIgnoreCase(levelStr, "trace") == 0)
        return EasyLogLevel::TRACE;
    if (dev::stringCmpIgnoreCase(levelStr, "debug") == 0)
        return EasyLogLevel::DEBUG;
    if (dev::stringCmpIgnoreCase(levelStr, "warning") == 0)
        return EasyLogLevel::WARNNING;
    if (dev::stringCmpIgnoreCase(levelStr, "error") == 0)
        return EasyLogLevel::ERROR;
    if (dev::stringCmpIgnoreCase(levelStr, "fatal") == 0)
        return EasyLogLevel::FATAL;
    return EasyLogLevel::INFO;
}

/// set easylog with default setting
void LogInitializer::initLog(boost::property_tree::ptree const& pt)
{
    // Enables support for multiple loggers
    el::Loggers::addFlag(el::LoggingFlag::MultiLoggerSupport);
    el::Loggers::addFlag(el::LoggingFlag::MultiLoggerSupport);
    el::Loggers::addFlag(el::LoggingFlag::StrictLogFileSizeCheck);
    el::Loggers::setVerboseLevel(10);
    el::Configurations defaultConf;
    el::Logger* fileLogger = el::Loggers::getLogger("fileLogger");
    std::string logPath = pt.get<std::string>("log.log_path", "./log/");
    std::string logPostfix = "log_%datetime{%Y%M%d%H}.log";
    /// init the defaultConf
    /// init global configurations
    defaultConf.set(el::Level::Global, el::ConfigurationType::Enabled,
        pt.get<std::string>("log.enabled", "true"));
    defaultConf.set(el::Level::Global, el::ConfigurationType::ToFile, "true");
    defaultConf.set(el::Level::Global, el::ConfigurationType::ToStandardOutput, "false");
    defaultConf.set(el::Level::Global, el::ConfigurationType::Format,
        pt.get<std::string>("log.format", "%level|%datetime{%Y-%M-%d %H:%m:%s:%g}|%msg"));
    defaultConf.set(el::Level::Global, el::ConfigurationType::MillisecondsWidth,
        pt.get<std::string>("log.millseconds_width", "3"));
    defaultConf.set(el::Level::Global, el::ConfigurationType::PerformanceTracking,
        pt.get<std::string>("log.performance_tracking", "false"));
    defaultConf.set(el::Level::Global, el::ConfigurationType::MaxLogFileSize,
        pt.get<std::string>("log.max_log_file_size", "209715200"));
    defaultConf.set(el::Level::Global, el::ConfigurationType::LogFlushThreshold,
        pt.get<std::string>("log.log_flush_threshold", "100"));
    defaultConf.set(el::Level::Global, el::ConfigurationType::Filename, logPath + "/" + logPostfix);

    /// init level log
    auto log_level = getLogLevel(pt.get<std::string>("log.level", "info"));
    defaultConf.set(el::Level::Info, el::ConfigurationType::ToFile, "false");
    defaultConf.set(el::Level::Warning, el::ConfigurationType::ToFile, "false");
    defaultConf.set(el::Level::Error, el::ConfigurationType::ToFile, "false");
    defaultConf.set(el::Level::Debug, el::ConfigurationType::ToFile, "false");
    defaultConf.set(el::Level::Trace, el::ConfigurationType::ToFile, "false");
    defaultConf.set(el::Level::Fatal, el::ConfigurationType::ToFile, "false");
    defaultConf.set(el::Level::Verbose, el::ConfigurationType::ToFile, "false");
    defaultConf.set(el::Level::Trace, el::ConfigurationType::Enabled, "false");
    defaultConf.set(el::Level::Debug, el::ConfigurationType::Enabled, "false");
    defaultConf.set(el::Level::Info, el::ConfigurationType::Enabled, "false");
    defaultConf.set(el::Level::Warning, el::ConfigurationType::Enabled, "false");
    switch (log_level)
    {
    case EasyLogLevel::TRACE:
        defaultConf.set(el::Level::Trace, el::ConfigurationType::Enabled, "true");
    case EasyLogLevel::DEBUG:
        defaultConf.set(el::Level::Debug, el::ConfigurationType::Enabled, "true");
    case EasyLogLevel::INFO:
        defaultConf.set(el::Level::Info, el::ConfigurationType::Enabled, "true");
    case EasyLogLevel::WARNNING:
        defaultConf.set(el::Level::Warning, el::ConfigurationType::Enabled, "true");
    case EasyLogLevel::ERROR:
        defaultConf.set(el::Level::Error, el::ConfigurationType::Enabled, "true");
    case EasyLogLevel::FATAL:
        defaultConf.set(el::Level::Fatal, el::ConfigurationType::Enabled, "true");
    }
    defaultConf.set(el::Level::Verbose, el::ConfigurationType::Enabled, "false");

    /// set allConf for globalLog
    el::Configurations allConf;
    allConf.set(defaultConf.get(el::Level::Global, el::ConfigurationType::Enabled));
    allConf.set(defaultConf.get(el::Level::Global, el::ConfigurationType::ToFile));
    allConf.set(defaultConf.get(el::Level::Global, el::ConfigurationType::ToStandardOutput));
    allConf.set(defaultConf.get(el::Level::Global, el::ConfigurationType::Format));
    allConf.set(defaultConf.get(el::Level::Global, el::ConfigurationType::Filename));
    allConf.set(defaultConf.get(el::Level::Global, el::ConfigurationType::SubsecondPrecision));
    allConf.set(defaultConf.get(el::Level::Global, el::ConfigurationType::MillisecondsWidth));
    allConf.set(defaultConf.get(el::Level::Global, el::ConfigurationType::PerformanceTracking));
    allConf.set(defaultConf.get(el::Level::Global, el::ConfigurationType::MaxLogFileSize));
    allConf.set(defaultConf.get(el::Level::Global, el::ConfigurationType::LogFlushThreshold));
    allConf.set(defaultConf.get(el::Level::Trace, el::ConfigurationType::Enabled));
    allConf.set(defaultConf.get(el::Level::Debug, el::ConfigurationType::Enabled));
    allConf.set(defaultConf.get(el::Level::Fatal, el::ConfigurationType::Enabled));
    allConf.set(defaultConf.get(el::Level::Error, el::ConfigurationType::Enabled));
    allConf.set(defaultConf.get(el::Level::Warning, el::ConfigurationType::Enabled));
    allConf.set(defaultConf.get(el::Level::Verbose, el::ConfigurationType::Enabled));
    allConf.set(defaultConf.get(el::Level::Info, el::ConfigurationType::Enabled));
    el::Loggers::reconfigureLogger("default", allConf);
    el::Loggers::reconfigureLogger(fileLogger, defaultConf);
    el::Helpers::installPreRollOutCallback(rolloutHandler);
}
