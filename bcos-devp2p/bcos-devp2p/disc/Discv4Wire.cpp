/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file Discv4Wire.cpp
 * @brief Node Discovery v4 packet codec.
 * @date 2026/11/3
 */
#include "Discv4Wire.h"
#include "../RlpTake.h"
#include "../Try.h"
#include "../rlpx/Crypto.h"
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <boost/asio/ip/address_v6.hpp>
#include <algorithm>
#include <array>
#include <stdexcept>

namespace bcos::devp2p::disc
{
using bcos::codec::rlp::RlpResult;
using bcos::devp2p::detail::genericError;
using bcos::devp2p::detail::take;
using bcos::devp2p::detail::takeListPayload;
using bcos::devp2p::detail::takeRawItem;

char const* packetName(PacketType _type)
{
    switch (_type)
    {
    case PacketType::Ping:
        return "PING";
    case PacketType::Pong:
        return "PONG";
    case PacketType::FindNode:
        return "FINDNODE";
    case PacketType::Neighbors:
        return "NEIGHBORS";
    case PacketType::EnrRequest:
        return "ENRREQUEST";
    case PacketType::EnrResponse:
        return "ENRRESPONSE";
    }
    return "UNKNOWN";
}

std::string Endpoint::ipString() const
{
    if (isV4())
    {
        return std::to_string(ip[0]) + "." + std::to_string(ip[1]) + "." + std::to_string(ip[2]) +
               "." + std::to_string(ip[3]);
    }
    if (isV6())
    {
        boost::asio::ip::address_v6::bytes_type raw{};
        std::copy(ip.begin(), ip.end(), raw.begin());
        return boost::asio::ip::address_v6(raw).to_string();
    }
    return {};
}

PacketType packetType(Packet const& _packet)
{
    return std::visit(
        [](auto const& _p) -> PacketType {
            using T = std::decay_t<decltype(_p)>;
            if constexpr (std::is_same_v<T, PingPacket>)
            {
                return PacketType::Ping;
            }
            else if constexpr (std::is_same_v<T, PongPacket>)
            {
                return PacketType::Pong;
            }
            else if constexpr (std::is_same_v<T, FindNodePacket>)
            {
                return PacketType::FindNode;
            }
            else if constexpr (std::is_same_v<T, NeighborsPacket>)
            {
                return PacketType::Neighbors;
            }
            else if constexpr (std::is_same_v<T, EnrRequestPacket>)
            {
                return PacketType::EnrRequest;
            }
            else
            {
                return PacketType::EnrResponse;
            }
        },
        _packet);
}

// ---------------------------------------------------------------------------
// encoding
// ---------------------------------------------------------------------------
namespace
{
void appendItem(bcos::bytes& _out, uint64_t _value)
{
    bcos::codec::rlp::encode(_out, _value);
}

void appendItem(bcos::bytes& _out, bytesConstRef _value)
{
    bcos::codec::rlp::encode(_out, _value);
}

void appendList(bcos::bytes& _out, bytesConstRef _payload)
{
    bcos::codec::rlp::encodeHeader(_out, {.isList = true, .payloadLength = _payload.size()});
    _out.insert(_out.end(), _payload.begin(), _payload.end());
}

bcos::bytes endpointPayload(Endpoint const& _endpoint)
{
    bcos::bytes payload;
    appendItem(payload, bcos::ref(_endpoint.ip));
    appendItem(payload, _endpoint.udp);
    appendItem(payload, _endpoint.tcp);
    return payload;
}

void appendEndpoint(bcos::bytes& _out, Endpoint const& _endpoint)
{
    appendList(_out, bcos::ref(endpointPayload(_endpoint)));
}

bcos::bytes packetData(Packet const& _packet)
{
    bcos::bytes payload;
    std::visit(
        [&](auto const& _p) {
            using T = std::decay_t<decltype(_p)>;
            if constexpr (std::is_same_v<T, PingPacket>)
            {
                appendItem(payload, _p.version);
                appendEndpoint(payload, _p.from);
                appendEndpoint(payload, _p.to);
                appendItem(payload, _p.expiration);
                if (_p.enrSeq != 0)
                {
                    appendItem(payload, _p.enrSeq);
                }
            }
            else if constexpr (std::is_same_v<T, PongPacket>)
            {
                appendEndpoint(payload, _p.to);
                appendItem(payload, bcos::ref(_p.pingHash));
                appendItem(payload, _p.expiration);
                if (_p.enrSeq != 0)
                {
                    appendItem(payload, _p.enrSeq);
                }
            }
            else if constexpr (std::is_same_v<T, FindNodePacket>)
            {
                appendItem(payload, bcos::ref(_p.target));
                appendItem(payload, _p.expiration);
            }
            else if constexpr (std::is_same_v<T, NeighborsPacket>)
            {
                bcos::bytes nodes;
                for (auto const& node : _p.nodes)
                {
                    auto nodePayload = endpointPayload(node.endpoint);
                    appendItem(nodePayload, bcos::ref(node.id));
                    appendList(nodes, bcos::ref(nodePayload));
                }
                appendList(payload, bcos::ref(nodes));
                appendItem(payload, _p.expiration);
            }
            else if constexpr (std::is_same_v<T, EnrRequestPacket>)
            {
                appendItem(payload, _p.expiration);
            }
            else
            {
                appendItem(payload, bcos::ref(_p.requestHash));
                auto record = _p.record.encode();
                payload.insert(payload.end(), record.begin(), record.end());
            }
        },
        _packet);
    bcos::bytes out;
    appendList(out, bcos::ref(payload));
    return out;
}
}  // namespace

EncodedPacket encodePacket(Packet const& _packet, bytesConstRef _privateKey)
{
    // datagram = hash(32) || sig(65) || type(1) || rlp
    bcos::bytes datagram(c_packetHeaderSize, 0);
    datagram.push_back(static_cast<bcos::byte>(packetType(_packet)));
    auto data = packetData(_packet);
    datagram.insert(datagram.end(), data.begin(), data.end());
    bytesConstRef signed_(
        datagram.data() + c_packetHeaderSize, datagram.size() - c_packetHeaderSize);
    auto signature = rlpx::signRecoverable(bcos::ref(rlpx::keccak256(signed_)), _privateKey);
    std::copy(signature.begin(), signature.end(), datagram.begin() + c_packetHashSize);
    bytesConstRef hashed(datagram.data() + c_packetHashSize, datagram.size() - c_packetHashSize);
    auto hash = rlpx::keccak256(hashed);
    std::copy(hash.begin(), hash.end(), datagram.begin());
    if (datagram.size() > c_maxPacketSize)
    {
        throw std::invalid_argument("discv4: packet exceeds 1280 bytes");
    }
    return {std::move(datagram), std::move(hash)};
}

// ---------------------------------------------------------------------------
// decoding
// ---------------------------------------------------------------------------
namespace
{
RlpResult<Endpoint> takeEndpoint(bcos::bytesRef& _view)
{
    RLP_TRY(auto items, takeListPayload(_view, "discv4: endpoint must be a list"));
    Endpoint endpoint;
    RLP_TRY(endpoint.ip, take<bcos::bytes>(items));
    RLP_TRY(auto udp, take<uint64_t>(items));
    RLP_TRY(auto tcp, take<uint64_t>(items));
    if (udp > 0xffff || tcp > 0xffff)
    {
        return std::unexpected(genericError("discv4: port out of range"));
    }
    endpoint.udp = static_cast<uint16_t>(udp);
    endpoint.tcp = static_cast<uint16_t>(tcp);
    return endpoint;
}

// The optional EIP-868 enr-seq: absent when the list ended.
RlpResult<uint64_t> takeOptionalSeq(bcos::bytesRef& _items)
{
    if (_items.empty())
    {
        return 0;
    }
    return take<uint64_t>(_items);
}

RlpResult<Packet> decodePacketData(PacketType _type, bcos::bytesRef _data)
{
    RLP_TRY(auto items, takeListPayload(_data, "discv4: packet-data must be a list"));
    // Anything after the list and any extra list elements are ignored (EIP-8).
    switch (_type)
    {
    case PacketType::Ping:
    {
        PingPacket packet;
        RLP_TRY(packet.version, take<uint64_t>(items));
        RLP_TRY(packet.from, takeEndpoint(items));
        RLP_TRY(packet.to, takeEndpoint(items));
        RLP_TRY(packet.expiration, take<uint64_t>(items));
        RLP_TRY(packet.enrSeq, takeOptionalSeq(items));
        return packet;
    }
    case PacketType::Pong:
    {
        PongPacket packet;
        RLP_TRY(packet.to, takeEndpoint(items));
        RLP_TRY(packet.pingHash, take<bcos::bytes>(items));
        RLP_TRY(packet.expiration, take<uint64_t>(items));
        RLP_TRY(packet.enrSeq, takeOptionalSeq(items));
        return packet;
    }
    case PacketType::FindNode:
    {
        FindNodePacket packet;
        RLP_TRY(packet.target, take<bcos::bytes>(items));
        if (packet.target.size() != 64)
        {
            return std::unexpected(genericError("discv4: findnode target must be 64 bytes"));
        }
        RLP_TRY(packet.expiration, take<uint64_t>(items));
        return packet;
    }
    case PacketType::Neighbors:
    {
        NeighborsPacket packet;
        RLP_TRY(auto nodes, takeListPayload(items, "discv4: neighbors nodes must be a list"));
        while (!nodes.empty())
        {
            RLP_TRY(auto fields, takeListPayload(nodes, "discv4: neighbor must be a list"));
            NeighborNode node;
            RLP_TRY(node.endpoint.ip, take<bcos::bytes>(fields));
            RLP_TRY(auto udp, take<uint64_t>(fields));
            RLP_TRY(auto tcp, take<uint64_t>(fields));
            if (udp > 0xffff || tcp > 0xffff)
            {
                return std::unexpected(genericError("discv4: port out of range"));
            }
            node.endpoint.udp = static_cast<uint16_t>(udp);
            node.endpoint.tcp = static_cast<uint16_t>(tcp);
            RLP_TRY(node.id, take<bcos::bytes>(fields));
            if (node.id.size() != 64)
            {
                return std::unexpected(genericError("discv4: neighbor id must be 64 bytes"));
            }
            packet.nodes.push_back(std::move(node));
        }
        RLP_TRY(packet.expiration, take<uint64_t>(items));
        return packet;
    }
    case PacketType::EnrRequest:
    {
        EnrRequestPacket packet;
        RLP_TRY(packet.expiration, take<uint64_t>(items));
        return packet;
    }
    case PacketType::EnrResponse:
    {
        EnrResponsePacket packet;
        RLP_TRY(packet.requestHash, take<bcos::bytes>(items));
        RLP_TRY(auto rawRecord, takeRawItem(items));
        RLP_TRY(packet.record, Enr::decode(bcos::ref(rawRecord)));
        return packet;
    }
    }
    return std::unexpected(genericError("discv4: unknown packet type"));
}
}  // namespace

RlpResult<DecodedPacket> decodePacket(bytesConstRef _datagram)
{
    if (_datagram.size() < c_packetHeaderSize + 1)
    {
        return std::unexpected(genericError("discv4: packet too small"));
    }
    if (_datagram.size() > c_maxPacketSize)
    {
        return std::unexpected(genericError("discv4: packet too large"));
    }
    bytesConstRef hash(_datagram.data(), c_packetHashSize);
    bytesConstRef signature(_datagram.data() + c_packetHashSize, c_packetSignatureSize);
    bytesConstRef signedPart(
        _datagram.data() + c_packetHeaderSize, _datagram.size() - c_packetHeaderSize);
    bytesConstRef hashedPart(
        _datagram.data() + c_packetHashSize, _datagram.size() - c_packetHashSize);
    auto expectedHash = rlpx::keccak256(hashedPart);
    if (!std::equal(expectedHash.begin(), expectedHash.end(), hash.begin(), hash.end()))
    {
        return std::unexpected(genericError("discv4: bad packet hash"));
    }
    DecodedPacket decoded;
    try
    {
        decoded.senderPublicKey =
            rlpx::recoverPublicKey(bcos::ref(rlpx::keccak256(signedPart)), signature);
    }
    catch (std::exception const& e)
    {
        return std::unexpected(genericError(std::string("discv4: bad signature: ") + e.what()));
    }
    auto const typeByte = _datagram[c_packetHeaderSize];
    if (typeByte < static_cast<uint8_t>(PacketType::Ping) ||
        typeByte > static_cast<uint8_t>(PacketType::EnrResponse))
    {
        return std::unexpected(
            genericError("discv4: unknown packet type " + std::to_string(typeByte)));
    }
    decoded.type = static_cast<PacketType>(typeByte);
    decoded.hash = hash.toBytes();
    bcos::bytesRef data(const_cast<bcos::byte*>(_datagram.data()) + c_packetHeaderSize + 1,
        _datagram.size() - c_packetHeaderSize - 1);
    RLP_TRY(decoded.packet, decodePacketData(decoded.type, data));
    return decoded;
}
}  // namespace bcos::devp2p::disc
