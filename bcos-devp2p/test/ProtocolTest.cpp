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
    // legacy txs are lists, typed txs are 0xNN||payload. Use complete RLP here.
    body.transactions = {fromHex("c3010203"), fromHex("c101")};
    body.uncles = {fromHex("c0")};
    body.withdrawals = std::vector<bcos::bytes>{fromHex("c101"), fromHex("c20203")};
    msg.bodies.push_back(body);

    auto encoded = eth::encodeBlockBodies(msg);
    auto decoded = eth::decodeBlockBodies(ref(encoded));
    BOOST_CHECK_EQUAL(decoded.requestId, 3);
    BOOST_REQUIRE_EQUAL(decoded.bodies.size(), 1u);
    BOOST_REQUIRE_EQUAL(decoded.bodies[0].transactions.size(), 2u);
    BOOST_CHECK(decoded.bodies[0].transactions[0] == body.transactions[0]);
    BOOST_CHECK(decoded.bodies[0].transactions[1] == body.transactions[1]);
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

// eth/69 (EIP-7642) Status round trip: the 7-field block-range form. This is the
// format this PR advertises as preferred (kProtocolVersion = 69), so the encode
// and decode branches must both be exercised (the eth/68 case above covers only
// the 6-field form).
BOOST_AUTO_TEST_CASE(statusV69RoundTrip)
{
    eth::StatusMessage msg;
    msg.protocolVersion = 69;
    msg.networkId = 11155111;  // Sepolia
    msg.genesisHash = h256(
        std::string_view("0x25a5cc106eea7138acab33231d7160d69cb777ee0c2c553fcddf5138993e6dd9"),
        h256::FromHex);  // Sepolia genesis
    msg.forkId = {0x12345678, 0};
    msg.earliestBlock = 1000;
    msg.latestBlock = 2000;
    msg.latestBlockHash = h256(
        std::string_view("0x2222222222222222222222222222222222222222222222222222222222222222"),
        h256::FromHex);

    auto encoded = eth::encodeStatus(msg);
    auto decoded = eth::decodeStatus(ref(encoded));
    BOOST_CHECK_EQUAL(decoded.protocolVersion, 69);
    BOOST_CHECK_EQUAL(decoded.networkId, 11155111);
    BOOST_CHECK(decoded.genesisHash == msg.genesisHash);
    BOOST_CHECK_EQUAL(decoded.forkId.hash, 0x12345678u);
    BOOST_CHECK_EQUAL(decoded.forkId.next, 0u);
    BOOST_CHECK_EQUAL(decoded.earliestBlock, 1000u);
    BOOST_CHECK_EQUAL(decoded.latestBlock, 2000u);
    BOOST_CHECK(decoded.latestBlockHash == msg.latestBlockHash);
    // On eth/69 headHash is aliased to the latest advertised block hash.
    BOOST_CHECK(decoded.headHash == msg.latestBlockHash);
}

// A 9-byte uint64 field on the hostile-input path must be rejected, not silently
// truncated to its low 8 bytes (geth disconnects with "input string too long").
BOOST_AUTO_TEST_CASE(overwideUintRejected)
{
    // GetBlockHeaders with requestId encoded as a 9-byte integer:
    //   list(requestId=0x89 01*9, list(origin=0x80, amount=0x01, skip=0x80, reverse=0x80))
    bcos::bytes wire = fromHex("cf89010101010101010101c480018080");
    BOOST_CHECK_THROW(eth::decodeGetBlockHeaders(ref(wire)), std::runtime_error);
}

// Decoders are fail-closed: trailing elements after the declared fields must be
// rejected rather than silently discarded (byte-level canonicality).
BOOST_AUTO_TEST_CASE(trailingElementsRejected)
{
    // GetBlockHeaders with an extra trailing element in the outer list:
    //   list(requestId=0x05, list(origin=0x80, amount=0x01, skip=0x80, reverse=0x80), 0x99)
    bcos::bytes wire = fromHex("c705c48001808099");
    BOOST_CHECK_THROW(eth::decodeGetBlockHeaders(ref(wire)), std::runtime_error);
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

// EIP-2124: mainnet genesis + forks → CRC32 chain. Each intermediate checksum is
// anchored against geth's own seed-based checksum (crc32.Update), including the
// EIP's published vectors 0x97c2c34c (Homestead) and 0x668db0af (post-Petersburg).
BOOST_AUTO_TEST_CASE(forkIdMainnetChain)
{
    auto genesis = h256(
        std::string_view("0xd4e56740f876aef8c010b86a40d5f56745a118d0906a34e69aec8c0db1cb8fa3"),
        h256::FromHex);
    // crc32 over the FULL 32-byte genesis hash.
    auto hash = eth::crc32(bytesConstRef(genesis.data(), genesis.size()));
    BOOST_CHECK_EQUAL(hash, 0xfc64ec04u);

    // Mainnet fork points in order, DEDUPLICATED: Constantinople and Petersburg
    // both activate at block 7,280,000, and EIP-2124 chains a same-block fork ONCE.
    // Shanghai/Cancun are TIMESTAMP-activated — EIP-2124 chains the timestamp
    // (1681338455 / 1710338135), not a block number.
    struct ForkPoint
    {
        uint64_t value;
        uint32_t expectedAfter;  // geth/zlib-anchored intermediate checksum
    };
    std::vector<ForkPoint> forks = {
        {1150000, 0x97c2c34c},    // Homestead
        {1920000, 0x91d1f948},    // DAO
        {2463000, 0x7a64da13},    // Tangerine
        {2675000, 0x3edd5b10},    // Spurious
        {4370000, 0xa00bc324},    // Byzantium
        {7280000, 0x668db0af},    // Constantinople+Petersburg (deduped) — EIP vector
        {9069000, 0x879d6e30},    // Istanbul
        {9200000, 0xe029e991},    // Muir Glacier
        {12244000, 0x0eb440f6},   // Berlin
        {12965000, 0xb715077d},   // London
        {13773000, 0x20c327fc},   // Arrow Glacier
        {15050000, 0xf0afd0e3},   // Gray Glacier
        {15537394, 0xbfe70a5e},   // Paris (merge)
        {1681338455, 0xa701b4c9}, // Shanghai (timestamp)
        {1710338135, 0x749e0cdc}, // Cancun (timestamp)
    };
    for (auto const& fork : forks)
    {
        hash = eth::forkIdAddForkPoint(hash, fork.value);
        BOOST_CHECK_EQUAL(hash, fork.expectedAfter);
    }
    // Full mainnet forkid after Cancun (deduped + timestamp forks).
    BOOST_CHECK_EQUAL(hash, 0x749e0cdcu);
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
