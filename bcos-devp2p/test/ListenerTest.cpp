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
 * @file ListenerTest.cpp
 * @brief Inbound RLPx listener over loopback: a real RlpxClient dials the Listener and
 *        the recipient-side handshake, capability/network/admission rejections (with
 *        the Disconnect reason on the wire), the announced block range and stop()
 *        joining an idle session are checked end to end.
 * @date 2026/11/3
 */
#include <bcos-devp2p/eth/Protocol.h>
#include <bcos-devp2p/rlpx/Client.h>
#include <bcos-devp2p/rlpx/Listener.h>
#include <bcos-devp2p/rlpx/Messages.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

using namespace bcos;
using namespace bcos::devp2p;

BOOST_AUTO_TEST_SUITE(ListenerTest)

namespace
{
h256 const c_genesis(
    std::string_view("0x25a5cc106eea7138acab33231d7160d69cb777ee0c2c553fcddf5138993e6dd9"),
    h256::FromHex);
h256 const c_head(
    std::string_view("0x7777777777777777777777777777777777777777777777777777777777777777"),
    h256::FromHex);

rlpx::PeerConfig serverStatus()
{
    rlpx::PeerConfig config;
    config.clientId = "listener-under-test";
    config.networkId = 11155111;
    config.genesisHash = c_genesis;
    config.forkId = {0x12345678, 0};
    config.earliestBlock = 0;
    config.latestBlock = 42;
    config.latestBlockHash = c_head;
    return config;
}

rlpx::PeerConfig clientConfig(uint16_t _port, rlpx::EccKeyPair const& _serverKey)
{
    rlpx::PeerConfig config;
    config.host = "127.0.0.1";
    config.port = _port;
    config.peerPublicKey = _serverKey.publicKey();
    config.clientId = "dialer";
    config.networkId = 11155111;
    config.genesisHash = c_genesis;
    config.forkId = {0x12345678, 0};
    config.latestBlock = 7;
    config.latestBlockHash = c_head;
    return config;
}

// A handler that answers exactly one GetBlockHeaders with an empty BlockHeaders and
// then returns (the session closes when the EstablishedSession is destroyed).
void answerOneHeadersRequest(rlpx::EstablishedSession& _established)
{
    auto message = _established.session.recvMessage();
    if (!message || message->id != eth::frameId(eth::msg::GetBlockHeaders))
    {
        throw std::runtime_error("expected GetBlockHeaders");
    }
    auto request = eth::decodeGetBlockHeaders(ref(message->data));
    if (!request)
    {
        throw std::runtime_error("malformed GetBlockHeaders");
    }
    eth::BlockHeadersMessage response;
    response.requestId = request->requestId;
    _established.session.sendMessage(
        rlpx::Message{static_cast<uint8_t>(eth::frameId(eth::msg::BlockHeaders)),
            eth::encodeBlockHeaders(response)});
}
}  // namespace

BOOST_AUTO_TEST_CASE(acceptsDialerNegotiatesEth70AndAnnouncesRange)
{
    rlpx::EccKeyPair serverKey;
    std::atomic<int> sessions{0};
    rlpx::HandshakeOptions options;
    options.minEthVersion = eth::kMinServingProtocolVersion;
    auto listener = std::make_shared<rlpx::Listener>(
        serverKey, rlpx::ListenerConfig{"127.0.0.1", 0}, serverStatus,
        [&](rlpx::EstablishedSession& established) {
            ++sessions;
            answerOneHeadersRequest(established);
        },
        options);
    listener->start();
    BOOST_REQUIRE_NE(listener->port(), 0);

    rlpx::EccKeyPair clientKey;
    rlpx::RlpxClient client(clientKey, clientConfig(listener->port(), serverKey));
    auto established = client.connect();
    BOOST_CHECK(established.peerHello.id == serverKey.publicKey());
    BOOST_CHECK_EQUAL(established.peerHello.clientId, "listener-under-test");
    BOOST_CHECK_EQUAL(established.peerHello.listenPort, listener->port());
    BOOST_CHECK_EQUAL(established.peerStatus.protocolVersion, 70u);
    BOOST_CHECK(established.peerStatus.eip7642);
    BOOST_CHECK_EQUAL(established.peerStatus.earliestBlock, 0u);
    BOOST_CHECK_EQUAL(established.peerStatus.latestBlock, 42u);
    BOOST_CHECK(established.peerStatus.latestBlockHash == c_head);
    BOOST_CHECK(established.peerStatus.headHash == c_head);

    eth::GetBlockHeadersMessage request;
    request.requestId = 9;
    established.session.sendMessage(
        rlpx::Message{static_cast<uint8_t>(eth::frameId(eth::msg::GetBlockHeaders)),
            eth::encodeGetBlockHeaders(request)});
    auto response = established.session.recvMessage();
    BOOST_REQUIRE(response.has_value());
    BOOST_CHECK_EQUAL(response->id, eth::frameId(eth::msg::BlockHeaders));
    listener->stop();
    BOOST_CHECK_EQUAL(sessions.load(), 1);
    BOOST_CHECK_EQUAL(listener->liveConnections(), 0u);
}

BOOST_AUTO_TEST_CASE(rejectsLegacyOnlyAndWrongNetworkPeers)
{
    rlpx::EccKeyPair serverKey;
    std::atomic<int> sessions{0};
    rlpx::HandshakeOptions options;
    options.minEthVersion = eth::kMinServingProtocolVersion;
    options.forkIdReject = [](eth::ForkId const& forkId) -> std::optional<std::string> {
        if (forkId.hash != 0x12345678)
        {
            return "unknown fork";
        }
        return std::nullopt;
    };
    auto listener = std::make_shared<rlpx::Listener>(
        serverKey, rlpx::ListenerConfig{"127.0.0.1", 0}, serverStatus,
        [&](rlpx::EstablishedSession& established) {
            ++sessions;
            answerOneHeadersRequest(established);
        },
        options);
    listener->start();

    // The Hello/Status exchange is symmetric: a mismatch the DIALER can see itself
    // (capability, network id) surfaces as its own HandshakeRejected; one only the
    // listener checks (fork-id filter) arrives as the listener's Disconnect
    // frame — either while the dialer still waits for Status, or right after a connect()
    // that already returned because both Status frames crossed on the wire.
    auto expectDisconnect = [&](rlpx::PeerConfig config, rlpx::HandshakeOptions clientOptions,
                                rlpx::DisconnectReason reason) {
        rlpx::EccKeyPair clientKey;
        rlpx::RlpxClient client(clientKey, std::move(config), std::move(clientOptions));
        std::string const expectedText = "reason=" + std::to_string(static_cast<int>(reason));
        try
        {
            auto established = client.connect();
            auto next = established.session.recvMessage();
            BOOST_REQUIRE(next.has_value());
            BOOST_REQUIRE_EQUAL(next->id, rlpx::baseMsg::Disconnect);
            auto disconnect = rlpx::decodeDisconnect(ref(next->data));
            BOOST_REQUIRE(disconnect.has_value());
            BOOST_CHECK(disconnect->reason == reason);
        }
        catch (rlpx::HandshakeRejected const& e)
        {
            BOOST_CHECK_MESSAGE(e.reason == reason, e.what());
        }
        catch (std::exception const& e)
        {
            std::string const what = e.what();
            BOOST_CHECK_MESSAGE(what.find(expectedText) != std::string::npos, what);
        }
    };
    // eth/68-only dialer: no common capability -> UselessPeer (0x03) right after Hello.
    rlpx::HandshakeOptions legacyOnly;
    legacyOnly.maxEthVersion = 68;
    expectDisconnect(
        clientConfig(listener->port(), serverKey), legacyOnly, rlpx::DisconnectReason::UselessPeer);
    // Wrong network id -> UselessPeer during Status.
    auto wrongNetwork = clientConfig(listener->port(), serverKey);
    wrongNetwork.networkId = 1;
    expectDisconnect(wrongNetwork, {}, rlpx::DisconnectReason::UselessPeer);
    // Fork-id rejected by the filter -> UselessPeer during Status.
    auto wrongFork = clientConfig(listener->port(), serverKey);
    wrongFork.forkId = {0xdeadbeef, 0};
    expectDisconnect(wrongFork, {}, rlpx::DisconnectReason::UselessPeer);
    // Inverted block range (earliest > latest) -> ProtocolBreach.
    auto badRange = clientConfig(listener->port(), serverKey);
    badRange.earliestBlock = 10;
    badRange.latestBlock = 5;
    expectDisconnect(badRange, {}, rlpx::DisconnectReason::ProtocolBreach);
    // And a good dialer still gets through afterwards.
    rlpx::EccKeyPair clientKey;
    rlpx::RlpxClient client(clientKey, clientConfig(listener->port(), serverKey));
    auto established = client.connect();
    BOOST_CHECK_EQUAL(established.peerStatus.protocolVersion, 70u);
    listener->stop();
    BOOST_CHECK_EQUAL(sessions.load(), 1);
}

// A failed handshake never reaches the session handler, the only place a peer slot is
// claimed; and connections beyond the limit are closed instead of costing a thread.
BOOST_AUTO_TEST_CASE(connectionLimitClosesExcessSockets)
{
    rlpx::EccKeyPair serverKey;
    rlpx::ListenerConfig config{"127.0.0.1", 0};
    config.maxConnections = 2;
    auto listener = std::make_shared<rlpx::Listener>(
        serverKey, config, serverStatus, [](rlpx::EstablishedSession&) {});
    listener->start();
    auto connect = [&]() {
        rlpx::Socket socket;
        socket.connect("127.0.0.1", listener->port());
        return socket;
    };
    // Two idle dialers sit in the handshake and hold the limit.
    auto first = connect();
    auto second = connect();
    auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (listener->liveConnections() < 2 && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    BOOST_REQUIRE_EQUAL(listener->liveConnections(), 2u);
    // The third is accepted and closed at once: its read ends well before the 15 s I/O timeout.
    auto third = connect();
    auto const start = std::chrono::steady_clock::now();
    BOOST_CHECK_THROW(third.recvFixed(1), std::exception);
    BOOST_CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(5));
    BOOST_CHECK_EQUAL(listener->liveConnections(), 2u);
    first.shutdown();
    second.shutdown();
    listener->stop();
}

// stop() must wake a handler parked in recvMessage (idle peer) and join it promptly —
// well under the 15s socket I/O timeout.
BOOST_AUTO_TEST_CASE(stopWakesIdleSession)
{
    rlpx::EccKeyPair serverKey;
    std::atomic<bool> handlerExited{false};
    auto listener =
        std::make_shared<rlpx::Listener>(serverKey, rlpx::ListenerConfig{"127.0.0.1", 0},
            serverStatus, [&](rlpx::EstablishedSession& established) {
                try
                {
                    for (;;)
                    {
                        auto message = established.session.recvMessage();
                        if (!message)
                        {
                            break;
                        }
                    }
                }
                catch (std::exception const&)
                {
                    // shutdown() from stop() surfaces as a closed connection
                }
                handlerExited = true;
            });
    listener->start();
    rlpx::EccKeyPair clientKey;
    rlpx::RlpxClient client(clientKey, clientConfig(listener->port(), serverKey));
    auto established = client.connect();
    // Let the handler reach recvMessage.
    for (int i = 0; i < 50 && listener->liveConnections() == 0; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    auto const begin = std::chrono::steady_clock::now();
    listener->stop();
    auto const elapsed = std::chrono::steady_clock::now() - begin;
    BOOST_CHECK(handlerExited.load());
    BOOST_CHECK(elapsed < std::chrono::seconds(5));
    // The dialer sees the connection go away.
    BOOST_CHECK_THROW(
        [&] {
            for (int i = 0; i < 3; ++i)
            {
                auto message = established.session.recvMessage();
                (void)message;
            }
        }(),
        std::exception);
    // stop() is idempotent.
    listener->stop();
}

BOOST_AUTO_TEST_SUITE_END()
