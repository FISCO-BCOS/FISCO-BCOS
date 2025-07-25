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

inline void CHECK_OFFSET(uint64_t offset, uint64_t length)
{
    if (std::cmp_greater(offset, length))
    {
        throw std::out_of_range("Out of range error, offset:" + std::to_string(offset) +
                                " ,length: " + std::to_string(length));
    }
}

namespace bcos::boostssl::ws
{
// the message format for ws protocol
class WsMessage : public boostssl::MessageFace, public bcos::ObjectCounter<WsMessage>
{
public:
    // version(2) + type(2) + status(2) + seqLength(2) + ext(2) + payload(N)
    const static size_t MESSAGE_MIN_LENGTH;

    using Ptr = std::shared_ptr<WsMessage>;
    WsMessage()
    {
        m_payload = std::make_shared<bcos::bytes>();
        if (c_fileLogLevel == LogLevel::TRACE) [[unlikely]]
        {
            WEBSOCKET_MESSAGE(TRACE) << LOG_KV("[NEWOBJ][WsMessage]", this);
        }
    }
    WsMessage(const WsMessage&) = delete;
    WsMessage& operator=(const WsMessage&) = delete;
    WsMessage(WsMessage&&) = delete;
    WsMessage& operator=(WsMessage&&) = delete;
    ~WsMessage() override
    {
        if (c_fileLogLevel == LogLevel::TRACE) [[unlikely]]
        {
            WEBSOCKET_MESSAGE(TRACE) << LOG_KV("[DELOBJ][WsMessage]", this);
        }
    }


    uint16_t version() const override { return m_version; }
    void setVersion(uint16_t /*unused*/) override {}
    uint16_t packetType() const override { return m_packetType; }
    void setPacketType(uint16_t _packetType) override { m_packetType = _packetType; }
    int16_t status() const { return m_status; }
    void setStatus(int16_t _status) { m_status = _status; }
    std::string const& seq() const override { return m_seq; }
    void setSeq(std::string _seq) override { m_seq = _seq; }
    std::shared_ptr<bcos::bytes> payload() const override { return m_payload; }
    void setPayload(std::shared_ptr<bcos::bytes> _payload) override
    {
        m_payload = std::move(_payload);
    }
    uint16_t ext() const override { return m_ext; }
    void setExt(uint16_t _ext) override { m_ext = _ext; }


    bool encode(bcos::bytes& _buffer) override;
    int64_t decode(bytesConstRef _buffer) override;

    bool isRespPacket() const override
    {
        return (m_ext & bcos::protocol::MessageExtFieldFlag::RESPONSE) != 0;
    }
    void setRespPacket() override { m_ext |= bcos::protocol::MessageExtFieldFlag::RESPONSE; }

    uint32_t length() const override { return m_length; }

private:
    uint16_t m_version = 0;
    uint16_t m_packetType = 0;
    std::string m_seq;
    uint16_t m_ext = 0;
    std::shared_ptr<bcos::bytes> m_payload;

    int16_t m_status = 0;
    uint32_t m_length = 0;
};

class WsMessageFactory : public boostssl::MessageFaceFactory
{
public:
    using Ptr = std::shared_ptr<WsMessageFactory>;
    WsMessageFactory() = default;
    WsMessageFactory(const WsMessageFactory&) = delete;
    WsMessageFactory& operator=(const WsMessageFactory&) = delete;
    WsMessageFactory(WsMessageFactory&&) = delete;
    WsMessageFactory& operator=(WsMessageFactory&&) = delete;
    ~WsMessageFactory() override = default;

    boostssl::MessageFace::Ptr buildMessage() override
    {
        auto msg = std::make_shared<WsMessage>();
        return msg;
    }
};

}  // namespace bcos::boostssl::ws
