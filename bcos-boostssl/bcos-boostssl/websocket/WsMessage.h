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
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-utilities/Common.h>
#include <memory>
#include <string>
#include <utility>

void CHECK_OFFSET(uint64_t offset, uint64_t length);

namespace bcos::boostssl::ws
{
// The message for ws protocol, two wire formats are supported:
// - header mode(default): version(2) + type(2) + status(2) + seqLength(2) + ext(2) + payload(N)
// - raw mode: payload(N) only, e.g. for web3 websocket connections.
// The mode is fixed at construction and never changes during the message lifetime.
class WsMessage
{
public:
    // version(2) + type(2) + status(2) + seqLength(2) + ext(2) + payload(N)
    const static size_t MESSAGE_MIN_LENGTH;

    using Ptr = std::shared_ptr<WsMessage>;

    explicit WsMessage(bool _raw = false);
    WsMessage(const WsMessage&) = delete;
    WsMessage& operator=(const WsMessage&) = delete;
    WsMessage(WsMessage&&) = default;
    WsMessage& operator=(WsMessage&&) = default;
    ~WsMessage();

    bool raw() const { return m_raw; }

    uint16_t version() const;
    void setVersion(uint16_t /*unused*/);
    uint16_t packetType() const;
    void setPacketType(uint16_t _packetType);
    int16_t status() const;
    void setStatus(int16_t _status);
    std::string const& seq() const;
    void setSeq(std::string _seq);
    bytesConstRef payload() const;
    void setPayload(bcos::bytes _payload);
    uint16_t ext() const;
    void setExt(uint16_t _ext);

    bool encode(bcos::bytes& _buffer) const;
    int64_t decode(bytesConstRef _buffer);

    bool isRespPacket() const;
    void setRespPacket();

private:
    bool m_raw = false;
    uint16_t m_version = 0;
    uint16_t m_packetType = 0;
    std::string m_seq;
    uint16_t m_ext = 0;
    bcos::bytes m_payload;

    int16_t m_status = 0;
};

// generate a 32-char hex seq string
std::string newSeq();

}  // namespace bcos::boostssl::ws
