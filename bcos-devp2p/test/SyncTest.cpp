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
 * @file SyncTest.cpp
 * @brief End-to-end block-download test: RLPx client connects to a fake peer
 *        and syncs a chain from genesis via HeaderChain + BodySequence.
 * @date 2026/8/18
 */
#include "SyncPeerServer.h"
#include <bcos-devp2p/rlpx/Client.h>
#include <bcos-devp2p/sync/BlockExchange.h>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <stdexcept>
#include <thread>

using namespace bcos;
using namespace bcos::devp2p;

BOOST_AUTO_TEST_SUITE(SyncTest)

BOOST_AUTO_TEST_CASE(downloadChainFromFakePeer)
{
    auto chain = test::makeTestChain(5);

    rlpx::EccKeyPair serverKey;
    rlpx::EccKeyPair clientKey;

    rlpx::PeerConfig serverConfig;
    serverConfig.clientId = "fake-peer";
    rlpx::RlpxServer server(serverKey, 0, serverConfig);
    uint16_t port = server.port();

    std::atomic<bool> serverDone{false};
    std::thread serverThread([&] {
        try
        {
            auto established = server.accept();
            test::serveRequests(established.session, chain);
            serverDone = true;
        }
        catch (std::exception const& e)
        {
            // The client disconnects at the end; that's a normal exit path.
            serverDone = true;
        }
    });

    rlpx::PeerConfig clientConfig;
    clientConfig.host = "127.0.0.1";
    clientConfig.port = port;
    clientConfig.peerPublicKey = serverKey.publicKey();
    clientConfig.clientId = "sync-test";

    std::vector<sync::Block> downloaded;
    {
        rlpx::RlpxClient client(std::move(clientKey), clientConfig);
        auto established = client.connect();

        // Download the whole chain from block 0. The anchor is block 0's parent
        // (zero hash).
        sync::BlockExchange exchange(0, h256{});
        exchange.downloadRange(established.session, chain.size(), [&](sync::Block const& block) {
            downloaded.push_back(block);
        });

        BOOST_REQUIRE_EQUAL(downloaded.size(), chain.size());
        for (size_t i = 0; i < chain.size(); ++i)
        {
            BOOST_CHECK_EQUAL(downloaded[i].number(), chain[i].number());
            BOOST_CHECK(downloaded[i].hash == chain[i].hash);
            BOOST_CHECK(downloaded[i].parentHash() == chain[i].parentHash());
            BOOST_CHECK(downloaded[i].header == chain[i].header);
            BOOST_CHECK(downloaded[i].transactions == chain[i].transactions);
            BOOST_CHECK(downloaded[i].withdrawals == chain[i].withdrawals);
        }
        BOOST_CHECK_EQUAL(exchange.nextNumber(), chain.size());
        BOOST_CHECK(exchange.headHash() == chain.back().hash);
    }  // close the client connection

    serverThread.join();
    BOOST_CHECK(serverDone);
}

// A peer that returns a broken parent chain must be rejected.
BOOST_AUTO_TEST_CASE(brokenParentChainRejected)
{
    auto chain = test::makeTestChain(3);
    // Corrupt the second header's parentHash link by re-hashing a tampered copy.
    chain[1].header.parentInfo.blockHash = h256{0xdeadbeef};
    {
        bcos::bytes rlp;
        bcos::codec::rlp::encode(rlp, chain[1].header);
        chain[1].headerRlp = rlp;  // the server serves the raw RLP, so re-encode it
        chain[1].hash = bcos::crypto::keccak256Hash(
            bcos::bytesConstRef(rlp.data(), rlp.size()));
    }

    rlpx::EccKeyPair serverKey;
    rlpx::EccKeyPair clientKey;
    rlpx::PeerConfig serverConfig;
    serverConfig.clientId = "fake-peer";
    rlpx::RlpxServer server(serverKey, 0, serverConfig);
    uint16_t port = server.port();

    std::thread serverThread([&] {
        try
        {
            auto established = server.accept();
            test::serveRequests(established.session, chain);
        }
        catch (...)
        {
        }
    });

    rlpx::PeerConfig clientConfig;
    clientConfig.host = "127.0.0.1";
    clientConfig.port = port;
    clientConfig.peerPublicKey = serverKey.publicKey();

    {
        rlpx::RlpxClient client(std::move(clientKey), clientConfig);
        auto established = client.connect();

        sync::BlockExchange exchange(0, h256{});
        BOOST_CHECK_THROW(
            exchange.downloadRange(established.session, chain.size(),
                [](sync::Block const&) {}),
            std::runtime_error);
    }  // close the client connection

    serverThread.join();
}

// With the anchor header known, downloaded headers are validated against the
// Ethereum PoS field rules.
BOOST_AUTO_TEST_CASE(downloadChainWithPoSValidation)
{
    auto chain = test::makeTestChain(5);

    rlpx::EccKeyPair serverKey;
    rlpx::EccKeyPair clientKey;
    rlpx::PeerConfig serverConfig;
    serverConfig.clientId = "fake-peer";
    rlpx::RlpxServer server(serverKey, 0, serverConfig);
    uint16_t port = server.port();

    std::thread serverThread([&] {
        try
        {
            auto established = server.accept();
            test::serveRequests(established.session, chain);
        }
        catch (...)
        {
        }
    });

    rlpx::PeerConfig clientConfig;
    clientConfig.host = "127.0.0.1";
    clientConfig.port = port;
    clientConfig.peerPublicKey = serverKey.publicKey();

    std::vector<sync::Block> downloaded;
    {
        rlpx::RlpxClient client(std::move(clientKey), clientConfig);
        auto established = client.connect();

        // Anchor = block 0's header; download blocks 1..4 with PoS checks.
        sync::BlockExchange exchange(1, chain[0].header);
        exchange.downloadRange(established.session, chain.size() - 1,
            [&](sync::Block const& block) { downloaded.push_back(block); });

        BOOST_REQUIRE_EQUAL(downloaded.size(), chain.size() - 1);
        for (size_t i = 0; i < downloaded.size(); ++i)
        {
            BOOST_CHECK_EQUAL(downloaded[i].number(), i + 1);
            BOOST_CHECK(downloaded[i].hash == chain[i + 1].hash);
        }
    }  // close the client connection

    serverThread.join();
}

// A peer serving a header that violates PoS rules (wrong base fee) is rejected
// even though the parent-hash chain is intact.
BOOST_AUTO_TEST_CASE(posValidationRejectsBadBaseFee)
{
    auto chain = test::makeTestChain(3);
    // Tamper block 1's base fee and re-encode its header RLP (the server serves
    // raw RLP). The parent-hash link to block 0 stays intact.
    chain[1].header.baseFee = *chain[1].header.baseFee + 1;
    {
        bcos::bytes rlp;
        bcos::codec::rlp::encode(rlp, chain[1].header);
        chain[1].headerRlp = rlp;
        chain[1].hash = bcos::crypto::keccak256Hash(
            bcos::bytesConstRef(rlp.data(), rlp.size()));
    }

    rlpx::EccKeyPair serverKey;
    rlpx::EccKeyPair clientKey;
    rlpx::PeerConfig serverConfig;
    serverConfig.clientId = "fake-peer";
    rlpx::RlpxServer server(serverKey, 0, serverConfig);
    uint16_t port = server.port();

    std::thread serverThread([&] {
        try
        {
            auto established = server.accept();
            test::serveRequests(established.session, chain);
        }
        catch (...)
        {
        }
    });

    rlpx::PeerConfig clientConfig;
    clientConfig.host = "127.0.0.1";
    clientConfig.port = port;
    clientConfig.peerPublicKey = serverKey.publicKey();

    {
        rlpx::RlpxClient client(std::move(clientKey), clientConfig);
        auto established = client.connect();

        sync::BlockExchange exchange(1, chain[0].header);
        BOOST_CHECK_THROW(
            exchange.downloadRange(established.session, chain.size() - 1,
                [](sync::Block const&) {}),
            std::runtime_error);
    }  // close the client connection

    serverThread.join();
}

BOOST_AUTO_TEST_SUITE_END()
