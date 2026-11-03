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
 * @file PeerServerTest.cpp
 * @brief The eth serving loop: pure dispatch over an in-memory chain (header walks,
 *        caps, soft limit, bodies, receipts on 68/69/70 incl. EIP-7975 partial lists,
 *        base-protocol and breach handling) and a loopback session where a real
 *        RlpxClient downloads the chain, fetches receipts, sees a BlockRangeUpdate
 *        after a head advance and disconnects.
 * @date 2026/11/3
 */
#include "SyncPeerServer.h"
#include <bcos-devp2p/eth/Protocol.h>
#include <bcos-devp2p/rlpx/Client.h>
#include <bcos-devp2p/rlpx/Messages.h>
#include <bcos-devp2p/sync/BlockExchange.h>
#include <bcos-devp2p/sync/PeerServer.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <atomic>
#include <limits>
#include <thread>
#include <vector>

using namespace bcos;
using namespace bcos::devp2p;

BOOST_AUTO_TEST_SUITE(PeerServerTest)

namespace
{
rlpx::Message ethFrame(uint8_t _code, bcos::bytes _payload)
{
    return rlpx::Message{static_cast<uint8_t>(eth::frameId(_code)), std::move(_payload)};
}

std::vector<uint64_t> headerNumbers(sync::PeerServer::Outcome const& _outcome)
{
    BOOST_REQUIRE(_outcome.kind == sync::PeerServer::Outcome::Kind::Reply);
    BOOST_REQUIRE_EQUAL(_outcome.reply.id, eth::frameId(eth::msg::BlockHeaders));
    auto decoded = eth::decodeBlockHeaders(ref(_outcome.reply.data));
    BOOST_REQUIRE(decoded.has_value());
    std::vector<uint64_t> numbers;
    for (auto const& rlp : decoded->headers)
    {
        protocol::EthBlockHeader header;
        protocol::EthBlockHeader::toEthBlockHeader(header, ref(rlp));
        numbers.push_back(static_cast<uint64_t>(header.data().number));
    }
    return numbers;
}

protocol::EthReceiptData receipt(uint8_t _type, uint64_t _cumulativeGas)
{
    protocol::EthReceiptData data;
    data.type = _type;
    data.status = 1;
    data.cumulativeGasUsed = _cumulativeGas;
    protocol::EthLogData log;
    log.address = Address{};
    log.data = fromHex("aabb");
    data.logs.push_back(log);
    data.logsBloom[3] = 0x01;
    return data;
}

eth::GetBlockHeadersMessage headersRequest(
    uint64_t _origin, uint64_t _amount, uint64_t _skip, bool _reverse)
{
    eth::GetBlockHeadersMessage request;
    request.requestId = 1;
    request.originNumber = _origin;
    request.amount = _amount;
    request.skip = _skip;
    request.reverse = _reverse;
    return request;
}
}  // namespace

BOOST_AUTO_TEST_CASE(headerWalks)
{
    auto chain = test::makeTestChain(10);
    test::InMemoryBlockSource source(chain);
    sync::ServingLimits limits;
    limits.maxHeaders = 4;
    sync::PeerServer server(source, limits);
    auto ask = [&](eth::GetBlockHeadersMessage const& request) {
        return headerNumbers(server.handle(
            70, ethFrame(eth::msg::GetBlockHeaders, eth::encodeGetBlockHeaders(request))));
    };
    BOOST_CHECK((ask(headersRequest(0, 3, 0, false)) == std::vector<uint64_t>{0, 1, 2}));
    BOOST_CHECK((ask(headersRequest(5, 3, 1, true)) == std::vector<uint64_t>{5, 3, 1}));
    BOOST_CHECK((ask(headersRequest(1, 3, 1, true)) == std::vector<uint64_t>{1}));      // underflow
                                                                                        // stop
    BOOST_CHECK((ask(headersRequest(8, 5, 0, false)) == std::vector<uint64_t>{8, 9}));  // past head
    BOOST_CHECK(
        (ask(headersRequest(0, 100, 0, false)) == std::vector<uint64_t>{0, 1, 2, 3}));  // cap
    BOOST_CHECK(ask(headersRequest(0, 0, 0, false)).empty());
    BOOST_CHECK(ask(headersRequest(50, 2, 0, false)).empty());
    // skip overflow: the second step would wrap -> only the origin is served
    BOOST_CHECK((ask(headersRequest(2, 3, std::numeric_limits<uint64_t>::max() - 1, false)) ==
                 std::vector<uint64_t>{2}));
    // by hash, forward from block 4; unknown hash -> empty
    auto byHash = headersRequest(0, 2, 0, false);
    byHash.originHash = chain[4].hash;
    BOOST_CHECK((ask(byHash) == std::vector<uint64_t>{4, 5}));
    byHash.originHash =
        h256(std::string_view("0x9999999999999999999999999999999999999999999999999999999999999999"),
            h256::FromHex);
    BOOST_CHECK(ask(byHash).empty());
    // soft response limit: every header counts at least estHeaderBytes, so a 1-byte
    // budget still serves exactly one header
    sync::ServingLimits tiny;
    tiny.softResponseBytes = 1;
    sync::PeerServer small(source, tiny);
    BOOST_CHECK((headerNumbers(small.handle(70, ethFrame(eth::msg::GetBlockHeaders,
                                                    eth::encodeGetBlockHeaders(headersRequest(0, 5,
                                                        0, false))))) == std::vector<uint64_t>{0}));
}

BOOST_AUTO_TEST_CASE(bodiesSkipUnknownAndCap)
{
    auto chain = test::makeTestChain(6);
    test::InMemoryBlockSource source(chain);
    sync::ServingLimits limits;
    limits.maxBodies = 2;
    sync::PeerServer server(source, limits);
    eth::GetBlockBodiesMessage request;
    request.requestId = 5;
    h256 const unknown(
        std::string_view("0x9999999999999999999999999999999999999999999999999999999999999999"),
        h256::FromHex);
    request.hashes = {chain[2].hash, unknown, chain[5].hash, chain[1].hash};
    auto outcome =
        server.handle(69, ethFrame(eth::msg::GetBlockBodies, eth::encodeGetBlockBodies(request)));
    BOOST_REQUIRE(outcome.kind == sync::PeerServer::Outcome::Kind::Reply);
    auto decoded = eth::decodeBlockBodies(ref(outcome.reply.data));
    BOOST_REQUIRE(decoded.has_value());
    BOOST_CHECK_EQUAL(decoded->requestId, 5u);
    // Only the first maxBodies (2) hashes are considered: block 2 served, unknown skipped.
    BOOST_REQUIRE_EQUAL(decoded->bodies.size(), 1u);
    BOOST_CHECK(decoded->bodies[0].transactions == chain[2].transactions);
    BOOST_CHECK(!decoded->bodies[0].withdrawals.has_value());
}

BOOST_AUTO_TEST_CASE(receiptsAcrossVersions)
{
    auto chain = test::makeTestChain(6);
    test::InMemoryBlockSource source(chain);
    source.setReceipts(2, {receipt(0, 21000), receipt(2, 42000)});
    source.setReceipts(3, {receipt(1, 50000)});
    sync::PeerServer server(source);
    h256 const unknown(
        std::string_view("0x9999999999999999999999999999999999999999999999999999999999999999"),
        h256::FromHex);
    eth::GetReceiptsMessage request;
    request.requestId = 8;
    request.hashes = {chain[2].hash, unknown, chain[3].hash, chain[4].hash};
    for (uint8_t version : {68, 69, 70})
    {
        auto outcome = server.handle(
            version, ethFrame(eth::msg::GetReceipts, eth::encodeGetReceipts(request, version)));
        BOOST_REQUIRE(outcome.kind == sync::PeerServer::Outcome::Kind::Reply);
        BOOST_REQUIRE_EQUAL(outcome.reply.id, eth::frameId(eth::msg::Receipts));
        auto decoded = eth::decodeReceipts(ref(outcome.reply.data), version);
        BOOST_REQUIRE(decoded.has_value());
        BOOST_CHECK_EQUAL(decoded->requestId, 8u);
        BOOST_CHECK(!decoded->lastBlockIncomplete);
        // block 2 (2 receipts), block 3 (1), block 4 (known, empty list); unknown skipped
        BOOST_REQUIRE_EQUAL(decoded->receipts.size(), 3u);
        BOOST_CHECK_EQUAL(decoded->receipts[0].size(), 2u);
        BOOST_CHECK_EQUAL(decoded->receipts[1].size(), 1u);
        BOOST_CHECK_EQUAL(decoded->receipts[2].size(), 0u);
        auto second = eth::decodeReceiptElement(ref(decoded->receipts[0][1]), version);
        BOOST_REQUIRE(second.has_value());
        BOOST_CHECK_EQUAL(second->type, 2);
        BOOST_CHECK_EQUAL(second->cumulativeGasUsed, 42000u);
        // bloom travels on eth/68 only
        BOOST_CHECK_EQUAL(second->logsBloom[3], version == 68 ? 0x01 : 0x00);
    }
    // eth/70 partial list: start at index 1 of the first block
    request.firstBlockReceiptIndex = 1;
    {
        auto outcome =
            server.handle(70, ethFrame(eth::msg::GetReceipts, eth::encodeGetReceipts(request, 70)));
        auto decoded = eth::decodeReceipts(ref(outcome.reply.data), 70);
        BOOST_REQUIRE(decoded.has_value());
        BOOST_REQUIRE_EQUAL(decoded->receipts.size(), 3u);
        BOOST_CHECK_EQUAL(decoded->receipts[0].size(), 1u);
        auto only = eth::decodeReceiptElement(ref(decoded->receipts[0][0]), 70);
        BOOST_REQUIRE(only.has_value());
        BOOST_CHECK_EQUAL(only->type, 2);
        BOOST_CHECK_EQUAL(decoded->receipts[1].size(), 1u);
    }
    // soft limit of one byte: eth/70 cuts after the first receipt and flags the block;
    // eth/69 serves the first block whole and stops before the next block
    request.firstBlockReceiptIndex = 0;
    sync::ServingLimits tiny;
    tiny.softResponseBytes = 1;
    sync::PeerServer small(source, tiny);
    {
        auto outcome =
            small.handle(70, ethFrame(eth::msg::GetReceipts, eth::encodeGetReceipts(request, 70)));
        auto decoded = eth::decodeReceipts(ref(outcome.reply.data), 70);
        BOOST_REQUIRE(decoded.has_value());
        BOOST_CHECK(decoded->lastBlockIncomplete);
        BOOST_REQUIRE_EQUAL(decoded->receipts.size(), 1u);
        BOOST_CHECK_EQUAL(decoded->receipts[0].size(), 1u);
        // the requester continues from index 1 of block 2
        eth::GetReceiptsMessage resume;
        resume.requestId = 9;
        resume.firstBlockReceiptIndex = 1;
        resume.hashes = {chain[2].hash, chain[3].hash};
        auto next =
            small.handle(70, ethFrame(eth::msg::GetReceipts, eth::encodeGetReceipts(resume, 70)));
        auto decodedNext = eth::decodeReceipts(ref(next.reply.data), 70);
        BOOST_REQUIRE(decodedNext.has_value());
        BOOST_REQUIRE_GE(decodedNext->receipts.size(), 1u);
        BOOST_CHECK_EQUAL(decodedNext->receipts[0].size(), 1u);
        auto resumed = eth::decodeReceiptElement(ref(decodedNext->receipts[0][0]), 70);
        BOOST_REQUIRE(resumed.has_value());
        BOOST_CHECK_EQUAL(resumed->type, 2);
    }
    {
        auto outcome =
            small.handle(69, ethFrame(eth::msg::GetReceipts, eth::encodeGetReceipts(request, 69)));
        auto decoded = eth::decodeReceipts(ref(outcome.reply.data), 69);
        BOOST_REQUIRE(decoded.has_value());
        BOOST_CHECK(!decoded->lastBlockIncomplete);
        BOOST_REQUIRE_EQUAL(decoded->receipts.size(), 1u);
        BOOST_CHECK_EQUAL(decoded->receipts[0].size(), 2u);
    }
}

BOOST_AUTO_TEST_CASE(baseProtocolAndBreaches)
{
    auto chain = test::makeTestChain(3);
    test::InMemoryBlockSource source(chain);
    sync::PeerServer server(source);
    using Kind = sync::PeerServer::Outcome::Kind;

    auto pong = server.handle(70, rlpx::Message{rlpx::baseMsg::Ping, rlpx::encodePing()});
    BOOST_CHECK(pong.kind == Kind::Reply);
    BOOST_CHECK_EQUAL(pong.reply.id, rlpx::baseMsg::Pong);
    BOOST_CHECK(server.handle(70, rlpx::Message{rlpx::baseMsg::Pong, rlpx::encodePong()}).kind ==
                Kind::Ignore);
    auto closed =
        server.handle(70, rlpx::Message{rlpx::baseMsg::Disconnect,
                              rlpx::encodeDisconnect({rlpx::DisconnectReason::ClientQuitting})});
    BOOST_CHECK(closed.kind == Kind::PeerClosed);
    BOOST_CHECK_NE(closed.detail.find("reason=8"), std::string::npos);

    auto malformed = server.handle(70, ethFrame(eth::msg::GetBlockHeaders, fromHex("80")));
    BOOST_CHECK(malformed.kind == Kind::Disconnect);
    BOOST_CHECK(malformed.reason == rlpx::DisconnectReason::ProtocolBreach);
    BOOST_CHECK(
        server.handle(70, ethFrame(eth::msg::GetReceipts, fromHex("c0"))).kind == Kind::Disconnect);
    BOOST_CHECK(
        server.handle(70, ethFrame(eth::msg::Status, fromHex("c0"))).kind == Kind::Disconnect);
    // gossip and unknown codes are ignored
    BOOST_CHECK(
        server.handle(70, ethFrame(eth::msg::Transactions, fromHex("c0"))).kind == Kind::Ignore);
    BOOST_CHECK(
        server.handle(70, ethFrame(eth::msg::BlockHeaders, fromHex("c0"))).kind == Kind::Ignore);
    BOOST_CHECK(server.handle(70, ethFrame(0x1f, fromHex("c0"))).kind == Kind::Ignore);
    // BlockRangeUpdate: valid -> ignore, inverted -> breach, on eth/68 -> breach
    eth::BlockRangeUpdateMessage update;
    update.latestBlock = 5;
    update.latestBlockHash = chain[2].hash;
    BOOST_CHECK(
        server.handle(69, ethFrame(eth::msg::BlockRangeUpdate, eth::encodeBlockRangeUpdate(update)))
            .kind == Kind::Ignore);
    update.earliestBlock = 9;
    BOOST_CHECK(
        server.handle(69, ethFrame(eth::msg::BlockRangeUpdate, eth::encodeBlockRangeUpdate(update)))
            .kind == Kind::Disconnect);
    update.earliestBlock = 0;
    BOOST_CHECK(
        server.handle(68, ethFrame(eth::msg::BlockRangeUpdate, eth::encodeBlockRangeUpdate(update)))
            .kind == Kind::Disconnect);
    // GetPooledTransactions -> empty PooledTransactions with the request id
    eth::GetPooledTransactionsMessage pooled;
    pooled.requestId = 77;
    pooled.hashes = {chain[1].hash};
    auto answer = server.handle(
        70, ethFrame(eth::msg::GetPooledTransactions, eth::encodeGetPooledTransactions(pooled)));
    BOOST_REQUIRE(answer.kind == Kind::Reply);
    auto decoded = eth::decodePooledTransactions(ref(answer.reply.data));
    BOOST_REQUIRE(decoded.has_value());
    BOOST_CHECK_EQUAL(decoded->requestId, 77u);
    BOOST_CHECK(decoded->transactions.empty());
}

// Loopback: a real dialer downloads the whole chain through the production serving
// loop, fetches every block's receipts, sees the BlockRangeUpdate after a head advance
// and ends the session with Disconnect.
BOOST_AUTO_TEST_CASE(loopbackServeFullChain)
{
    auto chain = test::makeTestChain(10);
    auto source = std::make_shared<test::InMemoryBlockSource>(chain);
    for (size_t i = 0; i < chain.size(); ++i)
    {
        std::vector<protocol::EthReceiptData> receipts;
        for (size_t k = 0; k < chain[i].transactions.size(); ++k)
        {
            receipts.push_back(receipt(0, 21000 * (k + 1)));
        }
        source->setReceipts(i, receipts);
    }

    rlpx::EccKeyPair serverKey;
    rlpx::PeerConfig serverConfig;
    serverConfig.clientId = "serving-peer";
    serverConfig.latestBlock = 9;
    serverConfig.latestBlockHash = chain[9].hash;
    rlpx::RlpxServer rlpxServer(serverKey, 0, serverConfig);
    uint16_t const port = rlpxServer.port();

    std::atomic<int> exitReason{-1};
    std::thread serverThread([&] {
        try
        {
            auto established = rlpxServer.accept();
            sync::PeerServer server(*source);
            exitReason = static_cast<int>(server.serve(established, nullptr));
        }
        catch (std::exception const& e)
        {
            BOOST_ERROR(std::string("server: ") + e.what());
        }
    });

    rlpx::PeerConfig clientConfig;
    clientConfig.host = "127.0.0.1";
    clientConfig.port = port;
    clientConfig.peerPublicKey = serverKey.publicKey();
    clientConfig.clientId = "dialer";
    rlpx::EccKeyPair clientKey;
    rlpx::RlpxClient client(clientKey, clientConfig);
    auto established = client.connect();
    BOOST_CHECK_EQUAL(established.peerStatus.protocolVersion, 70u);
    BOOST_CHECK_EQUAL(established.peerStatus.latestBlock, 9u);

    // Headers + bodies through the production downloader (anchor = block 0). Any
    // failure below must still end the session so the server thread can be joined.
    std::vector<sync::Block> downloaded;
    try
    {
        // Hash-anchored (no PoS field validation: the test chain is not Shanghai-shaped
        // except for its last block), one batch for the whole chain.
        sync::BlockExchange exchange(1, chain[0].hash);
        exchange.downloadRange(established.session, chain.size() - 1,
            [&](sync::Block const& block) { downloaded.push_back(block); });
    }
    catch (std::exception const& e)
    {
        BOOST_ERROR(std::string("download: ") + e.what());
    }
    BOOST_CHECK_EQUAL(downloaded.size(), chain.size() - 1);
    for (size_t i = 0; i < std::min(downloaded.size(), chain.size() - 1); ++i)
    {
        BOOST_CHECK(downloaded[i].hash == chain[i + 1].hash);
        BOOST_CHECK(downloaded[i].transactions == chain[i + 1].transactions);
        BOOST_CHECK(downloaded[i].withdrawals == chain[i + 1].withdrawals);
    }

    // Receipts of every block (eth/70 request shape).
    eth::GetReceiptsMessage request;
    request.requestId = 1234;
    for (auto const& block : chain)
    {
        request.hashes.push_back(block.hash);
    }
    established.session.sendMessage(
        ethFrame(eth::msg::GetReceipts, eth::encodeGetReceipts(request, 70)));
    auto receiptsFrame = established.session.recvMessage();
    BOOST_REQUIRE(receiptsFrame.has_value());
    BOOST_REQUIRE_EQUAL(receiptsFrame->id, eth::frameId(eth::msg::Receipts));
    auto receipts = eth::decodeReceipts(ref(receiptsFrame->data), 70);
    BOOST_REQUIRE(receipts.has_value());
    BOOST_CHECK_EQUAL(receipts->requestId, 1234u);
    BOOST_REQUIRE_EQUAL(receipts->receipts.size(), chain.size());
    for (size_t i = 0; i < chain.size(); ++i)
    {
        BOOST_CHECK_EQUAL(receipts->receipts[i].size(), chain[i].transactions.size());
    }

    // Head advance: the next frame the loop handles triggers a BlockRangeUpdate.
    auto longer = test::makeTestChain(11);
    source->append(longer[10]);
    established.session.sendMessage(rlpx::Message{rlpx::baseMsg::Ping, rlpx::encodePing()});
    auto pong = established.session.recvMessage();
    BOOST_REQUIRE(pong.has_value());
    BOOST_CHECK_EQUAL(pong->id, rlpx::baseMsg::Pong);
    auto rangeFrame = established.session.recvMessage();
    BOOST_REQUIRE(rangeFrame.has_value());
    BOOST_REQUIRE_EQUAL(rangeFrame->id, eth::frameId(eth::msg::BlockRangeUpdate));
    auto range = eth::decodeBlockRangeUpdate(ref(rangeFrame->data));
    BOOST_REQUIRE(range.has_value());
    BOOST_CHECK_EQUAL(range->earliestBlock, 0u);
    BOOST_CHECK_EQUAL(range->latestBlock, 10u);
    BOOST_CHECK(range->latestBlockHash == longer[10].hash);

    established.session.sendMessage(rlpx::Message{rlpx::baseMsg::Disconnect,
        rlpx::encodeDisconnect({rlpx::DisconnectReason::ClientQuitting})});
    serverThread.join();
    BOOST_CHECK_EQUAL(
        exitReason.load(), static_cast<int>(sync::PeerServer::ExitReason::PeerDisconnected));
}

BOOST_AUTO_TEST_SUITE_END()
