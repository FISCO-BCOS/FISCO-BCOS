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
 * @file P2PDecoder.h
 * @brief The FrameDecoder (see libnetwork/FrameMeta.h) for the gateway P2P wire format.
 *        libnetwork's session machinery is generic over this decoder; the Message type and
 *        every wire-format detail live here in libp2p.
 */

#pragma once

#include "bcos-framework/protocol/Protocol.h"
#include "bcos-gateway/Common.h"
#include "bcos-gateway/libnetwork/FrameMeta.h"
#include "bcos-gateway/libnetwork/Host.h"
#include "bcos-gateway/libnetwork/Session.h"
#include "bcos-gateway/libp2p/Message.h"
#include <boost/asio/detail/socket_ops.hpp>

namespace bcos::gateway
{
/// Stream decoder for the gateway P2P wire format (the frame layout is documented on Message).
/// Stateless: every frame is self-delimiting via its 4-byte length prefix, so the decoder
/// instance carries no state. tryDecode mirrors the header validation of Message::decodeHeader
/// (FIB-66) plus the MAX_MESSAGE_LENGTH bound, is bounds-safe on arbitrary attacker-controlled
/// input and never throws (FrameDecoder contract).
class P2PDecoder
{
public:
    FrameMeta tryDecode(const bytesConstRef& buffer) const noexcept
    {
        FrameMeta meta;
        if (buffer.size() < Message::MESSAGE_HEADER_LENGTH)
        {
            meta.status = FrameMeta::Status::NeedMoreData;
            meta.declaredLength = Message::MESSAGE_HEADER_LENGTH;
            return meta;
        }

        const byte* data = buffer.data();
        uint32_t length =
            boost::asio::detail::socket_ops::network_to_host_long(*((const uint32_t*)data));
        if (length < Message::MESSAGE_HEADER_LENGTH ||
            std::cmp_greater(length, MAX_MESSAGE_LENGTH)) [[unlikely]]
        {
            meta.status = FrameMeta::Status::ProtocolError;
            return meta;
        }
        if (buffer.size() < length)
        {
            meta.status = FrameMeta::Status::NeedMoreData;
            meta.declaredLength = length;
            return meta;
        }

        // version (offset 4)
        uint16_t version =
            boost::asio::detail::socket_ops::network_to_host_short(*((const uint16_t*)(data + 4)));
        if (version > static_cast<uint16_t>(bcos::protocol::ProtocolVersion::V3)) [[unlikely]]
        {
            meta.status = FrameMeta::Status::ProtocolError;
            return meta;
        }

        // seq (offset 8) + ext (offset 12)
        meta.seq =
            boost::asio::detail::socket_ops::network_to_host_long(*((const uint32_t*)(data + 8)));
        uint16_t ext =
            boost::asio::detail::socket_ops::network_to_host_short(*((const uint16_t*)(data + 12)));
        meta.isResp = (ext & bcos::protocol::MessageExtFieldFlag::RESPONSE) != 0;

        // Extended header (version > V0): ttl(2) + srcP2PNodeID + dstP2PNodeID. Only the
        // destination id is needed at the session layer, for the routed-message bypass in
        // BasicSession::onMessage; the full parse happens later in Message::decode.
        if (version > static_cast<uint16_t>(bcos::protocol::ProtocolVersion::V0))
        {
            uint32_t offset = Message::MESSAGE_HEADER_LENGTH + 2;  // skip ttl
            auto readNodeID = [&](std::string* out) {
                if (offset + 2 > length) [[unlikely]]
                {
                    return false;
                }
                uint16_t nodeIDLen = boost::asio::detail::socket_ops::network_to_host_short(
                    *((const uint16_t*)(data + offset)));
                offset += 2;
                if (offset + nodeIDLen > length) [[unlikely]]
                {
                    return false;
                }
                if (out != nullptr)
                {
                    out->assign(data + offset, data + offset + nodeIDLen);
                }
                offset += nodeIDLen;
                return true;
            };
            if (!readNodeID(nullptr) || !readNodeID(&meta.dstID)) [[unlikely]]
            {
                meta.status = FrameMeta::Status::ProtocolError;
                return meta;
            }
        }

        meta.status = FrameMeta::Status::Frame;
        meta.consumed = length;
        meta.declaredLength = length;
        // Owned copy: dispatch is asynchronous while the read buffer is reused and resized.
        meta.frame.assign(data, data + length);
        return meta;
    }
};

// The gateway's session type: the generic libnetwork session machinery instantiated with the
// P2P wire decoder. No `Session` alias remains in libnetwork.
using Session = BasicSession<P2PDecoder>;
using P2PSessionFactory = BasicSessionFactory<P2PDecoder>;
// The gateway's host type: the generic libnetwork Host instantiated with the P2P wire decoder.
using P2PHost = Host<P2PDecoder>;
}  // namespace bcos::gateway
