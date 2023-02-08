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

#include "bcos-utilities/CompositeBuffer.h"
#include <bcos-utilities/Common.h>
#include <set>
#include <string>
#include <vector>

namespace bcos
{
namespace gateway
{

class MessageExtAttributes
{
public:
    using Ptr = std::shared_ptr<MessageExtAttributes>;
    using ConstPtr = std::shared_ptr<const MessageExtAttributes>;

    MessageExtAttributes() = default;
    MessageExtAttributes(const MessageExtAttributes&) = delete;
    MessageExtAttributes(MessageExtAttributes&&) = delete;
    MessageExtAttributes& operator=(MessageExtAttributes&&) = delete;
    MessageExtAttributes& operator=(const MessageExtAttributes&) = delete;
    virtual ~MessageExtAttributes() = default;
};

class Message
{
public:
    using Ptr = std::shared_ptr<Message>;
    using ConstPtr = std::shared_ptr<const Message>;

    Message() = default;
    Message(const Message&) = delete;
    Message(Message&&) = delete;
    Message& operator=(Message&&) = delete;
    Message& operator=(const Message&) = delete;
    virtual ~Message() = default;

    virtual uint32_t length() const = 0;
    virtual uint32_t seq() const = 0;
    virtual uint16_t version() const = 0;
    virtual uint16_t packetType() const = 0;
    virtual uint16_t ext() const = 0;
    virtual bool isRespPacket() const = 0;

    [[deprecated("use encode(bcos::CompositeBuffer& _buffer)")]] virtual bool encode(
        bcos::bytes& _buffer) = 0;
    virtual bool encode(bcos::CompositeBuffer& _buffer) = 0;
    virtual int32_t decode(bytesConstRef _buffer) = 0;

    virtual MessageExtAttributes::Ptr extAttributes() = 0;

    // TODO: move the follow interfaces to P2PMessage
    virtual std::string const& srcP2PNodeID() const = 0;
    virtual std::string const& dstP2PNodeID() const = 0;
};

class MessageFactory
{
public:
    using Ptr = std::shared_ptr<MessageFactory>;

    MessageFactory() = default;
    MessageFactory(const MessageFactory&) = delete;
    MessageFactory(MessageFactory&&) = delete;
    MessageFactory& operator=(const MessageFactory&) = delete;
    MessageFactory&& operator=(MessageFactory&&) = delete;

    virtual ~MessageFactory() = default;
    virtual Message::Ptr buildMessage() = 0;
    virtual Message::Ptr buildMessage(uint16_t _type, CompositeBuffer::Ptr _data) = 0;

    virtual uint32_t newSeq()
    {
        uint32_t seq = ++m_seq;
        return seq;
    }

private:
    std::atomic<uint32_t> m_seq = {1};
};

}  // namespace gateway
}  // namespace bcos