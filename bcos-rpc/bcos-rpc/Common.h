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
 * @file Common.h
 * @author: octopus
 * @date 2021-07-02
 */
#pragma once
#include "bcos-utilities/Common.h"
#include <bcos-framework/Common.h>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/stream.hpp>
#include <iostream>
#include <memory>

#define RPC_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("RPC")

namespace bcos::rpc
{
enum AMOPClientMessageType
{
    AMOP_SUBTOPIC = 0x110,   // 272
    AMOP_REQUEST = 0x111,    // 273
    AMOP_BROADCAST = 0x112,  // 274
    AMOP_RESPONSE = 0x113    // 275
};
class JsonSink
{
public:
    typedef char char_type;
    typedef boost::iostreams::sink_tag category;

    explicit JsonSink(bcos::bytes& buffer) : m_buffer(buffer) {}

    std::streamsize write(const char* s, std::streamsize n)
    {
        m_buffer.insert(m_buffer.end(), (bcos::byte*)s, (bcos::byte*)s + n);
        return n;
    }

    bcos::bytes& m_buffer;
};

constexpr const std::string_view EarliestBlock{"earliest"};
constexpr const std::string_view LatestBlock{"latest"};
constexpr const std::string_view PendingBlock{"pending"};
constexpr const std::string_view SafeBlock{"safe"};
constexpr const std::string_view FinalizedBlock{"finalized"};

}  // namespace bcos::rpc