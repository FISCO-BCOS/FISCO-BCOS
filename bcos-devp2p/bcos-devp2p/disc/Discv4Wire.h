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
 * @file Discv4Wire.h
 * @brief Node Discovery v4 wire format (devp2p discv4.md, EIP-8, EIP-868):
 *        packet = hash(32) || signature(65) || type(1) || rlp(packet-data).
 *        Wire-compatible with geth p2p/discover/v4wire.
 * @date 2026/11/3
 */
#pragma once

#include "Enr.h"
#include <bcos-codec/rlp/Result.h>
#include <bcos-utilities/Common.h>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace bcos::devp2p::disc
{
// discv4.md: the maximum size of any packet is 1280 bytes.
constexpr size_t c_maxPacketSize = 1280;
// geth v4wire.MaxNeighbors: the most nodes that fit one Neighbors packet.
constexpr size_t c_maxNeighbors = 12;
constexpr size_t c_packetHashSize = 32;
constexpr size_t c_packetSignatureSize = 65;
constexpr size_t c_packetHeaderSize = c_packetHashSize + c_packetSignatureSize;

enum class PacketType : uint8_t
{
    Ping = 0x01,
    Pong = 0x02,
    FindNode = 0x03,
    Neighbors = 0x04,
    EnrRequest = 0x05,
    EnrResponse = 0x06,
};
char const* packetName(PacketType _type);

// [ip, udp-port, tcp-port]; ip is 4 (IPv4) or 16 (IPv6) bytes as received.
struct Endpoint
{
    bcos::bytes ip;
    uint16_t udp{0};
    uint16_t tcp{0};

    bool isV4() const { return ip.size() == 4; }
    bool isV6() const { return ip.size() == 16; }
    std::string ipString() const;
    bool operator==(Endpoint const&) const = default;
};

// packet-data = [version, from, to, expiration, enr-seq ...]
struct PingPacket
{
    uint64_t version{4};
    Endpoint from;
    Endpoint to;
    uint64_t expiration{0};
    uint64_t enrSeq{0};  // EIP-868; 0 = absent on the wire
};

// packet-data = [to, ping-hash, expiration, enr-seq, ...]
struct PongPacket
{
    Endpoint to;
    bcos::bytes pingHash;
    uint64_t expiration{0};
    uint64_t enrSeq{0};  // EIP-868; 0 = absent on the wire
};

// packet-data = [target, expiration, ...]; target is a 64-byte public key.
struct FindNodePacket
{
    bcos::bytes target;
    uint64_t expiration{0};
};

// nodes = [[ip, udp-port, tcp-port, node-id], ...]
struct NeighborNode
{
    Endpoint endpoint;
    bcos::bytes id;  // 64-byte public key
    bool operator==(NeighborNode const&) const = default;
};

// packet-data = [nodes, expiration, ...]
struct NeighborsPacket
{
    std::vector<NeighborNode> nodes;
    uint64_t expiration{0};
};

// packet-data = [expiration]
struct EnrRequestPacket
{
    uint64_t expiration{0};
};

// packet-data = [request-hash, ENR]
struct EnrResponsePacket
{
    bcos::bytes requestHash;
    Enr record;  // structurally decoded; the receiver verifies the signature
};

using Packet = std::variant<PingPacket, PongPacket, FindNodePacket, NeighborsPacket,
    EnrRequestPacket, EnrResponsePacket>;
PacketType packetType(Packet const& _packet);

struct DecodedPacket
{
    Packet packet;
    PacketType type{};
    bcos::bytes senderPublicKey;  // 64 bytes, recovered from the signature
    bcos::bytes hash;  // the 32-byte packet hash (Pong.pingHash / ENRResponse.requestHash)
};

struct EncodedPacket
{
    bcos::bytes datagram;
    bcos::bytes hash;
};

// Verifies the hash, recovers the sender and decodes packet-data. Extra list elements
// and trailing bytes after the list are ignored (EIP-8); the Ping version is not checked.
bcos::codec::rlp::RlpResult<DecodedPacket> decodePacket(bytesConstRef _datagram);

// Signs with the 32-byte node private key and frames the packet.
EncodedPacket encodePacket(Packet const& _packet, bytesConstRef _privateKey);

// discv4.md: packets whose expiration lies in the past must not be processed.
inline bool isExpired(uint64_t _expiration, uint64_t _nowUnixSeconds)
{
    return _expiration < _nowUnixSeconds;
}
}  // namespace bcos::devp2p::disc
