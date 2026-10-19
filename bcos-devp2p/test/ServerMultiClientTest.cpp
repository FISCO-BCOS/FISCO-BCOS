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
 * @file ServerMultiClientTest.cpp
 * @brief Regression tests for the two lifecycle bugs the bcos-network migration introduced in the
 *        devp2p facade:
 *          1. an accepted (server-side) session stopped the SERVER's Host, so RlpxServer served
 *             exactly one client — the fix is the ownsHost split on rlpx::Session;
 *          2. RlpxClient built its ASIOInterface with listenPort 0, and the acceptor's endpoint
 *             constructor binds + listens, so a pure client exposed an OS-assigned listener whose
 *             accepted sessions were queued and never drained — the fix is listen=false.
 *        Neither was reachable through the previous suite, which built a fresh RlpxServer per
 *        accept().
 * @date 2026/10/17
 */
#include <bcos-devp2p/eth/Protocol.h>
#include <bcos-devp2p/rlpx/Client.h>
#include <bcos-devp2p/rlpx/Messages.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace bcos;
using namespace bcos::devp2p;
using namespace bcos::devp2p::rlpx;

BOOST_AUTO_TEST_SUITE(ServerMultiClientTest)

namespace
{
// Chain identity shared by the server and both clients.
constexpr char kGenesis[] = "0x25a5cc106eea7138acab33231d7160d69cb777ee0c2c553fcddf5138993e6dd9";

PeerConfig serverConfig()
{
    PeerConfig config;
    config.clientId = "FISCO-BCOS-devp2p-server/v0.1.0";
    config.networkId = 11155111;
    config.genesisHash = h256(std::string_view(kGenesis), h256::FromHex);
    config.forkId = {0x12345678, 0};
    return config;
}

PeerConfig clientConfig(uint16_t port, bcos::bytes serverPublicKey)
{
    PeerConfig config;
    config.host = "127.0.0.1";
    config.port = port;
    config.peerPublicKey = std::move(serverPublicKey);
    config.networkId = 11155111;
    config.genesisHash = h256(std::string_view(kGenesis), h256::FromHex);
    config.forkId = {0x12345678, 0};
    return config;
}
}  // namespace

// The server must keep serving after a client disconnects: accept two clients in sequence, each
// completing the full RLPx + Hello + Status exchange. Before the ownsHost fix the first
// EstablishedSession's destructor stopped the server's Host, so the second accept() threw
// "HostStopped".
BOOST_AUTO_TEST_CASE(serverAcceptsSequentialClients)
{
    rlpx::EccKeyPair serverKey;
    auto config = serverConfig();

    rlpx::RlpxServer server(serverKey, 0, config);
    uint16_t port = server.port();
    auto serverPublicKey = serverKey.publicKey();

    constexpr int kClients = 2;
    std::vector<rlpx::EccKeyPair> clientKeys;
    std::vector<bcos::bytes> clientPublicKeys;
    clientKeys.reserve(kClients);
    for (int i = 0; i < kClients; ++i)
    {
        clientKeys.emplace_back();
        clientPublicKeys.push_back(clientKeys.back().publicKey());
    }
    std::atomic<int> accepted{0};
    // One accept per client, on one thread: the whole point is that the SERVER object (and its
    // Host) survives all of them.
    std::thread serverThread([&] {
        for (int i = 0; i < kClients; ++i)
        {
            try
            {
                // Scoped: `established` (and with it the server-side session) is destroyed at the
                // end of every iteration, before the next accept().
                auto established = server.accept();
                // The server really completed a handshake with THIS client (its Hello carries the
                // client's uncompressed public key), not merely "did not throw".
                auto index = static_cast<std::size_t>(accepted.fetch_add(1));
                BOOST_REQUIRE(index < clientPublicKeys.size());
                BOOST_CHECK(established.peerHello.id == clientPublicKeys[index]);
                BOOST_CHECK_EQUAL(established.peerStatus.networkId, 11155111u);
            }
            catch (std::exception const& e)
            {
                BOOST_ERROR(std::string("server: accept #") + std::to_string(i) + " failed: " +
                            e.what());
                return;
            }
        }
    });

    for (int i = 0; i < kClients; ++i)
    {
        rlpx::RlpxClient client(clientKeys[static_cast<std::size_t>(i)],
            clientConfig(port, serverPublicKey));
        try
        {
            auto established = client.connect();
            BOOST_CHECK(established.peerHello.id.size() == 64);
            BOOST_CHECK_EQUAL(established.peerStatus.networkId, 11155111u);
        }
        catch (std::exception const& e)
        {
            BOOST_ERROR(std::string("client #") + std::to_string(i) + " failed: " + e.what());
            break;
        }
        // `established` and `client` die here: the connection is torn down before the next client.
    }

    serverThread.join();
    BOOST_CHECK_EQUAL(accepted.load(), kClients);
}

// A pure client must not create a listening socket. Verified through the public surface the facade
// exposes: repeat connections must each succeed (nothing is left in a half-accepted state), and —
// the direct part of the claim — ASIOInterface built the way RlpxClient builds it reports no
// acceptor at all.
BOOST_AUTO_TEST_CASE(pureClientDoesNotListen)
{
    auto asio = std::make_shared<bcos::network::ASIOInterface>(
        std::make_shared<bcos::IOServicePool>(1, "devp2p-test"), "0.0.0.0", 0,
        /* listen = */ false);
    BOOST_CHECK(!asio->listening());
    BOOST_CHECK(asio->acceptor() == nullptr);

    // The server flavour still listens (and can report its port).
    auto listening = std::make_shared<bcos::network::ASIOInterface>(
        std::make_shared<bcos::IOServicePool>(1, "devp2p-test"), "127.0.0.1", 0,
        /* listen = */ true);
    BOOST_REQUIRE(listening->listening());
    BOOST_REQUIRE(listening->acceptor() != nullptr);
    BOOST_CHECK(listening->acceptor()->local_endpoint().port() != 0);

    // And a client Host built on the non-listening interface neither runs an accept loop nor
    // exposes a port: start() must be a no-op for it rather than spawning acceptLoop.
    auto plainSocketHost = std::make_shared<
        bcos::network::Host<RlpxFrameDecoder, bcos::network::PlainSocket>>(asio, nullptr);
    plainSocketHost->setListen(false);
    plainSocketHost->start();
    BOOST_CHECK(plainSocketHost->haveNetwork());
    BOOST_CHECK(!plainSocketHost->listen());
    plainSocketHost->stop();
}

BOOST_AUTO_TEST_SUITE_END()
