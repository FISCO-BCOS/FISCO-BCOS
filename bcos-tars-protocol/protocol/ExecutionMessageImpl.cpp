/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @brief tars implementation for ExecutionMessage
 * @file ExecutionMessageImpl.cpp
 * @author: yujiechen
 * @date 2022-05-09
 */
#include "ExecutionMessageImpl.h"
#include "bcos-tars-protocol/Common.h"
using namespace bcostars;
using namespace bcostars::protocol;

void ExecutionMessageImpl::decodeLogEntries()
{
    m_logEntries.reserve(m_inner()->logEntries.size());
    for (auto& it : m_inner()->logEntries)
    {
        auto bcosLogEntry = toBcosLogEntry(it);
        m_logEntries.emplace_back(std::move(bcosLogEntry));
    }
}

void ExecutionMessageImpl::decodeKeyLocks()
{
    for (auto const& _keyLock : m_inner()->keyLocks)
    {
        m_keyLocks.emplace_back(_keyLock);
    }
}

gsl::span<bcos::protocol::LogEntry const> const ExecutionMessageImpl::logEntries() const
{
    return m_logEntries;
}
std::vector<bcos::protocol::LogEntry> ExecutionMessageImpl::takeLogEntries()
{
    return std::move(m_logEntries);
}

void ExecutionMessageImpl::setLogEntries(std::vector<bcos::protocol::LogEntry> _logEntries)
{
    m_logEntries = _logEntries;
    m_inner()->logEntries.clear();
    m_inner()->logEntries.reserve(_logEntries.size());

    for (auto& it : _logEntries)
    {
        auto tarsLogEntry = toTarsLogEntry(it);
        m_inner()->logEntries.emplace_back(std::move(tarsLogEntry));
    }
}

gsl::span<std::string const> ExecutionMessageImpl::keyLocks() const
{
    return m_keyLocks;
}
std::vector<std::string> ExecutionMessageImpl::takeKeyLocks()
{
    return std::move(m_keyLocks);
}

void ExecutionMessageImpl::setKeyLocks(std::vector<std::string> keyLocks)
{
    m_keyLocks = std::move(keyLocks);
    for (auto const& keyLock : m_keyLocks)
    {
        m_inner()->keyLocks.emplace_back(keyLock);
    }
}