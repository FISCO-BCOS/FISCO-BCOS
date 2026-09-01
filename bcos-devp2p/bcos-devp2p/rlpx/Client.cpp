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
 * @file Client.cpp
 * @brief RLPx client/server implementation: handshake + Hello + eth Status.
 * @date 2026/8/18
 */
#include "Client.h"

#include "Framing.h"
#include "Messages.h"
#include "../eth/Protocol.h"
#include <cctype>
#include <iostream>
#include <stdexcept>
#include <string>

namespace bcos::devp2p::rlpx
{
namespace
{
// Build the framing cipher from the handshake keys and wrap a session.
Session makeSession(Socket&& _socket, AuthKeys const& _keys, bool _isInitiator)
{
    FramingCipher::KeyMaterial keyMaterial;
    keyMaterial.ephemeralSharedSecret = EciesCipher::computeSharedSecret(
        bytesConstRef(_keys.peerEphemeralPublicKey.data(), _keys.peerEphemeralPublicKey.size()),
        bytesConstRef(_keys.ephemeralPrivateKey.data(), _keys.ephemeralPrivateKey.size()));
    keyMaterial.isInitiator = _isInitiator;
    keyMaterial.initiatorNonce = _keys.initiatorNonce;
    keyMaterial.recipientNonce = _keys.recipientNonce;
    keyMaterial.initiatorFirstMessageData = _keys.initiatorFirstMessageData;
    keyMaterial.recipientFirstMessageData = _keys.recipientFirstMessageData;
    return Session(std::move(_socket), FramingCipher(keyMaterial));
}

// Shared Hello/Status exchange once the encrypted session exists.
EstablishedSession exchangeHandshake(
    Session&& _session, EccKeyPair const& _keyPair, PeerConfig const& _config)
{
    auto session = std::move(_session);

    // --- Hello exchange ---
    HelloMessage hello;
    hello.version = 5;
    hello.clientId = _config.clientId;
    for (auto const& cap : eth::ethCapabilities())
    {
        hello.capabilities.push_back(cap);
    }
    hello.listenPort = _config.listenPort;
    hello.id = _keyPair.publicKey();

    session.sendMessage(Message{baseMsg::Hello, encodeHello(hello)});
    auto helloMsg = session.recvMessage();
    if (helloMsg.id != baseMsg::Hello)
    {
        if (helloMsg.id == baseMsg::Disconnect)
        {
            auto disc = decodeDisconnect(bytesConstRef(helloMsg.data.data(), helloMsg.data.size()));
            throw std::runtime_error(
                "exchangeHandshake: peer disconnected during Hello: reason=" +
                std::to_string(static_cast<int>(disc.reason)));
        }
        throw std::runtime_error("exchangeHandshake: expected Hello, got message id=" +
                                 std::to_string(helloMsg.id));
    }
    auto peerHello = decodeHello(bytesConstRef(helloMsg.data.data(), helloMsg.data.size()));
    // Negotiate the highest eth version the peer supports from {68, 69}.
    uint8_t negotiatedEth = 0;
    for (auto const& cap : peerHello.capabilities)
    {
        if (cap.name == "eth" && (cap.version == 68 || cap.version == 69))
        {
            negotiatedEth = std::max(negotiatedEth, cap.version);
        }
    }
    if (negotiatedEth == 0)
    {
        throw std::runtime_error("exchangeHandshake: no common eth capability with peer");
    }
    // Diagnostics: the negotiated capability layout determines every eth frame id.
    std::cerr << "[handshake] peer client=\"" << peerHello.clientId << "\" caps:";
    for (auto const& cap : peerHello.capabilities)
    {
        std::cerr << " " << cap.name << "/" << static_cast<int>(cap.version);
    }
    std::cerr << " (our caps:";
    for (auto const& cap : hello.capabilities)
    {
        std::cerr << " " << cap.name << "/" << static_cast<int>(cap.version);
    }
    std::cerr << ") negotiated eth/" << static_cast<int>(negotiatedEth) << std::endl;

    // Both peers send Hello with version >= 5 → enable snappy on the session.
    // Raw snappy is varint-prefixed + block, wire-identical across geth/erigon/
    // reth/ethrex (Go snappy.Decode and Rust snap::raw are the same format).
    session.enableCompression();

    // --- eth Status exchange ---
    eth::StatusMessage status;
    status.protocolVersion = negotiatedEth;
    status.networkId = _config.networkId;
    status.genesisHash = _config.genesisHash;
    status.forkId = _config.forkId;
    if (negotiatedEth >= 69)
    {
        // EIP-8085: advertise our (genesis-only) block range.
        status.eip8085 = true;
        status.earliestBlock = 0;
        status.latestBlock = 0;
        status.latestBlockHash = _config.genesisHash;
    }
    else
    {
        status.headHash = _config.headHash;
        status.totalDifficulty = _config.totalDifficulty;
    }

    session.sendMessage(
        Message{static_cast<uint8_t>(eth::frameId(eth::msg::Status)), encodeStatus(status)});

    auto statusMsg = session.recvMessage();
    if (statusMsg.id != eth::frameId(eth::msg::Status))
    {
        if (statusMsg.id == baseMsg::Disconnect)
        {
            try
            {
                auto disc = decodeDisconnect(
                    bytesConstRef(statusMsg.data.data(), statusMsg.data.size()));
                std::cerr << "[handshake] peer (\"" << peerHello.clientId
                          << "\") disconnected during Status: reason="
                          << static_cast<int>(disc.reason) << std::endl;
            }
            catch (std::exception const& e)
            {
                std::cerr << "[handshake] peer (\"" << peerHello.clientId
                          << "\") disconnected during Status (reason undecodable): "
                          << e.what() << std::endl;
            }
        }
        throw std::runtime_error("exchangeHandshake: expected eth Status, got message id=" +
                                 std::to_string(statusMsg.id));
    }
    auto peerStatus =
        eth::decodeStatus(bytesConstRef(statusMsg.data.data(), statusMsg.data.size()));

    // Verify the peer is on our chain — only when the local config actually
    // pins a genesis hash (the server side accepts whatever the client sends).
    if (_config.genesisHash != bcos::h256{} &&
        peerStatus.genesisHash != _config.genesisHash)
    {
        throw std::runtime_error(
            "exchangeHandshake: peer genesis hash mismatch (different chain)");
    }
    return EstablishedSession(std::move(session), std::move(peerHello), std::move(peerStatus));
}
}  // namespace

RlpxClient::RlpxClient(EccKeyPair _keyPair, PeerConfig _config)
  : m_keyPair(std::move(_keyPair)), m_config(std::move(_config))
{}

EstablishedSession RlpxClient::connect()
{
    Socket socket;
    socket.connect(m_config.host, m_config.port);

    Handshake handshake(m_keyPair, /* isInitiator = */ true,
        bytesConstRef(m_config.peerPublicKey.data(), m_config.peerPublicKey.size()));
    auto authKeys = handshake.execute(socket);

    auto session = makeSession(std::move(socket), authKeys, /* isInitiator = */ true);
    return exchangeHandshake(std::move(session), m_keyPair, m_config);
}

RlpxServer::RlpxServer(EccKeyPair _keyPair, uint16_t _port, PeerConfig _config)
  : m_keyPair(std::move(_keyPair)), m_config(std::move(_config)), m_listener(_port)
{}

EstablishedSession RlpxServer::accept()
{
    auto socket = m_listener.accept();

    Handshake handshake(m_keyPair, /* isInitiator = */ false);
    auto authKeys = handshake.execute(socket);

    auto session = makeSession(std::move(socket), authKeys, /* isInitiator = */ false);

    if (m_config.clientId.empty())
    {
        m_config.clientId = "FISCO-BCOS-devp2p-server/v0.1.0";
    }
    return exchangeHandshake(std::move(session), m_keyPair, m_config);
}

}  // namespace bcos::devp2p::rlpx
