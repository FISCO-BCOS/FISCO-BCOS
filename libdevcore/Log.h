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
 * @file: Log.h
 * @author: yujiechen
 * @date 2018-12-04
 */
#pragma once

#include <boost/log/attributes/constant.hpp>
#include <boost/log/attributes/scoped_attribute.hpp>
#include <boost/log/sources/severity_channel_logger.hpp>
#include <boost/log/trivial.hpp>

// BCOS log format
#define LOG_BADGE(_NAME) "[" << (_NAME) << "]"
#define LOG_DESC(_DESCRIPTION) (_DESCRIPTION)
#define LOG_KV(_K, _V) "," << (_K) << "=" << (_V)

namespace dev
{
extern std::string const FileLogger;
/// the file logger
extern boost::log::sources::severity_channel_logger_mt<boost::log::trivial::severity_level,
    std::string>
    FileLoggerHandler;

enum LogLevel
{
    FATAL = boost::log::trivial::fatal,
    ERROR = boost::log::trivial::error,
    WARNING = boost::log::trivial::warning,
    INFO = boost::log::trivial::info,
    DEBUG = boost::log::trivial::debug,
    TRACE = boost::log::trivial::trace
};

#define LOG(level)                        \
    BOOST_LOG_SEV(dev::FileLoggerHandler, \
        (boost::log::v2s_mt_posix::trivial::severity_level)(dev::LogLevel::level))
}  // namespace dev
