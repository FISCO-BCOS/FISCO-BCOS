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
 * @brief: define Log
 *
 * @file: Log.cpp
 * @author: yujiechen
 * @date 2018-12-04
 */
#include "Log.h"
#include <boost/log/core.hpp>

namespace dev
{
std::string const FileLogger = "FileLogger";
boost::log::sources::severity_channel_logger_mt<boost::log::trivial::severity_level, std::string>
    FileLoggerHandler(boost::log::keywords::channel = FileLogger);

std::string const StatFileLogger = "StatFileLogger";
boost::log::sources::severity_channel_logger_mt<boost::log::trivial::severity_level, std::string>
    StatFileLoggerHandler(boost::log::keywords::channel = StatFileLogger);

LogLevel c_fileLogLevel = LogLevel::TRACE;
LogLevel c_statLogLevel = LogLevel::INFO;

void setFileLogLevel(LogLevel const& _level)
{
    c_fileLogLevel = _level;
}

void setStatLogLevel(LogLevel const& _level)
{
    c_statLogLevel = _level;
}
}  // namespace dev