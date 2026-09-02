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
 * @file Messages.h
 * @brief devp2p base-protocol messages: Hello, Disconnect, Ping, Pong.
 * @date 2026/8/18
 */
#pragma once

#include <bcos-utilities/Common.h>
#include <string>
#include <vector>

namespace bcos::devp2p::rlpx
{
// devp2p base-protocol message codes (shared by every capability).
namespace baseMsg
{
constexpr uint8_t Hello = 0x00;
constexpr uint8_t Disconnect = 0x01;
constexpr uint8_t Ping = 0x02;
constexpr uint8_t Pong = 0x03;
}  // namespace baseMsg

// Reason codes of the Disconnect message (EIP-8 / devp2p).
enum class DisconnectReason : uint8_t
{
    DisconnectRequested = 0x00,
    TcpSubsystemError = 0x01,
    ProtocolBreach = 0x02,
    UselessPeer = 0x03,
    TooManyPeers = 0x04,
    AlreadyConnected = 0x05,
    IncompatibleP2pProtocolVersion = 0x06,
    NullNodeIdentity = 0x07,
    ClientQuitting = 0x08,
    UnexpectedIdentity = 0x09,
    LocalIdentity = 0x0a,
    PingTimeout = 0x0b,
    // 0x10 is the spec's "subprotocol-specific reason" sentinel.
    SubprotocolReason = 0x10,
};

struct Capability
{
    std::string name;
    uint8_t version{0};
};

// RLP: [version, name, [[name, version], ...], listenPort, id]
struct HelloMessage
{
    uint64_t version{5};
    std::string clientId;
    std::vector<Capability> capabilities;
    uint64_t listenPort{0};
    bcos::bytes id;  // 64-byte secp256k1 public key
};

bcos::bytes encodeHello(HelloMessage const& _msg);
HelloMessage decodeHello(bytesConstRef _data);

// RLP: [reason]
struct DisconnectMessage
{
    DisconnectReason reason{DisconnectReason::DisconnectRequested};
};

bcos::bytes encodeDisconnect(DisconnectMessage const& _msg);
DisconnectMessage decodeDisconnect(bytesConstRef _data);

// Ping/Pong are the empty list RLP: 0xc0.
bcos::bytes encodePing();
bcos::bytes encodePong();
}  // namespace bcos::devp2p::rlpx
