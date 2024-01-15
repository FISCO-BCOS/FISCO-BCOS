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
 * @file: Log.cpp
 * @author: yujiechen
 * @date 2021-02-24
 */
#include "Log.h"
#include <boost/log/core.hpp>

std::string const bcos::FileLogger = "FileLogger";
boost::log::sources::severity_channel_logger_mt<boost::log::trivial::severity_level, std::string>
    bcos::FileLoggerHandler(boost::log::keywords::channel = bcos::FileLogger);

std::string const bcos::StatFileLogger = "StatFileLogger";
boost::log::sources::severity_channel_logger_mt<boost::log::trivial::severity_level, std::string>
    bcos::StatFileLoggerHandler(boost::log::keywords::channel = bcos::StatFileLogger);

bcos::LogLevel bcos::c_fileLogLevel = bcos::LogLevel::TRACE;
bcos::LogLevel bcos::c_statLogLevel = bcos::LogLevel::INFO;

void bcos::setFileLogLevel(bcos::LogLevel const& _level)
{
    bcos::c_fileLogLevel = _level;
}

void bcos::setStatLogLevel(bcos::LogLevel const& _level)
{
    bcos::c_statLogLevel = _level;
}

bcos::Logger::Logger(LogLevel level)
  : m_record(bcos::FileLoggerHandler.open_record(
        boost::log::keywords::severity = (boost::log::trivial::severity_level)level)),
    m_stream(m_record),
    m_level(level)
{}

bcos::Logger::~Logger() noexcept
{
    if (m_level >= bcos::c_fileLogLevel)
    {
        m_stream.flush();
        bcos::FileLoggerHandler.push_record(std::move(m_record));
    }
}
