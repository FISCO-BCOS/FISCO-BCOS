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
#include <libdevcore/easylog.h>
#include <map>
namespace dev
{
namespace initializer
{
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
class LogInitializer
{
public:
    typedef std::shared_ptr<LogInitializer> Ptr;
    LogInitializer(std::string const& logPath) : m_logPath(logPath) {}
    void inline initEasylogging()
    {
        // Enables support for multiple loggers
        el::Loggers::addFlag(el::LoggingFlag::MultiLoggerSupport);
        el::Loggers::addFlag(el::LoggingFlag::StrictLogFileSizeCheck);
        el::Loggers::setVerboseLevel(10);
        if (el::base::utils::File::pathExists(m_logPath.c_str(), true))
        {
            el::Logger* fileLogger = el::Loggers::getLogger("fileLogger");  // Register new logger
            el::Configurations conf(m_logPath.c_str());
            el::Configurations allConf;
            allConf.set(conf.get(el::Level::Global, el::ConfigurationType::Enabled));
            allConf.set(conf.get(el::Level::Global, el::ConfigurationType::ToFile));
            allConf.set(conf.get(el::Level::Global, el::ConfigurationType::ToStandardOutput));
            allConf.set(conf.get(el::Level::Global, el::ConfigurationType::Format));
            allConf.set(conf.get(el::Level::Global, el::ConfigurationType::Filename));
            allConf.set(conf.get(el::Level::Global, el::ConfigurationType::SubsecondPrecision));
            allConf.set(conf.get(el::Level::Global, el::ConfigurationType::MillisecondsWidth));
            allConf.set(conf.get(el::Level::Global, el::ConfigurationType::PerformanceTracking));
            allConf.set(conf.get(el::Level::Global, el::ConfigurationType::MaxLogFileSize));
            allConf.set(conf.get(el::Level::Global, el::ConfigurationType::LogFlushThreshold));
            allConf.set(conf.get(el::Level::Trace, el::ConfigurationType::Enabled));
            allConf.set(conf.get(el::Level::Debug, el::ConfigurationType::Enabled));
            allConf.set(conf.get(el::Level::Fatal, el::ConfigurationType::Enabled));
            allConf.set(conf.get(el::Level::Error, el::ConfigurationType::Enabled));
            allConf.set(conf.get(el::Level::Warning, el::ConfigurationType::Enabled));
            allConf.set(conf.get(el::Level::Verbose, el::ConfigurationType::Enabled));
            allConf.set(conf.get(el::Level::Info, el::ConfigurationType::Enabled));
            el::Loggers::reconfigureLogger("default", allConf);
            el::Loggers::reconfigureLogger(fileLogger, conf);
        }
        el::Helpers::installPreRollOutCallback(rolloutHandler);
    }

    static void inline logRotateByTime()
    {
        if (std::chrono::system_clock::now() <= nextWakeUp)
            return;
        nextWakeUp = std::chrono::system_clock::now() + wakeUpDelta;
        auto L = el::Loggers::getLogger("default");
        if (L == nullptr)
        {
            LOG(ERROR) << "Oops, it is not called default!";
        }
        else
        {
            L->reconfigure();
        }

        L = el::Loggers::getLogger("fileLogger");
        if (L == nullptr)
        {
            LOG(ERROR) << "Oops, it is not called fileLogger!";
        }
        else
        {
            L->reconfigure();
        }
    }

private:
    std::string m_logPath;
    static const std::chrono::seconds wakeUpDelta;
    static std::chrono::system_clock::time_point nextWakeUp;
};
}  // namespace initializer
}  // namespace dev