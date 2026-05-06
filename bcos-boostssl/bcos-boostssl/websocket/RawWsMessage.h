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
 * @file RawWsMessage.h
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
    RawWsMessage();

    RawWsMessage(const RawWsMessage&) = delete;
    RawWsMessage& operator=(const RawWsMessage&) = delete;
    RawWsMessage(RawWsMessage&&) = delete;
    RawWsMessage& operator=(RawWsMessage&&) = delete;
    ~RawWsMessage() override;

    uint16_t version() const override;
    void setVersion(uint16_t /*unused*/) override;
    uint16_t packetType() const override;
    void setPacketType(uint16_t _packetType) override;

    std::string const& seq() const override;
    void setSeq(std::string _seq) override;
    std::shared_ptr<bcos::bytes> payload() const override;
    void setPayload(std::shared_ptr<bcos::bytes> _payload) override;
    uint16_t ext() const override;
    void setExt(uint16_t /*unused*/) override;

    bool encode(bcos::bytes& _buffer) override;

    int64_t decode(bytesConstRef _buffer) override;

    bool isRespPacket() const override;
    void setRespPacket() override;

    uint32_t length() const override;

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

    boostssl::MessageFace::Ptr buildMessage() override;
};

}  // namespace bcos::boostssl::ws
