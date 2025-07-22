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
 * @file WsMessage.h
 * @author: octopus
 * @date 2021-07-28
 */
#pragma once
#include "bcos-boostssl/websocket/Common.h"
#include <bcos-boostssl/interfaces/MessageFace.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/ObjectCounter.h>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <memory>
#include <string>
#include <utility>

namespace bcos::boostssl::ws
{
class RawWsMessage : public boostssl::MessageFace, public bcos::ObjectCounter<RawWsMessage>
{
public:
    using Ptr = std::shared_ptr<RawWsMessage>;
    RawWsMessage()
    {
        m_payload = std::make_shared<bcos::bytes>();
        if (c_fileLogLevel == LogLevel::TRACE) [[unlikely]]
        {
            WEBSOCKET_MESSAGE(TRACE) << LOG_KV("[NEWOBJ][RawWsMessage]", this);
        }
    }

    RawWsMessage(const RawWsMessage&) = delete;
    RawWsMessage& operator=(const RawWsMessage&) = delete;
    RawWsMessage(RawWsMessage&&) = delete;
    RawWsMessage& operator=(RawWsMessage&&) = delete;
    ~RawWsMessage() override
    {
        if (c_fileLogLevel == LogLevel::TRACE) [[unlikely]]
        {
            WEBSOCKET_MESSAGE(TRACE) << LOG_KV("[DELOBJ][RawWsMessage]", this);
        }
    }

    uint16_t version() const override { return 0; }
    void setVersion(uint16_t /*unused*/) override {}
    uint16_t packetType() const override { return m_packetType; }
    void setPacketType(uint16_t _packetType) override {}

    std::string const& seq() const override { return m_seq; }
    void setSeq(std::string _seq) override { m_seq = _seq; }
    std::shared_ptr<bcos::bytes> payload() const override { return m_payload; }
    void setPayload(std::shared_ptr<bcos::bytes> _payload) override
    {
        m_payload = std::move(_payload);
    }
    uint16_t ext() const override { return 0; }
    void setExt(uint16_t /*unused*/) override {}

    bool encode(bcos::bytes& _buffer) override
    {
        _buffer.insert(_buffer.end(), m_payload->begin(), m_payload->end());
        return true;
    }

    int64_t decode(bytesConstRef _buffer) override
    {
        m_payload = std::make_shared<bcos::bytes>(_buffer.begin(), _buffer.end());
        return static_cast<int64_t>(_buffer.size());
    }

    bool isRespPacket() const override { return false; }
    void setRespPacket() override {}

    uint32_t length() const override { return m_payload->size(); }

private:
    uint16_t m_packetType = WS_RAW_MESSAGE_TYPE;
    std::string m_seq;
    std::shared_ptr<bcos::bytes> m_payload;
};

class RawWsMessageFactory : public MessageFaceFactory
{
public:
    using Ptr = std::shared_ptr<RawWsMessageFactory>;
    RawWsMessageFactory() = default;
    RawWsMessageFactory(const RawWsMessageFactory&) = delete;
    RawWsMessageFactory& operator=(const RawWsMessageFactory&) = delete;
    RawWsMessageFactory(RawWsMessageFactory&&) = delete;
    RawWsMessageFactory& operator=(RawWsMessageFactory&&) = delete;
    ~RawWsMessageFactory() override = default;

    boostssl::MessageFace::Ptr buildMessage() override
    {
        auto msg = std::make_shared<RawWsMessage>();
        return msg;
    }
};

}  // namespace bcos::boostssl::ws
