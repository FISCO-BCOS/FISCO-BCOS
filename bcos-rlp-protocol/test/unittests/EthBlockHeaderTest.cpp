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
 * @file EthBlockHeaderTest.cpp
 * @brief Unit tests for EthBlockHeader RLP encode/decode/hash
 * @date 2026/8/5
 */

#include "bcos-rlp-protocol/EthBlockHeader.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/tars/Block.h>
#include <boost/test/unit_test.hpp>
#include <memory>

using namespace bcos;
using namespace bcos::protocol;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(EthBlockHeaderTest)

// A helper that builds a Tars header with all Eth-required fields populated
static std::shared_ptr<bcostars::BlockHeader> makeEthTarsHeader()
{
    auto tars = std::make_shared<bcostars::BlockHeader>();
    auto& data = tars->data;
    data.blockNumber = 77;
    data.timestamp = 1700000000;
    data.gasLimit = "30000000";
    data.gasUsed = "21000";
    data.baseFee = "1000000000";
    data.coinbase.assign(20, static_cast<char>(0xab));
    data.stateRoot.assign(32, static_cast<char>(0x11));
    data.txsRoot.assign(32, static_cast<char>(0x22));
    data.receiptRoot.assign(32, static_cast<char>(0x33));
    data.prevRandao.assign(32, static_cast<char>(0x44));
    data.logsBloom.assign(256, static_cast<char>(0xcd));
    bcostars::ParentInfo parentInfo;
    parentInfo.blockNumber = 76;
    parentInfo.blockHash.assign(32, static_cast<char>(0x55));
    data.parentInfo.push_back(parentInfo);
    return tars;
}

// Tars header -> EthBlockHeader -> RLP -> toTarsHeader (base-class Ptr)
BOOST_AUTO_TEST_CASE(rlpEncodeDecodeRoundTrip)
{
    auto tars = makeEthTarsHeader();

    // Tars -> Eth
    EthBlockHeader ethHeader(*tars);
    BOOST_CHECK_EQUAL(ethHeader.data().number, 77);
    BOOST_CHECK_EQUAL(ethHeader.data().timestamp, 1700000000);
    BOOST_CHECK_EQUAL(ethHeader.data().gasLimit, u256(30000000));
    BOOST_CHECK_EQUAL(ethHeader.data().gasUsed, u256(21000));
    BOOST_CHECK(ethHeader.data().baseFee.has_value());
    BOOST_CHECK_EQUAL(*ethHeader.data().baseFee, u256(1000000000));

    // RLP encode
    bytes rlp;
    ethHeader.rlpEncode(rlp);
    BOOST_CHECK(!rlp.empty());

    // Static: decode RLP -> EthBlockHeader
    bcos::Error::UniquePtr ethError;
    auto decodedEth = EthBlockHeader::toEthBlockHeader(bcos::ref(rlp), ethError);
    BOOST_CHECK(!ethError);
    BOOST_CHECK_EQUAL(decodedEth.data().number, 77);
    BOOST_CHECK_EQUAL(decodedEth.data().timestamp, 1700000000);
    BOOST_CHECK_EQUAL(decodedEth.data().gasLimit, u256(30000000));
    BOOST_CHECK_EQUAL(decodedEth.data().gasUsed, u256(21000));
    BOOST_CHECK(decodedEth.data().baseFee.has_value());
    BOOST_CHECK_EQUAL(*decodedEth.data().baseFee, u256(1000000000));
    BOOST_CHECK(decodedEth.data().coinbase == ethHeader.data().coinbase);
    BOOST_CHECK(decodedEth.data().stateRoot == ethHeader.data().stateRoot);
    BOOST_CHECK(decodedEth.data().txsRoot == ethHeader.data().txsRoot);
    BOOST_CHECK(decodedEth.data().receiptsRoot == ethHeader.data().receiptsRoot);

    // Static: decode RLP -> Tars header (base-class Ptr)
    auto tarsPtr = EthBlockHeader::toTarsHeader(bcos::ref(rlp), ethError);
    BOOST_CHECK(tarsPtr != nullptr);
    BOOST_CHECK(!ethError);
    auto impl = std::dynamic_pointer_cast<bcostars::protocol::BlockHeaderImpl>(tarsPtr);
    BOOST_CHECK(impl != nullptr);
    BOOST_CHECK_EQUAL(impl->number(), 77);
    BOOST_CHECK_EQUAL(impl->timestamp(), 1700000000);
    BOOST_CHECK_EQUAL(impl->gasLimit(), u256(30000000));
    BOOST_CHECK_EQUAL(impl->gasUsed(), u256(21000));
    // The converted header must be marked as an Eth header
    BOOST_CHECK(impl->isEthBlockHeader());
}

// toTarsHeader must inject keccak256 of the canonical re-encoding, not of the raw input:
// trailing data after the header RLP list must not pollute the hash.
BOOST_AUTO_TEST_CASE(toTarsHeaderIgnoresTrailingData)
{
    auto tars = makeEthTarsHeader();
    EthBlockHeader ethHeader(*tars);

    bytes rlp;
    ethHeader.rlpEncode(rlp);

    // Append garbage after the header list; rlpDecode only consumes the header list.
    bytes withTrailing = rlp;
    withTrailing.push_back(static_cast<byte>(0xde));
    withTrailing.push_back(static_cast<byte>(0xad));

    bcos::Error::UniquePtr error;
    auto tarsPtr = EthBlockHeader::toTarsHeader(bcos::ref(withTrailing), error);
    BOOST_CHECK(!error);
    BOOST_CHECK(tarsPtr != nullptr);
    auto impl = std::dynamic_pointer_cast<bcostars::protocol::BlockHeaderImpl>(tarsPtr);
    BOOST_CHECK(impl != nullptr);

    // The injected hash equals keccak256(rlp(header)) — not keccak256(withTrailing).
    bytes canonicalRlp;
    ethHeader.rlpEncode(canonicalRlp);
    auto expected = bcos::crypto::keccak256Hash(bcos::ref(canonicalRlp));
    BOOST_CHECK(impl->hash() == expected);
    BOOST_CHECK(impl->hash() != bcos::crypto::keccak256Hash(bcos::ref(withTrailing)));
}

// calculateRLPHash injects the RLP hash into a BlockHeaderImpl
BOOST_AUTO_TEST_CASE(calculateRLPHashInjectsHash)
{
    auto tars = makeEthTarsHeader();
    auto header = std::make_shared<bcostars::protocol::BlockHeaderImpl>(tars);

    bcos::Error::UniquePtr error;
    bcos::protocol::EthBlockHeader::calculateRLPHash(header, error);
    BOOST_CHECK(!error);

    // Expected: keccak256 of the RLP encoding
    bcos::protocol::EthBlockHeader ethHeader(*tars);
    bytes rlp;
    ethHeader.rlpEncode(rlp);
    auto expected = bcos::crypto::keccak256Hash(bcos::ref(rlp));

    BOOST_CHECK(header->hash() == expected);
}

// RLP encoding equals keccak256 of the RLP encoding (the hash formula)
BOOST_AUTO_TEST_CASE(rlpHashFormula)
{
    auto tars = makeEthTarsHeader();
    tars->data.blockNumber = 5;
    tars->data.timestamp = 1000;

    EthBlockHeader ethHeader(*tars);

    bytes rlp;
    ethHeader.rlpEncode(rlp);
    auto expected = crypto::keccak256Hash(bcos::ref(rlp));

    // calculateRLPHash injects exactly keccak256(rlp(header))
    auto header = std::make_shared<bcostars::protocol::BlockHeaderImpl>(tars);
    bcos::Error::UniquePtr error;
    bcos::protocol::EthBlockHeader::calculateRLPHash(header, error);
    BOOST_CHECK(!error);
    BOOST_CHECK(header->hash() == expected);
}

// Fixed golden vector: the exact RLP bytes and keccak256 hash for the header built by
// makeEthTarsHeader() (a London-shaped 16-item header). These are pre-computed constants
// independently verified against a third-party RLP/keccak implementation — if the field
// order, the ommers constant, or the nonce width ever change, this assertion fails even
// though the self-consistent round-trip tests still pass.
BOOST_AUTO_TEST_CASE(goldenEncodingAndHash)
{
    auto tars = makeEthTarsHeader();
    EthBlockHeader ethHeader(*tars);

    bytes rlp;
    ethHeader.rlpEncode(rlp);
    auto hash = bcos::crypto::keccak256Hash(bcos::ref(rlp));

    BOOST_CHECK_EQUAL(bcos::toHex(rlp),
        "f901fca055555555555555555555555555555555555555555555555555555555"
        "55555555a01dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142"
        "fd40d4934794ababababababababababababababababababababa01111111111"
        "111111111111111111111111111111111111111111111111111111a022222222"
        "22222222222222222222222222222222222222222222222222222222a0333333"
        "3333333333333333333333333333333333333333333333333333333333b90100"
        "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
        "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
        "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
        "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
        "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
        "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
        "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
        "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
        "804d8401c9c380825208846553f10080a0444444444444444444444444444444"
        "4444444444444444444444444444444444880000000000000000843b9aca00");
    BOOST_CHECK_EQUAL(
        hash.hex(), "34931f02b946fb93c2e7c725af49b6a61b707eaf4b29c01dee0999d303d16771");
}

// Prague-shaped golden vector: all five optional fork fields present, so the RLP list
// carries 21 items (through requestsHash). Exercises the full cascade that the London
// golden above never reaches. The encoding must round-trip all optional fields losslessly.
BOOST_AUTO_TEST_CASE(goldenPragueEncoding)
{
    auto tars = makeEthTarsHeader();
    auto& data = tars->data;
    data.withdrawalsHash.assign(32, static_cast<char>(0x61));
    data.blobGasUsed = "1";
    data.excessBlobGas = "2";
    data.parentBeaconRoot.assign(32, static_cast<char>(0x62));
    data.requestsHash.assign(32, static_cast<char>(0x63));

    EthBlockHeader ethHeader(*tars);
    bytes rlp;
    ethHeader.rlpEncode(rlp);

    // Decode back and check every optional field survives the round-trip.
    bcos::Error::UniquePtr error;
    auto decoded = EthBlockHeader::toEthBlockHeader(bcos::ref(rlp), error);
    BOOST_CHECK(!error);
    BOOST_CHECK(decoded.data().baseFee.has_value());
    BOOST_CHECK_EQUAL(*decoded.data().baseFee, u256(1000000000));
    BOOST_CHECK(decoded.data().withdrawalsHash.has_value());
    BOOST_CHECK(*decoded.data().withdrawalsHash == ethHeader.data().withdrawalsHash);
    BOOST_CHECK(decoded.data().blobGasUsed.has_value());
    BOOST_CHECK_EQUAL(*decoded.data().blobGasUsed, u256(1));
    BOOST_CHECK(decoded.data().excessBlobGas.has_value());
    BOOST_CHECK_EQUAL(*decoded.data().excessBlobGas, u256(2));
    BOOST_CHECK(decoded.data().parentBeaconRoot.has_value());
    BOOST_CHECK(*decoded.data().parentBeaconRoot == ethHeader.data().parentBeaconRoot);
    BOOST_CHECK(decoded.data().requestsHash.has_value());
    BOOST_CHECK(*decoded.data().requestsHash == ethHeader.data().requestsHash);

    // Re-encoding the decoded header must reproduce the same bytes (canonical round-trip).
    bytes rlp2;
    decoded.rlpEncode(rlp2);
    BOOST_CHECK(rlp == rlp2);

    // Golden vector: the exact RLP bytes and keccak256 hash for this Prague-shaped header.
    // Independently verified against a third-party RLP/keccak implementation.
    BOOST_CHECK_EQUAL(bcos::toHex(rlp),
        "f90261a055555555555555555555555555555555555555555555555555555555"
        "55555555a01dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142"
        "fd40d4934794ababababababababababababababababababababa01111111111"
        "111111111111111111111111111111111111111111111111111111a022222222"
        "22222222222222222222222222222222222222222222222222222222a0333333"
        "3333333333333333333333333333333333333333333333333333333333b90100"
        "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
        "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
        "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
        "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
        "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
        "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
        "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
        "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
        "804d8401c9c380825208846553f10080a0444444444444444444444444444444"
        "4444444444444444444444444444444444880000000000000000843b9aca00a0"
        "6161616161616161616161616161616161616161616161616161616161616161"
        "0102a06262626262626262626262626262626262626262626262626262626262"
        "626262a063636363636363636363636363636363636363636363636363636363"
        "63636363");
    BOOST_CHECK_EQUAL(bcos::crypto::keccak256Hash(bcos::ref(rlp)).hex(),
        "3936b0bfd43eb01433042f9b967abe35cd97047aabba00437db70704b35fc461");
}


// An incomplete Tars header: the constructor converts defensively (no throw), but
// calculateRLPHash must report an InvalidTarsHeader error.
BOOST_AUTO_TEST_CASE(incompleteTarsHeaderReportsError)
{
    auto tars = std::make_shared<bcostars::BlockHeader>();
    // Empty header: missing all required Eth fields
    // Constructor no longer validates — it converts directly.
    EthBlockHeader ethHeader(*tars);  // should not throw
    BOOST_CHECK_EQUAL(ethHeader.data().number, 0);

    // Partially filled: missing coinbase
    tars = makeEthTarsHeader();
    tars->data.coinbase.clear();

    // calculateRLPHash on an incomplete header must report an error
    auto header = std::make_shared<bcostars::protocol::BlockHeaderImpl>(tars);
    bcos::Error::UniquePtr error;
    bcos::protocol::EthBlockHeader::calculateRLPHash(header, error);
    BOOST_CHECK(error != nullptr);
    BOOST_CHECK_EQUAL(
        error->errorCode(), static_cast<int32_t>(EthBlockHeaderError::InvalidTarsHeader));
}

// A truncated RLP header (fields stop mid-cascade) must decode cleanly instead of throwing
// on an empty optional. This is the 19-item Cancun header missing parentBeaconRoot.
BOOST_AUTO_TEST_CASE(decodeTruncatedCascadeNoCrash)
{
    auto tars = makeEthTarsHeader();
    tars->data.withdrawalsHash.assign(32, static_cast<char>(0x61));
    tars->data.blobGasUsed = "1";
    tars->data.excessBlobGas = "2";
    // parentBeaconRoot / requestsHash intentionally absent

    EthBlockHeader ethHeader(*tars);
    bytes rlp;
    ethHeader.rlpEncode(rlp);

    bcos::Error::UniquePtr error;
    auto decodedEth = EthBlockHeader::toEthBlockHeader(bcos::ref(rlp), error);
    BOOST_CHECK(!error);
    BOOST_CHECK(decodedEth.data().baseFee.has_value());
    BOOST_CHECK(decodedEth.data().withdrawalsHash.has_value());
    BOOST_CHECK(decodedEth.data().blobGasUsed.has_value());
    BOOST_CHECK(decodedEth.data().excessBlobGas.has_value());
    BOOST_CHECK(!decodedEth.data().parentBeaconRoot.has_value());

    // toTarsHeader must not crash and must preserve the present optional fields
    auto tarsPtr = EthBlockHeader::toTarsHeader(bcos::ref(rlp), error);
    BOOST_CHECK(!error);
    BOOST_CHECK(tarsPtr != nullptr);
    auto impl = std::dynamic_pointer_cast<bcostars::protocol::BlockHeaderImpl>(tarsPtr);
    BOOST_CHECK(impl != nullptr);
    BOOST_CHECK(!impl->inner().data.parentBeaconRoot.empty() == false);
}

// validateTarsHeader enforces the cascade-stop invariant: a later optional field cannot be
// present while an earlier one is absent.
BOOST_AUTO_TEST_CASE(validateTarsHeaderRejectsCascadeViolation)
{
    auto tars = makeEthTarsHeader();
    tars->data.baseFee.clear();  // baseFee missing, but withdrawalsHash present
    tars->data.withdrawalsHash.assign(32, static_cast<char>(0x61));

    auto header = std::make_shared<bcostars::protocol::BlockHeaderImpl>(tars);
    bcos::Error::UniquePtr error;
    bcos::protocol::EthBlockHeader::calculateRLPHash(header, error);
    BOOST_CHECK(error != nullptr);
    BOOST_CHECK_EQUAL(
        error->errorCode(), static_cast<int32_t>(EthBlockHeaderError::InvalidTarsHeader));
}

// validateTarsHeader requires exact lengths: an over-long fixed-size field (silently
// truncated by the constructor) must be rejected.
BOOST_AUTO_TEST_CASE(validateTarsHeaderRejectsOverlongFields)
{
    auto tars = makeEthTarsHeader();
    tars->data.stateRoot.assign(64, static_cast<char>(0x11));  // 64 bytes, not 32

    auto header = std::make_shared<bcostars::protocol::BlockHeaderImpl>(tars);
    bcos::Error::UniquePtr error;
    bcos::protocol::EthBlockHeader::calculateRLPHash(header, error);
    BOOST_CHECK(error != nullptr);
}

// validateTarsHeader rejects non-numeric string fields instead of letting the constructor's
// lexical_cast throw.
BOOST_AUTO_TEST_CASE(validateTarsHeaderRejectsNonNumeric)
{
    auto tars = makeEthTarsHeader();
    tars->data.gasLimit = "abc";

    auto header = std::make_shared<bcostars::protocol::BlockHeaderImpl>(tars);
    bcos::Error::UniquePtr error;
    bcos::protocol::EthBlockHeader::calculateRLPHash(header, error);
    BOOST_CHECK(error != nullptr);
    BOOST_CHECK_EQUAL(
        error->errorCode(), static_cast<int32_t>(EthBlockHeaderError::InvalidTarsHeader));
}

// Real mainnet golden vector: Ethereum block #19800000 (Cancun era, 20-item header).
// Fields are the actual on-chain values; the expected hash is the block hash published on
// the chain (0x95d7f597…), independently verified to equal keccak256(rlp(header)). This is
// a true interoperability oracle against geth/op-node — not a self-consistent fixture.
BOOST_AUTO_TEST_CASE(goldenMainnetCancunHeader)
{
    auto tars = std::make_shared<bcostars::BlockHeader>();
    auto& data = tars->data;
    data.blockNumber = 19800000;
    data.timestamp = 1714865051;
    data.gasLimit = "30000000";
    data.gasUsed = "8020412";
    data.baseFee = "5007423601";
    data.blobGasUsed = "786432";
    data.excessBlobGas = "0";
    data.coinbase.assign(20, 0x00);
    bcos::Address coinbase(
        std::string_view("0x95222290dd7278aa3ddd389cc1e1d165cc4bafe5"), bcos::Address::FromHex);
    data.coinbase.assign(coinbase.begin(), coinbase.end());
    data.stateRoot.assign(32, 0x00);
    auto stateRoot = bcos::crypto::HashType(
        std::string_view("0xe08eb6a130d0ab2b301b63ebb512ba47cd55662d3fe403341ea42dc79665613c"),
        bcos::crypto::HashType::FromHex);
    data.stateRoot.assign(stateRoot.begin(), stateRoot.end());
    data.txsRoot.assign(32, 0x00);
    auto txsRoot = bcos::crypto::HashType(
        std::string_view("0xb710f4a7c9917d3e81f0cfdbbe9ea5854db0e745d37194d2342d3ea0585d285f"),
        bcos::crypto::HashType::FromHex);
    data.txsRoot.assign(txsRoot.begin(), txsRoot.end());
    data.receiptRoot.assign(32, 0x00);
    auto receiptsRoot = bcos::crypto::HashType(
        std::string_view("0x27bfbe21bcb21d27ff8f5d442b4e5110080b3df4e1d1f0b6314d77605e9f7e6e"),
        bcos::crypto::HashType::FromHex);
    data.receiptRoot.assign(receiptsRoot.begin(), receiptsRoot.end());
    data.prevRandao.assign(32, 0x00);
    auto mixHash = bcos::h256(
        std::string_view("0xb50774a2180b910c41018b5651e87200c3d10c7b7cd0443b20e346b3f289b66a"),
        bcos::h256::FromHex);
    data.prevRandao.assign(mixHash.begin(), mixHash.end());
    data.withdrawalsHash.assign(32, 0x00);
    auto withdrawalsRoot = bcos::h256(
        std::string_view("0x1a470d20b701c3a7f5272198de31ac4a769ffc4dd0637e0c9718c0adf5c346f5"),
        bcos::h256::FromHex);
    data.withdrawalsHash.assign(withdrawalsRoot.begin(), withdrawalsRoot.end());
    data.parentBeaconRoot.assign(32, 0x00);
    auto beaconRoot = bcos::h256(
        std::string_view("0x81fcf3a2ae3a5543a467906ebc642cc9fc7d18fd094a253ea5349323b87494a2"),
        bcos::h256::FromHex);
    data.parentBeaconRoot.assign(beaconRoot.begin(), beaconRoot.end());
    auto parentHash = bcos::crypto::HashType(
        std::string_view("0x4c0f381da82f7c5232f921308b040f2f51475040db7b1ada1970960872182b44"),
        bcos::crypto::HashType::FromHex);
    data.parentInfo.emplace_back();
    data.parentInfo.front().blockNumber = 19799999;
    data.parentInfo.front().blockHash.assign(parentHash.begin(), parentHash.end());
    // extraData "beaverbuild.org"
    std::string extraDataHex = "6265617665726275696c642e6f7267";
    auto extraDataBytes = bcos::fromHex(extraDataHex);
    data.extraData.assign(extraDataBytes.begin(), extraDataBytes.end());
    // logsBloom: the actual 256-byte on-chain bloom of block #19800000
    auto logsBloomBytes = bcos::fromHex(
        "2723022003b74129109181308d241b0014a9489122d42100820822154c6c5b1881111186300"
        "1a228625c1ae2c81c418b4aa5880fae43b80054b8182a00fa20025002300d4c081aad49cad"
        "24b8830c0b794c5805003427cd4b114154582695007c0c829012631b70205c8441510a3ed5"
        "d020e10390c400e01a8c50495046862206a328e505070a1044001c642c9382106882104810d"
        "c8e208662300dc6e74448e4fb20042748a28244d0cc6e61909842466618640d89d58000a72"
        "6d0821c80140113031176cc24cc810018e35590bfc4e28c0a03e462220504a6654222240a0"
        "c2913bb40510030dd0941c16c3ba0688458528d2c89db43440a20cf410160854c7");
    data.logsBloom.assign(logsBloomBytes.begin(), logsBloomBytes.end());

    EthBlockHeader ethHeader(*tars);
    bytes rlp;
    ethHeader.rlpEncode(rlp);

    // The keccak of our RLP encoding must equal the on-chain block hash of #19800000.
    BOOST_CHECK_EQUAL(bcos::crypto::keccak256Hash(bcos::ref(rlp)).hex(),
        "95d7f597b43f97bb4dcb0f1d9a74f13d6d6236592cd01d122945d04b5a2aabad");

    // Golden RLP bytes: the exact RLP encoding of the real mainnet header, independently
    // verified to hash to the on-chain block hash.
    BOOST_CHECK_EQUAL(bcos::toHex(rlp),
        "f90258a04c0f381da82f7c5232f921308b040f2f51475040db7b1ada19709608"
        "72182b44a01dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142"
        "fd40d493479495222290dd7278aa3ddd389cc1e1d165cc4bafe5a0e08eb6a130"
        "d0ab2b301b63ebb512ba47cd55662d3fe403341ea42dc79665613ca0b710f4a7"
        "c9917d3e81f0cfdbbe9ea5854db0e745d37194d2342d3ea0585d285fa027bfbe"
        "21bcb21d27ff8f5d442b4e5110080b3df4e1d1f0b6314d77605e9f7e6eb90100"
        "2723022003b74129109181308d241b0014a9489122d42100820822154c6c5b18"
        "811111863001a228625c1ae2c81c418b4aa5880fae43b80054b8182a00fa2002"
        "5002300d4c081aad49cad24b8830c0b794c5805003427cd4b114154582695007"
        "c0c829012631b70205c8441510a3ed5d020e10390c400e01a8c5049504686220"
        "6a328e505070a1044001c642c9382106882104810dc8e208662300dc6e74448e"
        "4fb20042748a28244d0cc6e61909842466618640d89d58000a726d0821c80140"
        "113031176cc24cc810018e35590bfc4e28c0a03e462220504a6654222240a0c2"
        "913bb40510030dd0941c16c3ba0688458528d2c89db43440a20cf410160854c7"
        "8084012e1fc08401c9c380837a61bc846636c39b8f6265617665726275696c64"
        "2e6f7267a0b50774a2180b910c41018b5651e87200c3d10c7b7cd0443b20e346"
        "b3f289b66a88000000000000000085012a773871a01a470d20b701c3a7f52721"
        "98de31ac4a769ffc4dd0637e0c9718c0adf5c346f5830c000080a081fcf3a2ae"
        "3a5543a467906ebc642cc9fc7d18fd094a253ea5349323b87494a2");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
