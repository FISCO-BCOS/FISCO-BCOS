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

#include "../eth/Protocol.h"
#include "Framing.h"
#include "Messages.h"
#include <bcos-codec/rlp/Result.h>
#include <bcos-utilities/LogStream.h>
#include <algorithm>
#include <cctype>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

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

// Unwraps an RlpResult at the handshake boundary (once per connection, so the
// exception style is kept here).
using bcos::codec::rlp::unwrapOrThrow;

// Formats the peer's Disconnect reason for a log/exception message.
std::string disconnectReason(bytesConstRef _data)
{
    auto disc = decodeDisconnect(_data);
    if (disc)
    {
        return "reason=" + std::to_string(static_cast<int>(disc->reason));
    }
    return "reason undecodable: " + disc.error().message;
}

// Send Disconnect(reason) best-effort (the peer may already be gone) and throw.
constexpr size_t c_maxHandshakeFrameSize = 64 * 1024;

[[noreturn]] void rejectHandshake(
    Session& _session, DisconnectReason _reason, std::string const& _what)
{
    sendDisconnect(_session, _reason);
    throw HandshakeRejected(_reason, _what);
}
}  // namespace

EstablishedSession performHandshake(Session&& _session, AuthKeys const& _authKeys,
    EccKeyPair const& _keyPair, PeerConfig const& _config, HandshakeOptions const& _options)
{
    auto session = std::move(_session);

    // --- Hello exchange ---
    HelloMessage hello;
    hello.version = 5;
    hello.clientId = _config.clientId;
    for (auto const& cap : eth::ethCapabilities(_options.minEthVersion, _options.maxEthVersion))
    {
        hello.capabilities.push_back(cap);
    }
    hello.listenPort = _config.listenPort;
    hello.id = _keyPair.publicKey();

    session.sendMessage(Message{baseMsg::Hello, encodeHello(hello)});
    auto helloMsg = unwrapOrThrow(
        session.recvMessage(), "exchangeHandshake: failed to decode the Hello frame: ");
    if (helloMsg.id != baseMsg::Hello)
    {
        if (helloMsg.id == baseMsg::Disconnect)
        {
            throw std::runtime_error(
                "exchangeHandshake: peer disconnected during Hello: " +
                disconnectReason(bytesConstRef(helloMsg.data.data(), helloMsg.data.size())));
        }
        throw std::runtime_error(
            "exchangeHandshake: expected Hello, got message id=" + std::to_string(helloMsg.id));
    }
    auto peerHello =
        unwrapOrThrow(decodeHello(bytesConstRef(helloMsg.data.data(), helloMsg.data.size())),
            "exchangeHandshake: failed to decode the Hello message: ");
    // Both peers send Hello with version >= 5 → enable snappy on the session now: every
    // frame after Hello, a Disconnect we send on rejection included, is compressed.
    // Raw snappy is varint-prefixed + block, wire-identical across geth/erigon/
    // reth/ethrex (Go snappy.Decode and Rust snap::raw are the same format).
    session.enableCompression();
    // The Hello `id` must repeat the identity the ECIES handshake authenticated (the
    // recipient learned it from the auth message, the initiator dialled it).
    if (!_authKeys.peerPublicKey.empty() && peerHello.id != _authKeys.peerPublicKey)
    {
        rejectHandshake(session, DisconnectReason::UnexpectedIdentity,
            "exchangeHandshake: Hello id does not match the handshake public key");
    }
    // Negotiate the highest eth version both sides advertise (68..70 outbound, 69..70
    // on the inbound listener — see eth::ethCapabilities).
    uint8_t negotiatedEth = 0;
    for (auto const& cap : peerHello.capabilities)
    {
        if (cap.name == "eth" && cap.version >= _options.minEthVersion &&
            cap.version <= _options.maxEthVersion && cap.version <= eth::kProtocolVersion)
        {
            negotiatedEth = std::max(negotiatedEth, cap.version);
        }
    }
    // Diagnostics: the negotiated capability layout determines every eth frame id.
    {
        std::string peerCaps;
        for (auto const& cap : peerHello.capabilities)
        {
            peerCaps += " " + cap.name + "/" + std::to_string(cap.version);
        }
        std::string ourCaps;
        for (auto const& cap : hello.capabilities)
        {
            ourCaps += " " + cap.name + "/" + std::to_string(cap.version);
        }
        BCOS_LOG(INFO) << LOG_BADGE("handshake") << LOG_KV("peerClient", peerHello.clientId)
                       << LOG_KV("peerCaps", peerCaps) << LOG_KV("ourCaps", ourCaps)
                       << LOG_KV("eth", static_cast<int>(negotiatedEth));
    }
    if (negotiatedEth == 0)
    {
        rejectHandshake(session, DisconnectReason::UselessPeer,
            "exchangeHandshake: no common eth capability with peer");
    }
    // --- eth Status exchange ---
    eth::StatusMessage status;
    status.protocolVersion = negotiatedEth;
    status.networkId = _config.networkId;
    status.genesisHash = _config.genesisHash;
    status.forkId = _config.forkId;
    if (negotiatedEth >= 69)
    {
        // EIP-7642: advertise the served block range; an unset range (zero latest hash)
        // falls back to genesis-only.
        status.eip7642 = true;
        status.earliestBlock = _config.earliestBlock;
        status.latestBlock = _config.latestBlock;
        status.latestBlockHash =
            _config.latestBlockHash == bcos::h256{} ? _config.genesisHash : _config.latestBlockHash;
    }
    else
    {
        status.headHash = _config.headHash;
        status.totalDifficulty = _config.totalDifficulty;
    }

    session.sendMessage(
        Message{static_cast<uint8_t>(eth::frameId(eth::msg::Status)), encodeStatus(status)});

    auto statusMsg = unwrapOrThrow(
        session.recvMessage(), "exchangeHandshake: failed to decode the eth Status frame: ");
    if (statusMsg.id != eth::frameId(eth::msg::Status))
    {
        if (statusMsg.id == baseMsg::Disconnect)
        {
            auto const reason =
                disconnectReason(bytesConstRef(statusMsg.data.data(), statusMsg.data.size()));
            BCOS_LOG(INFO) << LOG_BADGE("handshake") << "peer disconnected during Status"
                           << LOG_KV("peerClient", peerHello.clientId) << reason;
            throw std::runtime_error(
                "exchangeHandshake: peer disconnected during Status: " + reason);
        }
        throw std::runtime_error("exchangeHandshake: expected eth Status, got message id=" +
                                 std::to_string(statusMsg.id));
    }
    // The Status layout is selected from the version negotiated in Hello (never
    // from the peer-supplied field), and the embedded version must match it.
    auto peerStatusResult = eth::decodeStatus(
        bytesConstRef(statusMsg.data.data(), statusMsg.data.size()), negotiatedEth);
    if (!peerStatusResult)
    {
        rejectHandshake(session, DisconnectReason::ProtocolBreach,
            "exchangeHandshake: failed to decode the eth Status message: " +
                peerStatusResult.error().message);
    }
    auto peerStatus = std::move(*peerStatusResult);

    // The peer must be on our network (spec: disconnect on network-ID mismatch).
    if (peerStatus.networkId != _config.networkId)
    {
        rejectHandshake(session, DisconnectReason::UselessPeer,
            "exchangeHandshake: peer network id mismatch: " + std::to_string(peerStatus.networkId) +
                " != " + std::to_string(_config.networkId));
    }

    // Verify the peer is on our chain — only when the local config actually
    // pins a genesis hash (the test server accepts whatever the client sends).
    if (_config.genesisHash != bcos::h256{} && peerStatus.genesisHash != _config.genesisHash)
    {
        rejectHandshake(session, DisconnectReason::UselessPeer,
            "exchangeHandshake: peer genesis hash mismatch (different chain)");
    }
    if (_options.forkIdReject)
    {
        if (auto why = _options.forkIdReject(peerStatus.forkId))
        {
            rejectHandshake(session, DisconnectReason::UselessPeer,
                "exchangeHandshake: peer fork id rejected: " + *why);
        }
    }
    // EIP-7642 block-range sanity (spec: a peer announcing earliest > latest is
    // disconnected).
    if (peerStatus.eip7642 && peerStatus.earliestBlock > peerStatus.latestBlock)
    {
        rejectHandshake(session, DisconnectReason::ProtocolBreach,
            "exchangeHandshake: invalid block range in peer Status");
    }
    return EstablishedSession(std::move(session), std::move(peerHello), std::move(peerStatus));
}

void sendDisconnect(Session& _session, DisconnectReason _reason) noexcept
{
    try
    {
        _session.sendMessage(Message{baseMsg::Disconnect, encodeDisconnect({_reason})});
    }
    catch (...)
    {
        // best effort: the caller already decided to drop this peer
    }
}

EstablishedSession acceptHandshake(Socket&& _socket, EccKeyPair const& _keyPair,
    PeerConfig const& _config, HandshakeOptions const& _options)
{
    auto socket = std::move(_socket);
    Handshake handshake(_keyPair, /* isInitiator = */ false);
    auto authKeys = handshake.execute(socket);
    auto session = makeSession(std::move(socket), authKeys, /* isInitiator = */ false);
    // Whoever connects can claim a frame size, and the frame buffer is allocated before it
    // arrives. Hello and Status are small, so a stranger gets no 16 MiB buffer.
    session.setMaxFrameSize(c_maxHandshakeFrameSize);
    auto established = performHandshake(std::move(session), authKeys, _keyPair, _config, _options);
    established.session.setMaxFrameSize(MessageCodec::kMaxFrameSize);
    return established;
}

RlpxClient::RlpxClient(EccKeyPair _keyPair, PeerConfig _config, HandshakeOptions _options)
  : m_keyPair(std::move(_keyPair)), m_config(std::move(_config)), m_options(std::move(_options))
{}

EstablishedSession RlpxClient::connect()
{
    Socket socket;
    socket.connect(m_config.host, m_config.port);

    Handshake handshake(m_keyPair, /* isInitiator = */ true,
        bytesConstRef(m_config.peerPublicKey.data(), m_config.peerPublicKey.size()));
    auto authKeys = handshake.execute(socket);

    auto session = makeSession(std::move(socket), authKeys, /* isInitiator = */ true);
    return performHandshake(std::move(session), authKeys, m_keyPair, m_config, m_options);
}

RlpxServer::RlpxServer(EccKeyPair _keyPair, uint16_t _port, PeerConfig _config)
  : m_keyPair(std::move(_keyPair)), m_config(std::move(_config)), m_listener(_port)
{}

EstablishedSession RlpxServer::accept()
{
    auto socket = m_listener.accept();
    if (m_config.clientId.empty())
    {
        m_config.clientId = "FISCO-BCOS-devp2p-server/v0.1.0";
    }
    return acceptHandshake(std::move(socket), m_keyPair, m_config);
}

}  // namespace bcos::devp2p::rlpx
