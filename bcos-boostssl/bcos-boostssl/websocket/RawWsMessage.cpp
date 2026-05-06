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
 */

#include <bcos-boostssl/websocket/RawWsMessage.h>

using namespace bcos::boostssl::ws;

RawWsMessage::RawWsMessage()
{
    m_payload = std::make_shared<bcos::bytes>();
    if (c_fileLogLevel == LogLevel::TRACE) [[unlikely]]
    {
        WEBSOCKET_MESSAGE(TRACE) << LOG_KV("[NEWOBJ][RawWsMessage]", this);
    }
}

RawWsMessage::~RawWsMessage()
{
    if (c_fileLogLevel == LogLevel::TRACE) [[unlikely]]
    {
        WEBSOCKET_MESSAGE(TRACE) << LOG_KV("[DELOBJ][RawWsMessage]", this);
    }
}

uint16_t RawWsMessage::version() const
{
    return 0;
}

void RawWsMessage::setVersion(uint16_t)
{}

uint16_t RawWsMessage::packetType() const
{
    return m_packetType;
}

void RawWsMessage::setPacketType(uint16_t)
{}

std::string const& RawWsMessage::seq() const
{
    return m_seq;
}

void RawWsMessage::setSeq(std::string _seq)
{
    m_seq = std::move(_seq);
}

std::shared_ptr<bcos::bytes> RawWsMessage::payload() const
{
    return m_payload;
}

void RawWsMessage::setPayload(std::shared_ptr<bcos::bytes> _payload)
{
    m_payload = std::move(_payload);
}

uint16_t RawWsMessage::ext() const
{
    return 0;
}

void RawWsMessage::setExt(uint16_t)
{}

bool RawWsMessage::encode(bcos::bytes& _buffer)
{
    _buffer.insert(_buffer.end(), m_payload->begin(), m_payload->end());
    return true;
}

int64_t RawWsMessage::decode(bytesConstRef _buffer)
{
    m_payload = std::make_shared<bcos::bytes>(_buffer.begin(), _buffer.end());
    return static_cast<int64_t>(_buffer.size());
}

bool RawWsMessage::isRespPacket() const
{
    return false;
}

void RawWsMessage::setRespPacket()
{}

uint32_t RawWsMessage::length() const
{
    return m_payload->size();
}

bcos::boostssl::MessageFace::Ptr RawWsMessageFactory::buildMessage()
{
    return std::make_shared<RawWsMessage>();
}