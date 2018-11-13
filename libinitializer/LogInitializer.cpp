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
#include "LogInitializer.h"

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

/// set easylog with default setting
void LogInitializer::initEasylogging(boost::property_tree::ptree const& pt)
{
    // Enables support for multiple loggers
    el::Loggers::addFlag(el::LoggingFlag::MultiLoggerSupport);
    el::Loggers::addFlag(el::LoggingFlag::MultiLoggerSupport);
    el::Loggers::addFlag(el::LoggingFlag::StrictLogFileSizeCheck);
    el::Loggers::setVerboseLevel(10);
    el::Configurations defaultConf;
    el::Logger* fileLogger = el::Loggers::getLogger("fileLogger");
    std::string logPath = pt.get<std::string>("log.LOG_PATH", "./log/");
    std::string logPostfix = "log_%datetime{%Y%M%d%H}.log";
    /// init the defaultConf
    /// init global configurations
    defaultConf.setGlobally(
        el::ConfigurationType::Enabled, pt.get<std::string>("log.GLOBAL-ENABLED", "true"));
    defaultConf.setGlobally(el::ConfigurationType::ToFile, "true");
    defaultConf.setGlobally(el::ConfigurationType::ToStandardOutput, "false");
    defaultConf.setGlobally(el::ConfigurationType::Format,
        pt.get<std::string>(
            "log.GLOBAL-FORMAT", "%level|%datetime{%Y-%M-%d %H:%m:%s:%g}|%file:%line|%msg"));
    defaultConf.setGlobally(el::ConfigurationType::MillisecondsWidth,
        pt.get<std::string>("log.GLOBAL-MILLISECONDS_WIDTH", "3"));
    defaultConf.setGlobally(el::ConfigurationType::PerformanceTracking,
        pt.get<std::string>("log.GLOBAL-PERFORMANCE_TRACKING", "false"));
    defaultConf.setGlobally(el::ConfigurationType::MaxLogFileSize,
        pt.get<std::string>("log.GLOBAL-MAX_LOG_FILE_SIZE", "209715200"));
    defaultConf.setGlobally(el::ConfigurationType::LogFlushThreshold,
        pt.get<std::string>("log.GLOBAL-LOG_FLUSH_THRESHOLD", "100"));
    defaultConf.setGlobally(el::ConfigurationType::Filename, logPath + "/" + logPostfix);
    /// init level log
    defaultConf.set(el::Level::Info, el::ConfigurationType::Enabled,
        pt.get<std::string>("log.INFO-ENABLED", "true"));
    defaultConf.set(
        el::Level::Info, el::ConfigurationType::Filename, logPath + "/info_" + logPostfix);

    defaultConf.set(el::Level::Warning, el::ConfigurationType::Enabled,
        pt.get<std::string>("log.WARNING-ENABLED", "true"));
    defaultConf.set(
        el::Level::Warning, el::ConfigurationType::Filename, logPath + "/warn_" + logPostfix);

    defaultConf.set(el::Level::Error, el::ConfigurationType::Enabled,
        pt.get<std::string>("log.ERROR-ENABLED", "true"));
    defaultConf.set(
        el::Level::Error, el::ConfigurationType::Filename, logPath + "/error_" + logPostfix);

    defaultConf.set(el::Level::Debug, el::ConfigurationType::Enabled,
        pt.get<std::string>("log.DEBUG-ENABLED", "false"));
    defaultConf.set(
        el::Level::Debug, el::ConfigurationType::Filename, logPath + "/debug_" + logPostfix);

    defaultConf.set(el::Level::Trace, el::ConfigurationType::Enabled,
        pt.get<std::string>("log.TRACE-ENABLED", "false"));
    defaultConf.set(
        el::Level::Trace, el::ConfigurationType::Filename, logPath + "/trace_" + logPostfix);

    defaultConf.set(el::Level::Fatal, el::ConfigurationType::Enabled,
        pt.get<std::string>("log.FATAL-ENABLED", "false"));
    defaultConf.set(
        el::Level::Fatal, el::ConfigurationType::Filename, logPath + "/fatal_" + logPostfix);

    defaultConf.set(el::Level::Verbose, el::ConfigurationType::Enabled,
        pt.get<std::string>("log.VERBOSE-ENABLED", "false"));
    defaultConf.set(
        el::Level::Verbose, el::ConfigurationType::Filename, logPath + "/verbose_" + logPostfix);
    el::Loggers::reconfigureLogger("default", defaultConf);
    el::Loggers::reconfigureLogger(fileLogger, defaultConf);
    el::Helpers::installPreRollOutCallback(rolloutHandler);
}