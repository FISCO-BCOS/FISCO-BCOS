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
 * @file Message.h
 * @author: octopus
 * @date 2021-05-06
 */

#pragma once

#include "Common.h"
#include <set>
#include <string>
#include <vector>

namespace boostssl
{
namespace net
{
class Message
{
public:
    using Ptr = std::shared_ptr<Message>;

public:
    virtual ~Message() {}

    virtual uint32_t length() const = 0;
    virtual uint32_t seq() const = 0;
    virtual uint16_t version() const = 0;
    virtual uint16_t packetType() const = 0;
    virtual uint16_t ext() const = 0;
    virtual bool isRespPacket() const = 0;
    virtual bool encode(std::vector<byte>& _buffer) = 0;
    virtual ssize_t decode(byte* _data, std::size_t size) = 0;
};

class MessageFactory
{
public:
    using Ptr = std::shared_ptr<MessageFactory>;

public:
    virtual ~MessageFactory() {}
    virtual Message::Ptr buildMessage() = 0;

    virtual uint32_t newSeq()
    {
        uint32_t seq = ++m_seq;
        return seq;
    }
    std::atomic<uint32_t> m_seq = {1};
};

}  // namespace net
}  // namespace boostssl