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
#include <limits>
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
        // The fake headers carry no Shanghai/Cancun fields, so keep those forks
        // inactive (UINT64_MAX = never) and exercise the PoS/London rules only.
        sync::ChainConfig config;
        config.shanghaiTime = std::numeric_limits<uint64_t>::max();
        config.cancunTime = std::numeric_limits<uint64_t>::max();
        config.pragueTime = std::numeric_limits<uint64_t>::max();
        sync::BlockExchange exchange(1, chain[0].header, config);
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

        sync::ChainConfig config;
        config.shanghaiTime = std::numeric_limits<uint64_t>::max();
        config.cancunTime = std::numeric_limits<uint64_t>::max();
        config.pragueTime = std::numeric_limits<uint64_t>::max();
        sync::BlockExchange exchange(1, chain[0].header, config);
        // The typed classification is load-bearing: the sync loop routes
        // HeaderRuleViolation to the deterministic-failure path (no retry
        // against further bootnodes), so this must not regress to a plain
        // std::runtime_error (which would be classified as transient).
        BOOST_CHECK_THROW(
            exchange.downloadRange(established.session, chain.size() - 1,
                [](sync::Block const&) {}),
            sync::HeaderRuleViolation);
    }  // close the client connection

    serverThread.join();
}

// A first header whose parentHash is not the anchor signals a fork/reorg and
// must throw ParentHashMismatch specifically — the sync loop's three-strike
// reorg FATAL keys on exactly that type, so a regression to a plain
// std::runtime_error would silently turn the reorg signal into a transient
// retry. (brokenParentChainRejected above corrupts a LATER link, which is the
// untyped "broken parent chain" throw and does not pin this type.)
BOOST_AUTO_TEST_CASE(firstHeaderParentMismatchThrowsTyped)
{
    auto chain = test::makeTestChain(3);
    // Corrupt the FIRST header's parent link so it no longer matches the
    // anchor, and re-encode its header RLP (the server serves raw RLP).
    chain[0].header.parentInfo.blockHash = h256{0xdeadbeef};
    {
        bcos::bytes rlp;
        bcos::codec::rlp::encode(rlp, chain[0].header);
        chain[0].headerRlp = rlp;
        chain[0].hash = bcos::crypto::keccak256Hash(
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
            sync::ParentHashMismatch);
    }  // close the client connection

    serverThread.join();
}

BOOST_AUTO_TEST_SUITE_END()

// ─── OP-Stack shapes over a full RLPx session ────────────────────────────────
// The serving side is shape-agnostic (SyncPeerServer serves raw header RLP), so
// these cases exercise exactly what the opstack-el sync driver wires up in
// production: an OP-shaped served chain + makeOpHeaderValidator injected through
// BlockExchange::setHeaderValidator.
BOOST_AUTO_TEST_SUITE(OpSyncTest)

namespace
{
// Bedrock shape: isthmus_time SET to a far-future time so the full Bedrock..Karst
// ladder is live (an unset isthmus_time is the "Isthmus zero-start baseline",
// which is not the pre-Canyon world this fixture models); every other rung stays
// UINT64_MAX ("not scheduled", implied/skipped), so all blocks resolve to Bedrock.
sync::OpChainConfig bedrockConfig()
{
    sync::OpChainConfig config;
    config.chainId = 11155420;
    config.blockTimeSeconds = 2;
    config.forkSchedule.m_isthmusTime = 4102444800;  // 2100-01-01
    return config;
}

sync::OpChainConfig canyonConfig()
{
    auto config = bedrockConfig();
    config.forkSchedule.m_canyonTime = 0;  // active from genesis
    return config;
}

sync::OpChainConfig ecotoneConfig()
{
    auto config = canyonConfig();
    config.forkSchedule.m_ecotoneTime = 0;
    return config;
}

sync::OpChainConfig holoceneConfig()
{
    auto config = ecotoneConfig();
    config.forkSchedule.m_holoceneTime = 0;
    return config;
}

// The current-superchain shape: Isthmus/Jovian at genesis (requestsHash + 17B
// extraData + the Jovian base-fee rule).
sync::OpChainConfig jovianConfig()
{
    auto config = holoceneConfig();
    config.forkSchedule.m_isthmusTime = 0;
    config.forkSchedule.m_jovianTime = 0;
    return config;
}

// One RLPx round: a fake peer serves `_chain`, `_download` runs on the connected
// session and the round ends when the client disconnects.
template <typename F>
void runOpRound(std::vector<sync::Block> const& chain, F&& download)
{
    rlpx::EccKeyPair serverKey;
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
            // The client disconnects at the end; that's a normal exit path.
        }
    });

    rlpx::PeerConfig clientConfig;
    clientConfig.host = "127.0.0.1";
    clientConfig.port = port;
    clientConfig.peerPublicKey = serverKey.publicKey();
    {
        rlpx::EccKeyPair clientKey;
        rlpx::RlpxClient client(std::move(clientKey), clientConfig);
        auto established = client.connect();
        download(established.session);
    }  // close the client connection

    serverThread.join();
}
}  // namespace

// Normal path, Bedrock shape (no withdrawalsHash, empty extraData, Bedrock 50/6
// base fee): the OP validator accepts the whole chain.
BOOST_AUTO_TEST_CASE(opBedrockChainDownloadsWithOpValidator)
{
    auto config = bedrockConfig();
    auto chain = test::makeOpTestChain(5, config);

    std::vector<sync::Block> downloaded;
    runOpRound(chain, [&](rlpx::Session& session) {
        sync::BlockExchange exchange(1, chain[0].header, sync::ChainConfig{});
        exchange.setHeaderValidator(sync::makeOpHeaderValidator(config));
        exchange.downloadRange(session, chain.size() - 1,
            [&](sync::Block const& block) { downloaded.push_back(block); });
    });

    BOOST_REQUIRE_EQUAL(downloaded.size(), chain.size() - 1);
    for (size_t i = 0; i < downloaded.size(); ++i)
    {
        BOOST_CHECK_EQUAL(downloaded[i].number(), i + 1);
        BOOST_CHECK(downloaded[i].hash == chain[i + 1].hash);
        BOOST_CHECK(!downloaded[i].header.withdrawalsHash.has_value());
        BOOST_CHECK(downloaded[i].header.extraData.empty());
    }
}

// Contrast: the SAME served OP Bedrock chain must fail the default L1 PoS
// validator — its EIP-1559 recomputation uses the L1 elasticity/denominator
// (2/8), not the OP Bedrock constants (6/50). This is the assertion that the
// validator injection is load-bearing, not decoration.
BOOST_AUTO_TEST_CASE(defaultL1ValidatorRejectsOpChain)
{
    auto config = bedrockConfig();
    auto chain = test::makeOpTestChain(3, config);

    runOpRound(chain, [&](rlpx::Session& session) {
        sync::ChainConfig l1Config;
        l1Config.shanghaiTime = std::numeric_limits<uint64_t>::max();
        l1Config.cancunTime = std::numeric_limits<uint64_t>::max();
        l1Config.pragueTime = std::numeric_limits<uint64_t>::max();
        sync::BlockExchange exchange(1, chain[0].header, l1Config);
        BOOST_CHECK_THROW(
            exchange.downloadRange(session, chain.size() - 1, [](sync::Block const&) {}),
            sync::HeaderRuleViolation);
    });
}

// Normal path, Canyon shape: every header carries the empty-withdrawals hash and
// every body the (empty) withdrawals list.
BOOST_AUTO_TEST_CASE(opCanyonChainDownloadsWithOpValidator)
{
    auto config = canyonConfig();
    auto chain = test::makeOpTestChain(5, config);

    std::vector<sync::Block> downloaded;
    runOpRound(chain, [&](rlpx::Session& session) {
        sync::BlockExchange exchange(1, chain[0].header, sync::ChainConfig{});
        exchange.setHeaderValidator(sync::makeOpHeaderValidator(config));
        exchange.downloadRange(session, chain.size() - 1,
            [&](sync::Block const& block) { downloaded.push_back(block); });
    });

    BOOST_REQUIRE_EQUAL(downloaded.size(), chain.size() - 1);
    for (size_t i = 0; i < downloaded.size(); ++i)
    {
        BOOST_CHECK(downloaded[i].hash == chain[i + 1].hash);
        BOOST_REQUIRE(downloaded[i].header.withdrawalsHash.has_value());
        BOOST_CHECK(*downloaded[i].header.withdrawalsHash == sync::c_opEmptyWithdrawalsHash);
        BOOST_CHECK(downloaded[i].hasWithdrawals());
    }
}

// Normal path, Ecotone shape: withdrawalsHash + the Cancun blob fields
// (blobGasUsed/excessBlobGas/parentBeaconRoot, all zero on OP chains).
BOOST_AUTO_TEST_CASE(opEcotoneChainDownloadsWithOpValidator)
{
    auto config = ecotoneConfig();
    auto chain = test::makeOpTestChain(5, config);

    std::vector<sync::Block> downloaded;
    runOpRound(chain, [&](rlpx::Session& session) {
        sync::BlockExchange exchange(1, chain[0].header, sync::ChainConfig{});
        exchange.setHeaderValidator(sync::makeOpHeaderValidator(config));
        exchange.downloadRange(session, chain.size() - 1,
            [&](sync::Block const& block) { downloaded.push_back(block); });
    });

    BOOST_REQUIRE_EQUAL(downloaded.size(), chain.size() - 1);
    for (size_t i = 0; i < downloaded.size(); ++i)
    {
        BOOST_CHECK(downloaded[i].hash == chain[i + 1].hash);
        BOOST_CHECK(downloaded[i].header.blobGasUsed.has_value());
        BOOST_CHECK(downloaded[i].header.excessBlobGas.has_value());
        BOOST_CHECK(downloaded[i].header.parentBeaconRoot.has_value());
        BOOST_CHECK_EQUAL(*downloaded[i].header.blobGasUsed, bcos::u256(0));
        BOOST_CHECK_EQUAL(*downloaded[i].header.excessBlobGas, bcos::u256(0));
    }
}

// Normal path, Jovian shape (the current superchain baseline): requestsHash,
// 17-byte extraData and the Jovian base-fee rule over the wire.
BOOST_AUTO_TEST_CASE(opJovianChainDownloadsWithOpValidator)
{
    auto config = jovianConfig();
    auto chain = test::makeOpTestChain(5, config);

    std::vector<sync::Block> downloaded;
    runOpRound(chain, [&](rlpx::Session& session) {
        sync::BlockExchange exchange(1, chain[0].header, sync::ChainConfig{});
        exchange.setHeaderValidator(sync::makeOpHeaderValidator(config));
        exchange.downloadRange(session, chain.size() - 1,
            [&](sync::Block const& block) { downloaded.push_back(block); });
    });

    BOOST_REQUIRE_EQUAL(downloaded.size(), chain.size() - 1);
    for (size_t i = 0; i < downloaded.size(); ++i)
    {
        BOOST_CHECK(downloaded[i].hash == chain[i + 1].hash);
        BOOST_REQUIRE(downloaded[i].header.requestsHash.has_value());
        BOOST_CHECK(*downloaded[i].header.requestsHash == sync::c_opEmptyRequestsHash);
        BOOST_CHECK_EQUAL(downloaded[i].header.extraData.size(), 17u);
    }
}

// Fault path: a Canyon-active header WITHOUT withdrawalsHash violates the OP
// fork-gated presence rule. The header's own parent link stays intact, so the
// rejection must come from the field rules — typed HeaderRuleViolation, which
// the sync loop routes to the deterministic-failure path.
BOOST_AUTO_TEST_CASE(canyonHeaderMissingWithdrawalsHashThrows)
{
    auto config = canyonConfig();
    auto chain = test::makeOpTestChain(3, config);
    chain[1].header.withdrawalsHash.reset();
    test::reencodeOpHeader(chain[1]);

    runOpRound(chain, [&](rlpx::Session& session) {
        sync::HeaderChain headers(1, chain[0].header, sync::ChainConfig{});
        headers.setHeaderValidator(sync::makeOpHeaderValidator(config));
        BOOST_CHECK_THROW(headers.requestHeaders(session, chain.size() - 1),
            sync::HeaderRuleViolation);
    });
}

// Fault path: a Holocene-active header with a malformed extraData (5 bytes
// instead of the required 9) violates the extraData shape rule — again typed
// HeaderRuleViolation from requestHeaders.
BOOST_AUTO_TEST_CASE(holoceneHeaderBadExtraDataThrows)
{
    auto config = holoceneConfig();
    auto chain = test::makeOpTestChain(3, config);
    chain[1].header.extraData = bcos::bytes(5, 0);
    test::reencodeOpHeader(chain[1]);

    runOpRound(chain, [&](rlpx::Session& session) {
        sync::HeaderChain headers(1, chain[0].header, sync::ChainConfig{});
        headers.setHeaderValidator(sync::makeOpHeaderValidator(config));
        BOOST_CHECK_THROW(headers.requestHeaders(session, chain.size() - 1),
            sync::HeaderRuleViolation);
    });
}

BOOST_AUTO_TEST_SUITE_END()
