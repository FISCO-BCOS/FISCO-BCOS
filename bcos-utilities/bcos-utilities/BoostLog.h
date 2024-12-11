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
 * @brief: define Log
 *
 * @file: Log.h
 * @author: yujiechen
 * @date 2021-02-24
 */
#pragma once

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#endif

#ifdef ERROR
#undef ERROR
#endif

#ifdef TRACE
#undef TRACE
#endif

#ifdef INFO
#undef INFO
#endif

#ifdef WARNING
#undef WARNING
#endif

#ifdef FATAL
#undef FATAL
#endif

#include <boost/log/attributes/constant.hpp>
#include <boost/log/attributes/scoped_attribute.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/sources/severity_channel_logger.hpp>
#include <boost/log/trivial.hpp>

// BCOS log format
#ifndef LOG_BADGE
#define LOG_BADGE(_NAME) "[" << (_NAME) << "]"
#endif

#ifndef LOG_TYPE
#define LOG_TYPE(_TYPE) (_TYPE) << "|"
#endif

#ifndef LOG_DESC
#define LOG_DESC(_DESCRIPTION) (_DESCRIPTION)
#endif

#ifndef LOG_KV
#define LOG_KV(_K, _V) "," << (_K) << "=" << (_V)
#endif

namespace bcos
{
extern std::string const FileLogger;
/// the file logger
extern boost::log::sources::severity_channel_logger_mt<boost::log::trivial::severity_level,
    std::string>
    FileLoggerHandler;

// the statFileLogger
extern std::string const StatFileLogger;
extern boost::log::sources::severity_channel_logger_mt<boost::log::trivial::severity_level,
    std::string>
    StatFileLoggerHandler;

enum LogLevel
{
    TRACE = boost::log::trivial::severity_level::trace,
    DEBUG = boost::log::trivial::severity_level::debug,
    INFO = boost::log::trivial::severity_level::info,
    WARNING = boost::log::trivial::severity_level::warning,
    ERROR = boost::log::trivial::severity_level::error,
    FATAL = boost::log::trivial::severity_level::fatal,
};

extern LogLevel c_fileLogLevel;
extern LogLevel c_statLogLevel;

constexpr auto operator<=>(LogLevel const& _lhs, auto const& _rhs)
    requires(std::same_as<decltype(_rhs), LogLevel> || std::integral<decltype(_rhs)>)
{
    return static_cast<int>(_lhs) <=> static_cast<int>(_rhs);
}

void setFileLogLevel(LogLevel const& _level);
void setStatLogLevel(LogLevel const& _level);

#define BCOS_LOG(level)                                \
    if (bcos::LogLevel::level >= bcos::c_fileLogLevel) \
    BOOST_LOG_SEV(                                     \
        bcos::FileLoggerHandler, (boost::log::trivial::severity_level)(bcos::LogLevel::level))
// for block number log
#define BLOCK_NUMBER(NUMBER) "[blk-" << (NUMBER) << "]"

namespace log
{
boost::shared_ptr<boost::log::sinks::file::collector> make_collector(
    boost::filesystem::path const& target_dir, uintmax_t max_size, uintmax_t min_free_space,
    uintmax_t max_files, bool convert_tar_gz);
}

}  // namespace bcos