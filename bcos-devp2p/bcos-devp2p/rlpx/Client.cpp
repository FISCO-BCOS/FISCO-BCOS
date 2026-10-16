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
 * @brief RLPx client/server implementation over the bcos-network async
 *        transport: handshake + Hello + eth Status.
 * @date 2026/8/18
 */
#include "Client.h"

#include "../eth/Protocol.h"
#include "Framing.h"
#include "Messages.h"
#include <bcos-codec/rlp/Result.h>
#include <bcos-network/Common.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/IOServicePool.h>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <atomic>
#include <chrono>
#include <cctype>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace bcos::devp2p::rlpx
{
namespace
{
// Behaviour-compatible timeouts of the old blocking implementation.
constexpr uint32_t kConnectTimeoutMs = 5000;
constexpr uint32_t kIoTimeoutMs = 15000;

// Process-wide IO pool shared by every devp2p client/server (the old
// implementation spent one std::thread per connection on a blocking loop).
// Leaky singleton: never destroyed, avoiding static-destruction-order issues
// at process exit. The pool's worker threads start in its constructor.
bcos::IOServicePool::Ptr ioPool()
{
    static auto* pool = new bcos::IOServicePool(2, "devp2p");
    // Non-owning shared_ptr: satisfies ASIOInterface's Ptr parameter without
    // ever running the destructor.
    return bcos::IOServicePool::Ptr(pool, [](bcos::IOServicePool*) {});
}

// One Host per client connection; the server keeps one Host for its lifetime.
// The recv buffer size is the grow CEILING (initial allocation stays small) and
// must cover the largest frame — bcos-network's own convention is
// 2 * allowMaxMsgSize. allowMaxMsgSize leaves headroom over the 16MiB codec
// limit for the 32B header + 16B padding + 16B MAC wire overhead.
NetHostPtr makeHost(std::shared_ptr<bcos::network::ASIOInterface> _asio)
{
    constexpr uint32_t kAllowMaxMsgSize = MessageCodec::kMaxFrameSize + 64 * 1024;
    auto factory =
        std::make_shared<bcos::network::BasicSessionFactory<RlpxFrameDecoder,
            bcos::network::PlainSocket>>(
            2 * kAllowMaxMsgSize, kAllowMaxMsgSize, 64 * 1024, 1024 * 1024);
    return std::make_shared<NetHost>(std::move(_asio), std::move(factory));
}

// Total-deadline guard for one handshake-phase I/O operation: a timer armed on
// the socket's own io_context whose expiry flags the operation and cancels the
// pending async read/write (mirrors the read loop's setReadTimeoutMs pattern,
// which cannot be used before session start). Flags are atomic: the timer
// handler runs on the socket's io thread while disarm() runs on the thread
// that blocked in syncWait.
struct Deadline
{
    std::shared_ptr<std::atomic_bool> armed = std::make_shared<std::atomic_bool>(true);
    std::shared_ptr<std::atomic_bool> expired = std::make_shared<std::atomic_bool>(false);
    std::shared_ptr<boost::asio::steady_timer> timer;

    Deadline(std::shared_ptr<bcos::network::PlainSocket> const& _socket, uint32_t _timeoutMs)
      : timer(std::make_shared<boost::asio::steady_timer>(
            _socket->ioService(), std::chrono::milliseconds(_timeoutMs)))
    {
        timer->async_wait([armed = armed, expired = expired, socket = _socket](
                              const boost::system::error_code& _ec) {
            if (!_ec && armed->load())
            {
                expired->store(true);
                boost::system::error_code cancelEc;
                // NOLINTNEXTLINE(bugprone-unused-return-value) delivered via the ec out-param
                socket->ref().cancel(cancelEc);
            }
        });
    }

    // Disarm BEFORE cancel: an expiry already queued behind the I/O completion
    // observes armed == false and no-ops, so it can never cancel the NEXT
    // operation. The cancel itself is posted to the socket's io thread — asio
    // objects are not cross-thread safe and the caller runs off that context.
    void disarm(std::shared_ptr<bcos::network::PlainSocket> const& _socket)
    {
        armed->store(false);
        boost::asio::post(_socket->ioService(), [timer = timer]() {
            try
            {
                timer->cancel();
            }
            catch (...)
            {}
        });
    }
};

// Handshake-phase read-full with a total deadline (the session read loop is
// not running yet, so setReadTimeoutMs does not apply).
bcos::bytes readExactWithTimeout(bcos::network::ASIOInterface& _asio,
    std::shared_ptr<bcos::network::PlainSocket> const& _socket, size_t _size, uint32_t _timeoutMs)
{
    Deadline deadline(_socket, _timeoutMs);
    bcos::bytes buffer(_size, 0);
    boost::system::error_code ec;
    try
    {
        auto result = task::syncWait(
            _asio.awaitableRead(_socket, boost::asio::buffer(buffer.data(), buffer.size())));
        ec = std::get<0>(result);
    }
    catch (...)
    {
        deadline.disarm(_socket);
        throw;
    }
    deadline.disarm(_socket);
    if (deadline.expired->load() && ec == boost::asio::error::operation_aborted)
    {
        throw std::runtime_error("rlpx handshake: read timed out");
    }
    if (ec)
    {
        throw std::runtime_error("rlpx handshake: read failed: " + ec.message());
    }
    return buffer;
}

// Handshake-phase write-full with a total deadline.
void sendAllWithTimeout(bcos::network::ASIOInterface& _asio,
    std::shared_ptr<bcos::network::PlainSocket> const& _socket, bytesConstRef _data,
    uint32_t _timeoutMs)
{
    Deadline deadline(_socket, _timeoutMs);
    boost::system::error_code ec;
    try
    {
        auto result = task::syncWait(
            _asio.awaitableWrite(_socket, boost::asio::buffer(_data.data(), _data.size())));
        ec = std::get<0>(result);
    }
    catch (...)
    {
        deadline.disarm(_socket);
        throw;
    }
    deadline.disarm(_socket);
    if (deadline.expired->load() && ec == boost::asio::error::operation_aborted)
    {
        throw std::runtime_error("rlpx handshake: write timed out");
    }
    if (ec)
    {
        throw std::runtime_error("rlpx handshake: write failed: " + ec.message());
    }
}

// Derive the framing key material from the completed handshake (the old
// makeSession logic).
FramingCipher::KeyMaterial makeKeyMaterial(AuthKeys const& _keys, bool _isInitiator)
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
    return keyMaterial;
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
        // EIP-7642: advertise our (genesis-only) block range.
        status.eip7642 = true;
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

    auto statusMsg = unwrapOrThrow(
        session.recvMessage(), "exchangeHandshake: failed to decode the eth Status frame: ");
    if (statusMsg.id != eth::frameId(eth::msg::Status))
    {
        if (statusMsg.id == baseMsg::Disconnect)
        {
            BCOS_LOG(INFO) << LOG_BADGE("handshake") << "peer disconnected during Status"
                           << LOG_KV("peerClient", peerHello.clientId)
                           << disconnectReason(
                                  bytesConstRef(statusMsg.data.data(), statusMsg.data.size()));
        }
        throw std::runtime_error("exchangeHandshake: expected eth Status, got message id=" +
                                 std::to_string(statusMsg.id));
    }
    // The Status layout is selected from the version negotiated in Hello (never
    // from the peer-supplied field), and the embedded version must match it.
    auto peerStatus =
        unwrapOrThrow(eth::decodeStatus(bytesConstRef(statusMsg.data.data(), statusMsg.data.size()),
                          negotiatedEth),
            "exchangeHandshake: failed to decode the eth Status message: ");

    // The peer must be on our network (spec: disconnect on network-ID mismatch).
    if (peerStatus.networkId != _config.networkId)
    {
        throw std::runtime_error(
            "exchangeHandshake: peer network id mismatch: " + std::to_string(peerStatus.networkId) +
            " != " + std::to_string(_config.networkId));
    }

    // Verify the peer is on our chain — only when the local config actually
    // pins a genesis hash (the server side accepts whatever the client sends).
    if (_config.genesisHash != bcos::h256{} && peerStatus.genesisHash != _config.genesisHash)
    {
        throw std::runtime_error("exchangeHandshake: peer genesis hash mismatch (different chain)");
    }
    return EstablishedSession(std::move(session), std::move(peerHello), std::move(peerStatus));
}

// Run the RLPx encrypted handshake over a not-yet-started network session,
// then arm it for data traffic and wrap it in the synchronous facade.
Session establishSession(NetHostPtr const& _host, NetSessionPtr const& _netSession,
    std::shared_ptr<bcos::network::ASIOInterface> const& _asio, EccKeyPair const& _keyPair,
    bool _isInitiator, bytesConstRef _recipientPublicKey)
{
    auto socket = _netSession->socket();
    IoChannel io;
    io.sendAll = [_asio, socket](bytesConstRef _data) {
        sendAllWithTimeout(*_asio, socket, _data, kIoTimeoutMs);
    };
    io.recvFixed = [_asio, socket](size_t _size) {
        return readExactWithTimeout(*_asio, socket, _size, kIoTimeoutMs);
    };

    Handshake handshake(_keyPair, _isInitiator, _recipientPublicKey);
    auto authKeys = handshake.execute(io);
    auto keyMaterial = makeKeyMaterial(authKeys, _isInitiator);

    // Inject the ingress cipher into the session decoder BEFORE start(): the
    // read loop only begins consuming bytes there.
    _netSession->m_decoder.initCipher(keyMaterial);
    _netSession->setReadTimeoutMs(kIoTimeoutMs);  // 15s read timeout, session stays alive
    _netSession->setIdleTimeInterval(0);          // no idle-drop (matches the old behaviour)
    _netSession->start();
    return Session(_host, _netSession, FramingCipher(keyMaterial));
}
}  // namespace

RlpxClient::RlpxClient(EccKeyPair _keyPair, PeerConfig _config)
  : m_keyPair(std::move(_keyPair)), m_config(std::move(_config))
{}

EstablishedSession RlpxClient::connect()
{
    // Port 0: pure client — the acceptor binds an ephemeral port and the
    // accept loop idles harmlessly.
    auto asio = std::make_shared<bcos::network::ASIOInterface>(ioPool(), "0.0.0.0", 0);
    auto host = makeHost(asio);
    host->setConnectTimeout(kConnectTimeoutMs);
    host->start();

    auto [error, token, netSession] = task::syncWait(
        host->connect(bcos::network::NodeIPEndpoint(m_config.host, m_config.port)));
    if (!netSession)
    {
        host->stop();
        throw std::runtime_error("RlpxClient: failed to connect to " + m_config.host + ":" +
                                 std::to_string(m_config.port) + ": " + error.what());
    }

    try
    {
        auto session = establishSession(host, netSession, asio, m_keyPair, /* isInitiator = */ true,
            bytesConstRef(m_config.peerPublicKey.data(), m_config.peerPublicKey.size()));
        return exchangeHandshake(std::move(session), m_keyPair, m_config);
    }
    catch (...)
    {
        netSession->drop(bcos::network::DisconnectReason::DisconnectRequested);
        host->stop();
        throw;
    }
}

RlpxServer::RlpxServer(EccKeyPair _keyPair, uint16_t _port, PeerConfig _config)
  : m_keyPair(std::move(_keyPair)),
    m_config(std::move(_config)),
    m_asio(std::make_shared<bcos::network::ASIOInterface>(ioPool(), "0.0.0.0", _port)),
    m_host(makeHost(m_asio))
{}

RlpxServer::~RlpxServer()
{
    try
    {
        m_host->stop();
    }
    catch (...)
    {}
}

EstablishedSession RlpxServer::accept()
{
    std::call_once(m_startOnce, [this] { m_host->start(); });

    NetSessionPtr netSession;
    try
    {
        auto accepted = task::syncWait(m_host->acceptSession());
        netSession = std::move(accepted.session);
    }
    catch (bcos::network::NetworkException const& e)
    {
        throw std::runtime_error(std::string("RlpxServer: accept failed: ") + e.what());
    }
    if (!netSession)
    {
        throw std::runtime_error("RlpxServer: accept failed: no session");
    }

    try
    {
        auto session = establishSession(
            m_host, netSession, m_asio, m_keyPair, /* isInitiator = */ false, {});
        if (m_config.clientId.empty())
        {
            m_config.clientId = "FISCO-BCOS-devp2p-server/v0.1.0";
        }
        return exchangeHandshake(std::move(session), m_keyPair, m_config);
    }
    catch (...)
    {
        // The host stays up: the server may accept another client.
        netSession->drop(bcos::network::DisconnectReason::DisconnectRequested);
        throw;
    }
}

}  // namespace bcos::devp2p::rlpx
