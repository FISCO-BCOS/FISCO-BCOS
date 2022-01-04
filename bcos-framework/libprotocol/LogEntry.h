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
#include "../interfaces/crypto/CryptoSuite.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/FixedBytes.h"
#include <gsl/span>

namespace bcos
{
namespace protocol
{
class LogEntry
{
public:
    using Ptr = std::shared_ptr<LogEntry>;
    LogEntry() = default;
    LogEntry(bytes const& _address, h256s _topics, bytes _data)
      : m_address(_address), m_topics(std::move(_topics)), m_data(std::move(_data))
    {}

    ~LogEntry() {}

    std::string_view address() const
    {
        return std::string_view((char*)m_address.data(), m_address.size());
    }
    gsl::span<const h256> topics() const { return gsl::span(m_topics.data(), m_topics.size()); }
    bytesConstRef data() const { return ref(m_data); }
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
    bcos::bytes m_address;
    bcos::h256s m_topics;
    bytes m_data;
};

using LogEntries = std::vector<LogEntry>;
using LogEntriesPtr = std::shared_ptr<std::vector<LogEntry>>;
}  // namespace protocol
}  // namespace bcos