/*
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
 * @file LogEntry.h
 * @author: yujiechen
 * @date: 2021-03-18
 */
#pragma once
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <gsl/span>

namespace bcos::protocol
{
class LogEntry
{
public:
    using Ptr = std::shared_ptr<LogEntry>;
    LogEntry() = default;
    LogEntry(const LogEntry&) = default;
    LogEntry(LogEntry&&) noexcept = default;
    LogEntry& operator=(const LogEntry&) = default;
    LogEntry& operator=(LogEntry&&) noexcept = default;
    LogEntry(bytes address, h256s topics, bytes data)
      : m_address(std::move(address)), m_topics(std::move(topics)), m_data(std::move(data))
    {}
    ~LogEntry() noexcept = default;

    std::string_view address() const { return {(const char*)m_address.data(), m_address.size()}; }
    gsl::span<const bcos::h256> topics() const { return {m_topics.data(), m_topics.size()}; }
    bcos::bytesConstRef data() const { return ref(m_data); }
    bytes&& takeAddress() { return std::move(m_address); }
    h256s&& takeTopics() { return std::move(m_topics); }
    bytes&& takeData() { return std::move(m_data); }
    // Define the scale decode method, which cannot be modified at will
    template <class Stream, typename = std::enable_if_t<Stream::is_decoder_stream>>
    friend Stream& operator>>(Stream& _stream, LogEntry& _logEntry)
    {
        return _stream >> _logEntry.m_address >> _logEntry.m_topics >> _logEntry.m_data;
    }

    // Define the scale encode method, which cannot be modified at will
    template <class Stream, typename = std::enable_if_t<Stream::is_encoder_stream>>
    friend Stream& operator<<(Stream& _stream, LogEntry const& _logEntry)
    {
        return _stream << _logEntry.m_address << _logEntry.m_topics << _logEntry.m_data;
    }

private:
    bytes m_address;
    h256s m_topics;
    bytes m_data;
};

using LogEntries = std::vector<LogEntry>;
using LogEntriesPtr = std::shared_ptr<std::vector<LogEntry>>;
}  // namespace bcos::protocol
