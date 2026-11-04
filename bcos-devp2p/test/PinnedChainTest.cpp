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
 * @file PinnedChainTest.cpp
 * @brief HeaderChain::requestPinnedHashes: a discovered peer proves the chain from the
 *        CL's target down to the local head before anything is executed. A peer that
 *        cannot prove it must fail here, not after its blocks are committed.
 * @date 2026/11/4
 */
#include "SyncPeerServer.h"
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-devp2p/rlpx/Client.h>
#include <bcos-devp2p/sync/BlockExchange.h>
#include <boost/test/unit_test.hpp>
#include <functional>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace bcos;
using namespace bcos::devp2p;

BOOST_AUTO_TEST_SUITE(PinnedChainTest)

namespace
{
// Run `_client` against a loopback peer serving `_chain`.
void withPeer(std::vector<sync::Block> const& _chain,
    std::function<void(rlpx::EstablishedSession&)> const& _client)
{
    rlpx::EccKeyPair serverKey;
    rlpx::EccKeyPair clientKey;
    rlpx::PeerConfig serverConfig;
    serverConfig.clientId = "peer";
    rlpx::RlpxServer server(serverKey, 0, serverConfig);
    rlpx::PeerConfig clientConfig;
    clientConfig.host = "127.0.0.1";
    clientConfig.port = server.port();
    clientConfig.peerPublicKey = serverKey.publicKey();
    std::thread serverThread([&] {
        try
        {
            auto established = server.accept();
            devp2p::test::serveRequests(established, _chain);
        }
        catch (...)
        {  // the client hanging up is the normal end
        }
    });
    {
        rlpx::RlpxClient client(std::move(clientKey), clientConfig);
        auto established = client.connect();
        _client(established);
    }
    serverThread.join();
}
}  // namespace

// Crosses the 192-header request size, from genesis and from a mid-chain anchor.
BOOST_AUTO_TEST_CASE(provesTheChainDownToTheAnchor)
{
    auto const chain = devp2p::test::makeTestChain(300);
    withPeer(chain, [&](rlpx::EstablishedSession& _established) {
        sync::BlockExchange exchange(0, h256{});
        auto const hashes =
            exchange.requestPinnedHashes(_established.session, chain[299].hash, 299);
        BOOST_REQUIRE_EQUAL(hashes.size(), 300u);
        for (size_t i = 0; i < hashes.size(); ++i)
        {
            BOOST_CHECK(hashes[i] == chain[i].hash);
        }
        sync::BlockExchange midChain(10, chain[9].hash);
        auto const tail = midChain.requestPinnedHashes(_established.session, chain[20].hash, 20);
        BOOST_REQUIRE_EQUAL(tail.size(), 11u);
        BOOST_CHECK(tail.front() == chain[10].hash);
        BOOST_CHECK(tail.back() == chain[20].hash);
    });
}

// An authentic chain that does not build on our head is a fork, typed as such.
BOOST_AUTO_TEST_CASE(authenticChainOnAnotherAnchorIsTyped)
{
    auto const chain = devp2p::test::makeTestChain(12);
    withPeer(chain, [&](rlpx::EstablishedSession& _established) {
        sync::BlockExchange exchange(10, h256{0x01});
        BOOST_CHECK_THROW(exchange.requestPinnedHashes(_established.session, chain[11].hash, 11),
            sync::ParentHashMismatch);
    });
}

// The attack: the top of the chain is genuine (so the CL's hash resolves) and everything
// below it is forged. The first forged header breaks the hash link and nothing is returned.
BOOST_AUTO_TEST_CASE(forgedAncestorIsRejected)
{
    auto chain = devp2p::test::makeTestChain(5);
    auto const tip = chain[4].hash;
    chain[2].header.extraData = {0x66};
    chain[2].headerRlp.clear();
    bcos::codec::rlp::encode(chain[2].headerRlp, chain[2].header);
    chain[2].hash = bcos::crypto::keccak256Hash(
        bytesConstRef(chain[2].headerRlp.data(), chain[2].headerRlp.size()));
    withPeer(chain, [&](rlpx::EstablishedSession& _established) {
        sync::BlockExchange exchange(0, h256{});
        BOOST_CHECK_THROW(
            exchange.requestPinnedHashes(_established.session, tip, 4), std::runtime_error);
    });
}

BOOST_AUTO_TEST_CASE(unknownTipAndWrongNumberAreRejected)
{
    auto const chain = devp2p::test::makeTestChain(5);
    withPeer(chain, [&](rlpx::EstablishedSession& _established) {
        sync::BlockExchange exchange(0, h256{});
        BOOST_CHECK_THROW(
            exchange.requestPinnedHashes(_established.session, h256{0x02}, 4), std::runtime_error);
        BOOST_CHECK_THROW(exchange.requestPinnedHashes(_established.session, chain[4].hash, 3),
            std::runtime_error);
    });
}

BOOST_AUTO_TEST_SUITE_END()
