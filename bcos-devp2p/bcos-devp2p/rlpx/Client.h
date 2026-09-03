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
 * @file Client.h
 * @brief RLPx client: connect, encrypted handshake, Hello/Status exchange with
 *        a peer, then hand over an established Session to the sync layer.
 * @date 2026/8/18
 */
#pragma once

#include "Crypto.h"
#include "Handshake.h"
#include "Messages.h"
#include "Session.h"
#include "Socket.h"
#include "../eth/ForkId.h"
#include "../eth/Protocol.h"
#include <bcos-utilities/FixedBytes.h>
#include <string>

namespace bcos::devp2p::rlpx
{
// Connection parameters for a sync peer.
struct PeerConfig
{
    std::string host;
    uint16_t port{30303};
    // 64-byte uncompressed public key of the peer (used for the ECIES handshake).
    bcos::bytes peerPublicKey;
    std::string clientId{"FISCO-BCOS-devp2p/v0.1.0"};
    uint64_t listenPort{0};
    uint64_t networkId{1};
    bcos::h256 genesisHash;
    bcos::h256 headHash;
    bcos::bytes totalDifficulty;  // minimal big-endian u256
    bcos::devp2p::eth::ForkId forkId;
};

// The result of a successful connection: the encrypted session plus the
// peer's advertised identity and status.
struct EstablishedSession
{
    Session session;
    HelloMessage peerHello;
    bcos::devp2p::eth::StatusMessage peerStatus;

    EstablishedSession(Session&& _session, HelloMessage _hello,
        bcos::devp2p::eth::StatusMessage _status)
      : session(std::move(_session)), peerHello(std::move(_hello)), peerStatus(std::move(_status))
    {}
};

class RlpxClient
{
public:
    // _keyPair is this node's identity key (used for the ECIES handshake).
    RlpxClient(EccKeyPair _keyPair, PeerConfig _config);

    // Connect, perform the encrypted handshake, exchange Hello + eth Status,
    // and return an established session (snappy enabled after Hello).
    EstablishedSession connect();

private:
    EccKeyPair m_keyPair;
    PeerConfig m_config;
};

// Minimal server side used by tests: accept + handshake + Hello/Status exchange.
class RlpxServer
{
public:
    RlpxServer(EccKeyPair _keyPair, uint16_t _port, PeerConfig _config = {});

    // Block until a client completes the full handshake/hello/status exchange.
    EstablishedSession accept();

    // The port this server listens on (valid once constructed).
    uint16_t port() const { return m_listener.port(); }

private:
    EccKeyPair m_keyPair;
    PeerConfig m_config;
    TcpListener m_listener;
};
}  // namespace bcos::devp2p::rlpx
