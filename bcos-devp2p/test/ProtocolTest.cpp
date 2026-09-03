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
 * @file ProtocolTest.cpp
 * @brief eth/68 message codec round trips + golden vectors + EIP-2124 forkid.
 * @date 2026/8/18
 */
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-devp2p/eth/Protocol.h>
#include <bcos-devp2p/rlpx/Messages.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::devp2p;

BOOST_AUTO_TEST_SUITE(ProtocolTest)

// Golden from silkworm's packet_coding_test (eth/66):
//   GetBlockHeaders66 [0x6b1a456ba6e2f81d, [0xb9ffff, 1, 0, 0]]
BOOST_AUTO_TEST_CASE(getBlockHeadersGolden)
{
    eth::GetBlockHeadersMessage msg;
    msg.requestId = 0x6b1a456ba6e2f81dull;
    msg.originNumber = 0xb9ffff;
    msg.amount = 1;
    msg.skip = 0;
    msg.reverse = false;

    auto encoded = eth::encodeGetBlockHeaders(msg);
    // The trailing byte is the `reverse` flag: a BOOLEAN on the wire. ethrex's
    // bool decoder only accepts 0x80(false)/0x01(true) (RLP_NULL = false) and
    // rejects geth's 0x00 encoding; geth's decoder reads the field as an integer
    // and accepts both. Use 0x80/0x01 for cross-client compatibility.
    BOOST_CHECK_EQUAL(toHex(encoded), "d1886b1a456ba6e2f81dc783b9ffff018080");

    auto decoded = eth::decodeGetBlockHeaders(ref(encoded));
    BOOST_CHECK_EQUAL(decoded.requestId, msg.requestId);
    BOOST_CHECK(!decoded.originHash.has_value());
    BOOST_CHECK_EQUAL(decoded.originNumber, msg.originNumber);
    BOOST_CHECK_EQUAL(decoded.amount, msg.amount);
    BOOST_CHECK_EQUAL(decoded.skip, msg.skip);
    BOOST_CHECK_EQUAL(decoded.reverse, false);
}

BOOST_AUTO_TEST_CASE(getBlockHeadersByHashRoundTrip)
{
    eth::GetBlockHeadersMessage msg;
    msg.requestId = 7;
    msg.originHash = h256(
        std::string_view("0x00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"),
        h256::FromHex);
    msg.amount = 128;
    msg.skip = 1;
    msg.reverse = true;

    auto encoded = eth::encodeGetBlockHeaders(msg);
    auto decoded = eth::decodeGetBlockHeaders(ref(encoded));
    BOOST_CHECK_EQUAL(decoded.requestId, 7);
    BOOST_REQUIRE(decoded.originHash.has_value());
    BOOST_CHECK(*decoded.originHash == *msg.originHash);
    BOOST_CHECK_EQUAL(decoded.amount, 128);
    BOOST_CHECK_EQUAL(decoded.skip, 1);
    BOOST_CHECK_EQUAL(decoded.reverse, true);
}

BOOST_AUTO_TEST_CASE(blockHeadersRoundTrip)
{
    eth::BlockHeadersMessage msg;
    msg.requestId = 9;
    // Headers on the wire are already-encoded RLP elements (lists) — they must
    // NOT be re-wrapped as strings. Use small but COMPLETE RLP lists here.
    msg.headers = {fromHex("c0"), fromHex("c101")};

    auto encoded = eth::encodeBlockHeaders(msg);
    auto decoded = eth::decodeBlockHeaders(ref(encoded));
    BOOST_CHECK_EQUAL(decoded.requestId, 9);
    BOOST_REQUIRE_EQUAL(decoded.headers.size(), 2u);
    BOOST_CHECK(decoded.headers[0] == msg.headers[0]);
    BOOST_CHECK(decoded.headers[1] == msg.headers[1]);
}

BOOST_AUTO_TEST_CASE(blockBodiesRoundTrip)
{
    eth::BlockBodiesMessage msg;
    msg.requestId = 3;
    eth::BlockBody body;
    // Transactions / uncles / withdrawals are already-encoded RLP elements;
    // legacy txs are lists, typed txs are 0xNN||payload (unwrapped form; the
    // encoder applies the wire string wrapping). Use complete RLP here.
    body.transactions = {fromHex("c3010203"), fromHex("c101"),
        fromHex("02c3010203")};
    body.uncles = {fromHex("c0")};
    body.withdrawals = std::vector<bcos::bytes>{fromHex("c101"), fromHex("c20203")};
    msg.bodies.push_back(body);

    auto encoded = eth::encodeBlockBodies(msg);
    auto decoded = eth::decodeBlockBodies(ref(encoded));
    BOOST_CHECK_EQUAL(decoded.requestId, 3);
    BOOST_REQUIRE_EQUAL(decoded.bodies.size(), 1u);
    BOOST_REQUIRE_EQUAL(decoded.bodies[0].transactions.size(), 3u);
    BOOST_CHECK(decoded.bodies[0].transactions[0] == body.transactions[0]);
    BOOST_CHECK(decoded.bodies[0].transactions[1] == body.transactions[1]);
    BOOST_CHECK(decoded.bodies[0].transactions[2] == body.transactions[2]);
    BOOST_REQUIRE_EQUAL(decoded.bodies[0].uncles.size(), 1u);
    BOOST_CHECK(decoded.bodies[0].uncles[0] == body.uncles[0]);
}

// On the wire a typed transaction is RLP-encoded as a STRING whose content is
// 0xNN || rlp(payload) (legacy txs are RLP lists). decodeBlockBodies/takeTx must
// strip the string prefix so the decoder receives exactly 0xNN||payload.
BOOST_AUTO_TEST_CASE(blockBodiesTypedTxStringWrapped)
{
    // Real Sepolia block 33106 EIP-1559 (type 0x02) transfer: 0x02f877... .
    auto typedTx = fromHex(
        "02f87783aa36a7808459682f00851f71a335b5825208942f14582947e292a2ecd20c430b46f2"
        "d27cfe213c8901a055690d9db8000080c080a06dd8b58b520530663fa1ce8bbbc3eb9b2e0b79"
        "70138d781b9d9380e3dbf1f362a0098934613bfad8e42b3d93b26b450d1028c2288e212c8160"
        "5a54d886028c5746");
    // The same bytes as geth would encode them in a BlockBodies list: an RLP
    // STRING wrapping the typed tx (string prefix + 0x02f877...).
    bcos::bytes wireTypedTx;
    bcos::codec::rlp::encode(wireTypedTx,
        bcos::bytesConstRef(typedTx.data(), typedTx.size()));

    // Build the wire message by hand (decode path only): requestId, one body
    // with transactions [string-wrapped typed tx, legacy list tx], uncles [].
    auto txsList = [&]() {
        bcos::bytes out;
        bcos::codec::rlp::encodeHeader(out, {.isList = true,
            .payloadLength = wireTypedTx.size() + 4 /* 0xc3 01 02 03 */});
        out.insert(out.end(), wireTypedTx.begin(), wireTypedTx.end());
        out.insert(out.end(), {0xc3, 0x01, 0x02, 0x03});
        return out;
    }();
    auto unclesList = []() {
        bcos::bytes out;
        bcos::codec::rlp::encodeHeader(out, {.isList = true, .payloadLength = 1});
        out.push_back(0xc0);
        return out;
    }();
    auto bodyList = [&]() {
        bcos::bytes out;
        bcos::codec::rlp::encodeHeader(out,
            {.isList = true, .payloadLength = txsList.size() + unclesList.size()});
        out.insert(out.end(), txsList.begin(), txsList.end());
        out.insert(out.end(), unclesList.begin(), unclesList.end());
        return out;
    }();
    bcos::bytes bodyItems;
    bodyItems.insert(bodyItems.end(), bodyList.begin(), bodyList.end());
    auto bodiesList = [&]() {
        bcos::bytes out;
        bcos::codec::rlp::encodeHeader(out, {.isList = true, .payloadLength = bodyItems.size()});
        out.insert(out.end(), bodyItems.begin(), bodyItems.end());
        return out;
    }();
    auto wire = [&]() {
        bcos::bytes out;
        bcos::codec::rlp::encodeHeader(out,
            {.isList = true, .payloadLength = 1 + bodiesList.size()});
        out.push_back(0x07);  // requestId
        out.insert(out.end(), bodiesList.begin(), bodiesList.end());
        return out;
    }();

    auto decoded = eth::decodeBlockBodies(ref(wire));
    BOOST_CHECK_EQUAL(decoded.requestId, 7);
    BOOST_REQUIRE_EQUAL(decoded.bodies.size(), 1u);
    BOOST_REQUIRE_EQUAL(decoded.bodies[0].transactions.size(), 2u);
    // typed tx must come back WITHOUT the string prefix (0xNN||payload only)
    BOOST_CHECK(decoded.bodies[0].transactions[0] == typedTx);
    BOOST_CHECK(decoded.bodies[0].transactions[1] ==
                (bcos::bytes{0xc3, 0x01, 0x02, 0x03}));

    // The ENCODE path must produce exactly this hand-built wire form: typed txs
    // string-wrapped, legacy list txs spliced bare.
    eth::BlockBodiesMessage msg;
    msg.requestId = 7;
    eth::BlockBody body;
    body.transactions = {typedTx, bcos::bytes{0xc3, 0x01, 0x02, 0x03}};
    body.uncles = {bcos::bytes{0xc0}};
    msg.bodies.push_back(body);
    BOOST_CHECK(eth::encodeBlockBodies(msg) == wire);
}

BOOST_AUTO_TEST_CASE(statusRoundTrip)
{
    eth::StatusMessage msg;
    msg.protocolVersion = 68;
    msg.networkId = 11155111;  // Sepolia
    msg.totalDifficulty = fromHex("0102030405060708");
    msg.headHash = h256(
        std::string_view("0x1111111111111111111111111111111111111111111111111111111111111111"),
        h256::FromHex);
    msg.genesisHash = h256(
        std::string_view("0x25a5cc106eea7138acab33231d7160d69cb777ee0c2c553fcddf5138993e6dd9"),
        h256::FromHex);  // Sepolia genesis
    msg.forkId = {0x12345678, 0};

    auto encoded = eth::encodeStatus(msg);
    auto decoded = eth::decodeStatus(ref(encoded));
    BOOST_CHECK_EQUAL(decoded.protocolVersion, 68);
    BOOST_CHECK_EQUAL(decoded.networkId, 11155111);
    BOOST_CHECK(decoded.totalDifficulty == msg.totalDifficulty);
    BOOST_CHECK(decoded.headHash == msg.headHash);
    BOOST_CHECK(decoded.genesisHash == msg.genesisHash);
    BOOST_CHECK_EQUAL(decoded.forkId.hash, 0x12345678u);
    BOOST_CHECK_EQUAL(decoded.forkId.next, 0u);
}

BOOST_AUTO_TEST_CASE(newBlockHashesRoundTrip)
{
    eth::NewBlockHashesMessage msg;
    msg.entries = {
        {h256(std::string_view("0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
             h256::FromHex),
            1},
        {h256(std::string_view("0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
             h256::FromHex),
            2},
    };
    auto encoded = eth::encodeNewBlockHashes(msg);
    auto decoded = eth::decodeNewBlockHashes(ref(encoded));
    BOOST_REQUIRE_EQUAL(decoded.entries.size(), 2u);
    BOOST_CHECK(decoded.entries[0].hash == msg.entries[0].hash);
    BOOST_CHECK_EQUAL(decoded.entries[0].number, 1u);
    BOOST_CHECK(decoded.entries[1].hash == msg.entries[1].hash);
    BOOST_CHECK_EQUAL(decoded.entries[1].number, 2u);
}

// EIP-2124: mainnet genesis + forks → CRC32 chain. Vectors are the real
// network values (EIP-2124 worked example / geth forkid testdata).
BOOST_AUTO_TEST_CASE(forkIdMainnetChain)
{
    auto genesis = h256(
        std::string_view("0xd4e56740f876aef8c010b86a40d5f56745a118d0906a34e69aec8c0db1cb8fa3"),
        h256::FromHex);
    // crc32 over the FULL 32-byte genesis hash.
    auto hash = eth::crc32(bytesConstRef(genesis.data(), genesis.size()));
    BOOST_CHECK_EQUAL(hash, 0xfc64ec04u);

    // genesis + forks up to Petersburg (EIP-2124 worked example). Constantinople
    // and Petersburg share block 7280000 and are ONE forkid point.
    std::vector<uint64_t> forks = {1150000, 1920000, 2463000, 2675000, 4370000, 7280000};
    for (auto fork : forks)
    {
        hash = eth::forkIdAddForkPoint(hash, fork);
    }
    BOOST_CHECK_EQUAL(hash, 0x668db0afu);  // Petersburg

    // Continue with the remaining block-based forks, then the timestamp-based
    // Shanghai (1681338455) and Cancun (1710338135) points. The merge block
    // 15537394 is TTD-triggered and is NOT a forkid point.
    for (auto fork : {9069000ull, 9200000ull, 12244000ull, 12965000ull, 13773000ull,
             15050000ull, 1681338455ull, 1710338135ull})
    {
        hash = eth::forkIdAddForkPoint(hash, fork);
    }
    BOOST_CHECK_EQUAL(hash, 0x9f3d2254u);  // Cancun (geth forkid testdata)
}

// Hello message golden (RLP built with an independent encoder).
BOOST_AUTO_TEST_CASE(helloGolden)
{
    rlpx::HelloMessage hello;
    hello.version = 5;
    hello.clientId = "test";
    hello.capabilities.push_back({std::string("eth"), 68});
    hello.listenPort = 30303;
    hello.id.assign(64, 0x01);

    auto encoded = rlpx::encodeHello(hello);
    // [5, "test", [["eth", 68]], 30303, 64x01]
    std::string expected = "f852058474657374c6c5836574684482765fb840";
    for (int i = 0; i < 64; ++i)
    {
        expected += "01";
    }
    BOOST_CHECK_EQUAL(toHex(encoded), expected);

    auto decoded = rlpx::decodeHello(ref(encoded));
    BOOST_CHECK_EQUAL(decoded.version, 5u);
    BOOST_CHECK_EQUAL(decoded.clientId, "test");
    BOOST_REQUIRE_EQUAL(decoded.capabilities.size(), 1u);
    BOOST_CHECK_EQUAL(decoded.capabilities[0].name, "eth");
    BOOST_CHECK_EQUAL(decoded.capabilities[0].version, 68);
    BOOST_CHECK_EQUAL(decoded.listenPort, 30303u);
    BOOST_CHECK(decoded.id == hello.id);
}

BOOST_AUTO_TEST_CASE(disconnectRoundTrip)
{
    rlpx::DisconnectMessage msg;
    msg.reason = rlpx::DisconnectReason::UselessPeer;
    auto encoded = rlpx::encodeDisconnect(msg);
    auto decoded = rlpx::decodeDisconnect(ref(encoded));
    BOOST_CHECK(decoded.reason == rlpx::DisconnectReason::UselessPeer);
}

BOOST_AUTO_TEST_SUITE_END()
