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

// A helper that builds a base-class header (BlockHeaderImpl) with all Eth-required fields
// populated. The test depends on protocol-tars to provide a concrete BlockHeaderImpl; the
// rlp-protocol library itself does not.
static bcos::protocol::BlockHeader::Ptr makeEthHeader(
    EthBlockVersion version = EthBlockVersion::LONDON)
{
    auto tars = std::make_shared<bcostars::BlockHeader>();
    auto& data = tars->data;
    data.blockNumber = 77;
    data.timestamp = 1700000000000LL;  // internal domain: milliseconds (whole seconds)
    data.gasLimit = "30000000";
    data.gasUsed = "21000";
    data.coinbase.assign(20, static_cast<char>(0xab));
    data.uncleHash.assign(32, static_cast<char>(0xee));
    data.stateRoot.assign(32, static_cast<char>(0x11));
    data.txsRoot.assign(32, static_cast<char>(0x22));
    data.receiptRoot.assign(32, static_cast<char>(0x33));
    data.prevRandao.assign(32, static_cast<char>(0x44));
    data.logsBloom.assign(256, static_cast<char>(0xcd));
    data.difficulty = "0";
    data.nonce.assign(8, static_cast<char>(0x00));
    bcostars::ParentInfo parentInfo;
    parentInfo.blockNumber = 76;
    parentInfo.blockHash.assign(32, static_cast<char>(0x55));
    data.parentInfo.push_back(parentInfo);

    auto versionGE = [](EthBlockVersion v, EthBlockVersion minV) {
        return static_cast<uint8_t>(v) >= static_cast<uint8_t>(minV);
    };
    if (versionGE(version, EthBlockVersion::LONDON))
    {
        data.baseFee = "1000000000";
    }
    if (versionGE(version, EthBlockVersion::SHANGHAI))
    {
        data.withdrawalsHash.assign(32, static_cast<char>(0x61));
    }
    if (versionGE(version, EthBlockVersion::CANCUN))
    {
        data.blobGasUsed = "1";
        data.excessBlobGas = "2";
        data.parentBeaconRoot.assign(32, static_cast<char>(0x62));
    }
    if (versionGE(version, EthBlockVersion::PRAGUE))
    {
        data.requestsHash.assign(32, static_cast<char>(0x63));
    }

    auto header = std::make_shared<bcostars::protocol::BlockHeaderImpl>(tars);
    header->setEthBlockVersion(version);
    return header;
}

// header -> EthBlockHeader -> RLP -> toTarsHeader (base-class Ptr)
BOOST_AUTO_TEST_CASE(rlpEncodeDecodeRoundTrip)
{
    auto header = makeEthHeader();

    // header -> Eth
    EthBlockHeader ethHeader(*header);
    BOOST_CHECK_EQUAL(ethHeader.data().number, 77);
    // The codec struct is wire seconds; the internal millisecond domain lives in timestampMs().
    BOOST_CHECK_EQUAL(ethHeader.data().timestamp, 1700000000);
    BOOST_CHECK_EQUAL(ethHeader.timestampMs(), 1700000000000LL);
    BOOST_CHECK_EQUAL(ethHeader.data().gasLimit, u256(30000000));
    BOOST_CHECK_EQUAL(ethHeader.data().gasUsed, u256(21000));
    BOOST_CHECK_EQUAL(ethHeader.data().uncleHash,
        bcos::crypto::HashType(
            std::string_view("0xeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"),
            bcos::crypto::HashType::FromHex));
    BOOST_CHECK(ethHeader.data().baseFee.has_value());
    BOOST_CHECK_EQUAL(*ethHeader.data().baseFee, u256(1000000000));

    // RLP encode
    bytes rlp;
    ethHeader.rlpEncode(rlp);
    BOOST_CHECK(!rlp.empty());

    // Static: decode RLP -> EthBlockHeader
    bcos::Error::UniquePtr ethError;
    EthBlockHeader decodedEth;
    ethError = EthBlockHeader::toEthBlockHeader(decodedEth, bcos::ref(rlp));
    BOOST_CHECK(!ethError);
    BOOST_CHECK_EQUAL(decodedEth.data().number, 77);
    // rlpDecode converts the wire's seconds back into internal milliseconds.
    BOOST_CHECK_EQUAL(decodedEth.data().timestamp, 1700000000);
    BOOST_CHECK_EQUAL(decodedEth.timestampMs(), 1700000000000LL);
    BOOST_CHECK_EQUAL(decodedEth.data().gasLimit, u256(30000000));
    BOOST_CHECK_EQUAL(decodedEth.data().gasUsed, u256(21000));
    BOOST_CHECK(decodedEth.data().baseFee.has_value());
    BOOST_CHECK_EQUAL(*decodedEth.data().baseFee, u256(1000000000));
    BOOST_CHECK(decodedEth.data().coinbase == ethHeader.data().coinbase);
    BOOST_CHECK(decodedEth.data().stateRoot == ethHeader.data().stateRoot);
    BOOST_CHECK(decodedEth.data().txsRoot == ethHeader.data().txsRoot);
    BOOST_CHECK(decodedEth.data().receiptsRoot == ethHeader.data().receiptsRoot);
    BOOST_CHECK(decodedEth.data().uncleHash == ethHeader.data().uncleHash);
    BOOST_CHECK(decodedEth.data().difficulty == ethHeader.data().difficulty);
    BOOST_CHECK(decodedEth.data().nonce == ethHeader.data().nonce);
    BOOST_CHECK(decodedEth.data().extraData == ethHeader.data().extraData);
    BOOST_CHECK(decodedEth.data().prevRandao == ethHeader.data().prevRandao);
    BOOST_CHECK(decodedEth.data().logsBloom == ethHeader.data().logsBloom);
    BOOST_CHECK(decodedEth.version() == EthBlockVersion::LONDON);

    // Canonical round-trip: re-encoding the decoded header must reproduce the same bytes.
    bytes rlpReencoded;
    decodedEth.rlpEncode(rlpReencoded);
    BOOST_CHECK(rlp == rlpReencoded);

    // Static: decode RLP into a caller-provided base-class header
    auto decodedHeader = makeEthHeader();
    ethError = EthBlockHeader::toTarsHeader(decodedHeader, bcos::ref(rlp));
    BOOST_CHECK(!ethError);
    BOOST_CHECK_EQUAL(decodedHeader->number(), 77);
    BOOST_CHECK_EQUAL(decodedHeader->timestamp(), 1700000000000LL);
    BOOST_CHECK_EQUAL(decodedHeader->gasLimit(), u256(30000000));
    BOOST_CHECK_EQUAL(decodedHeader->gasUsed(), u256(21000));
    // The converted header must be marked as an Eth header
    BOOST_CHECK(decodedHeader->ethBlockVersion() != EthBlockVersion::NON_ETH);
}

// toTarsHeader must inject keccak256 of the canonical re-encoding, not of the raw input:
// trailing data after the header RLP list must not pollute the hash.
BOOST_AUTO_TEST_CASE(toTarsHeaderIgnoresTrailingData)
{
    auto header = makeEthHeader();
    EthBlockHeader ethHeader(*header);

    bytes rlp;
    ethHeader.rlpEncode(rlp);

    // Append garbage after the header list; rlpDecode only consumes the header list.
    bytes withTrailing = rlp;
    withTrailing.push_back(static_cast<byte>(0xde));
    withTrailing.push_back(static_cast<byte>(0xad));

    bcos::Error::UniquePtr error;
    auto decodedHeader = makeEthHeader();
    error = EthBlockHeader::toTarsHeader(decodedHeader, bcos::ref(withTrailing));
    BOOST_CHECK(!error);

    // The injected hash equals keccak256(rlp(header)) — not keccak256(withTrailing).
    bytes canonicalRlp;
    ethHeader.rlpEncode(canonicalRlp);
    auto expected = bcos::crypto::keccak256Hash(bcos::ref(canonicalRlp));
    BOOST_CHECK(decodedHeader->hash() == expected);
    BOOST_CHECK(decodedHeader->hash() != bcos::crypto::keccak256Hash(bcos::ref(withTrailing)));
}

// calculateRLPHash injects the RLP hash into the base-class header
BOOST_AUTO_TEST_CASE(calculateRLPHashInjectsHash)
{
    auto header = makeEthHeader();

    bcos::Error::UniquePtr error;
    error = bcos::protocol::EthBlockHeader::calculateRLPHash(*header);
    BOOST_CHECK(!error);

    // Expected: keccak256 of the RLP encoding
    bcos::protocol::EthBlockHeader ethHeader(*header);
    bytes rlp;
    ethHeader.rlpEncode(rlp);
    auto expected = bcos::crypto::keccak256Hash(bcos::ref(rlp));

    BOOST_CHECK(header->hash() == expected);
}

// RLP encoding equals keccak256 of the RLP encoding (the hash formula)
BOOST_AUTO_TEST_CASE(rlpHashFormula)
{
    auto header = makeEthHeader();
    header->setNumber(5);
    header->setTimestamp(1000);

    EthBlockHeader ethHeader(*header);

    bytes rlp;
    ethHeader.rlpEncode(rlp);
    auto expected = crypto::keccak256Hash(bcos::ref(rlp));

    // calculateRLPHash injects exactly keccak256(rlp(header))
    bcos::Error::UniquePtr error;
    error = bcos::protocol::EthBlockHeader::calculateRLPHash(*header);
    BOOST_CHECK(!error);
    BOOST_CHECK(header->hash() == expected);
}

// Prague-shaped golden vector: all five optional fork fields present, so the RLP list
// carries 21 items (through requestsHash). The encoding must round-trip all optional fields
// losslessly.
BOOST_AUTO_TEST_CASE(goldenPragueEncoding)
{
    auto header = makeEthHeader(EthBlockVersion::PRAGUE);

    EthBlockHeader ethHeader(*header);
    bytes rlp;
    ethHeader.rlpEncode(rlp);

    // Decode back and check every optional field survives the round-trip.
    bcos::Error::UniquePtr error;
    EthBlockHeader decoded;
    error = EthBlockHeader::toEthBlockHeader(decoded, bcos::ref(rlp));
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
    BOOST_CHECK(decoded.version() == EthBlockVersion::PRAGUE);

    // Re-encoding the decoded header must reproduce the same bytes (canonical round-trip).
    bytes rlp2;
    decoded.rlpEncode(rlp2);
    BOOST_CHECK(rlp == rlp2);
}

// An incomplete header: the constructor converts defensively (no throw), but
// calculateRLPHash must report an InvalidHeader error.
BOOST_AUTO_TEST_CASE(incompleteHeaderReportsError)
{
    auto header = makeEthHeader();
    auto impl = std::dynamic_pointer_cast<bcostars::protocol::BlockHeaderImpl>(header);
    BOOST_REQUIRE(impl != nullptr);
    impl->inner().data.stateRoot.clear();  // all-zero -> "missing"

    // calculateRLPHash on an incomplete header must report an error
    bcos::Error::UniquePtr error;
    error = bcos::protocol::EthBlockHeader::calculateRLPHash(*header);
    BOOST_CHECK(error != nullptr);
    BOOST_CHECK_EQUAL(error->errorCode(), static_cast<int32_t>(EthBlockHeaderError::InvalidHeader));
}

// A truncated RLP header (fields stop mid-cascade) must decode cleanly instead of throwing
// on an empty optional.
BOOST_AUTO_TEST_CASE(decodeTruncatedCascadeNoCrash)
{
    auto header = makeEthHeader(EthBlockVersion::CANCUN);
    // parentBeaconRoot is present (Cancun), requestsHash absent (not Prague)

    EthBlockHeader ethHeader(*header);
    bytes rlp;
    ethHeader.rlpEncode(rlp);

    bcos::Error::UniquePtr error;
    EthBlockHeader decodedEth;
    error = EthBlockHeader::toEthBlockHeader(decodedEth, bcos::ref(rlp));
    BOOST_CHECK(!error);
    BOOST_CHECK(decodedEth.data().baseFee.has_value());
    BOOST_CHECK(decodedEth.data().withdrawalsHash.has_value());
    BOOST_CHECK(decodedEth.data().blobGasUsed.has_value());
    BOOST_CHECK(decodedEth.data().excessBlobGas.has_value());
    BOOST_CHECK(decodedEth.data().parentBeaconRoot.has_value());
    BOOST_CHECK(!decodedEth.data().requestsHash.has_value());
    BOOST_CHECK(decodedEth.version() == EthBlockVersion::CANCUN);

    // toTarsHeader must not crash and must preserve the present optional fields
    auto decodedHeader = makeEthHeader(EthBlockVersion::CANCUN);
    error = EthBlockHeader::toTarsHeader(decodedHeader, bcos::ref(rlp));
    BOOST_CHECK(!error);
    BOOST_CHECK(decodedHeader->parentBeaconBlockRoot().has_value());
}

// validateHeader enforces the fork-gated invariant: a Cancun header missing one of its
// mandatory Cancun fields must be rejected.
BOOST_AUTO_TEST_CASE(validateHeaderRejectsMissingForkField)
{
    auto header = makeEthHeader(EthBlockVersion::CANCUN);
    auto impl = std::dynamic_pointer_cast<bcostars::protocol::BlockHeaderImpl>(header);
    BOOST_REQUIRE(impl != nullptr);
    impl->inner().data.excessBlobGas.clear();  // Cancun requires excessBlobGas

    bcos::Error::UniquePtr error;
    error = bcos::protocol::EthBlockHeader::calculateRLPHash(*header);
    BOOST_CHECK(error != nullptr);
    BOOST_CHECK_EQUAL(error->errorCode(), static_cast<int32_t>(EthBlockHeaderError::InvalidHeader));
}

// A pre-London (version 1) header must not require fork-gated fields, but must carry the
// mandatory pre-merge fields.
BOOST_AUTO_TEST_CASE(validateHeaderPreLondon)
{
    auto header = makeEthHeader(EthBlockVersion::PRE_LONDON);

    bcos::Error::UniquePtr error;
    error = bcos::protocol::EthBlockHeader::calculateRLPHash(*header);
    BOOST_CHECK(!error);
}

// A Prague header without requestsHash must be rejected (version 5 requires it).
BOOST_AUTO_TEST_CASE(validateHeaderPragueRequiresRequestsHash)
{
    auto header = makeEthHeader(EthBlockVersion::PRAGUE);
    auto impl = std::dynamic_pointer_cast<bcostars::protocol::BlockHeaderImpl>(header);
    BOOST_REQUIRE(impl != nullptr);
    impl->inner().data.requestsHash.clear();

    bcos::Error::UniquePtr error;
    error = bcos::protocol::EthBlockHeader::calculateRLPHash(*header);
    BOOST_CHECK(error != nullptr);
    BOOST_CHECK_EQUAL(error->errorCode(), static_cast<int32_t>(EthBlockHeaderError::InvalidHeader));
}

// A fork-gated field must not be present when the header's version is older than the fork
// that introduced it (the symmetric check to the required-missing direction). A LONDON header
// carrying requestsHash would encode requestsHash into the withdrawalsRoot slot for geth.
BOOST_AUTO_TEST_CASE(validateHeaderRejectsUnexpectedForkField)
{
    auto header = makeEthHeader(EthBlockVersion::LONDON);
    auto impl = std::dynamic_pointer_cast<bcostars::protocol::BlockHeaderImpl>(header);
    BOOST_REQUIRE(impl != nullptr);
    impl->inner().data.requestsHash.assign(32, static_cast<char>(0x63));

    bcos::Error::UniquePtr error;
    error = bcos::protocol::EthBlockHeader::calculateRLPHash(*header);
    BOOST_CHECK(error != nullptr);
    BOOST_CHECK_EQUAL(error->errorCode(), static_cast<int32_t>(EthBlockHeaderError::InvalidHeader));
}

// A wire-supplied EthBlockVersion above the known fork range must be rejected.
BOOST_AUTO_TEST_CASE(validateHeaderRejectsUnknownVersion)
{
    auto header = makeEthHeader(EthBlockVersion::LONDON);
    auto impl = std::dynamic_pointer_cast<bcostars::protocol::BlockHeaderImpl>(header);
    BOOST_REQUIRE(impl != nullptr);
    impl->inner().ethBlockVersion = static_cast<tars::Char>(200);  // not a named EthBlockVersion

    bcos::Error::UniquePtr error;
    error = bcos::protocol::EthBlockHeader::calculateRLPHash(*header);
    BOOST_CHECK(error != nullptr);
    BOOST_CHECK_EQUAL(error->errorCode(), static_cast<int32_t>(EthBlockHeaderError::InvalidHeader));
}

// Each fork version must round-trip to its own derived version (encode -> decode -> version).
BOOST_AUTO_TEST_CASE(versionDerivationRoundTrip)
{
    for (auto v : {EthBlockVersion::PRE_LONDON, EthBlockVersion::LONDON, EthBlockVersion::SHANGHAI,
             EthBlockVersion::CANCUN, EthBlockVersion::PRAGUE})
    {
        auto header = makeEthHeader(v);
        EthBlockHeader ethHeader(*header);
        bytes rlp;
        ethHeader.rlpEncode(rlp);

        bcos::Error::UniquePtr error;
        EthBlockHeader decoded;
        error = EthBlockHeader::toEthBlockHeader(decoded, bcos::ref(rlp));
        BOOST_CHECK(!error);
        BOOST_CHECK_EQUAL(static_cast<uint8_t>(decoded.version()), static_cast<uint8_t>(v));
    }
}

// Negative number/timestamp must be rejected by validateHeader (they would otherwise wrap
// into a huge u64 on RLP encode).
BOOST_AUTO_TEST_CASE(validateHeaderRejectsNegativeScalars)
{
    auto header = makeEthHeader();
    auto impl = std::dynamic_pointer_cast<bcostars::protocol::BlockHeaderImpl>(header);
    BOOST_REQUIRE(impl != nullptr);
    impl->inner().data.blockNumber = -5;

    bcos::Error::UniquePtr error;
    error = bcos::protocol::EthBlockHeader::calculateRLPHash(*header);
    BOOST_CHECK(error != nullptr);

    // timestamp negative
    auto header2 = makeEthHeader();
    auto impl2 = std::dynamic_pointer_cast<bcostars::protocol::BlockHeaderImpl>(header2);
    BOOST_REQUIRE(impl2 != nullptr);
    impl2->inner().data.timestamp = -1;
    error = bcos::protocol::EthBlockHeader::calculateRLPHash(*header2);
    BOOST_CHECK(error != nullptr);
}

// A wire-supplied sub-second millisecond timestamp on an otherwise-valid ETH-version header
// must be rejected by validateHeader on the calculateRLPHash path (Error return, not the
// rlpEncode throw), keeping BlockHeaderImpl::calculateHash's clear-on-failure promise intact.
BOOST_AUTO_TEST_CASE(validateHeaderRejectsSubSecondTimestamp)
{
    auto header = makeEthHeader();
    auto impl = std::dynamic_pointer_cast<bcostars::protocol::BlockHeaderImpl>(header);
    BOOST_REQUIRE(impl != nullptr);
    impl->inner().data.timestamp = 1700000000001LL;  // ms not divisible by 1000

    bcos::Error::UniquePtr error;
    BOOST_CHECK_NO_THROW(error = bcos::protocol::EthBlockHeader::calculateRLPHash(*header));
    BOOST_REQUIRE(error != nullptr);
    BOOST_CHECK_EQUAL(error->errorCode(), static_cast<int32_t>(EthBlockHeaderError::InvalidHeader));
}

// Real mainnet golden vector: Ethereum block #19800000 (Cancun era, 20-item header).
// Fields are the actual on-chain values; the expected hash is the block hash published on
// the chain (0x95d7f597…), independently verified to equal keccak256(rlp(header)). This is
// a true interoperability oracle against geth/op-node — not a self-consistent fixture.
BOOST_AUTO_TEST_CASE(goldenMainnetCancunHeader)
{
    auto header = makeEthHeader(EthBlockVersion::CANCUN);
    auto impl = std::dynamic_pointer_cast<bcostars::protocol::BlockHeaderImpl>(header);
    BOOST_REQUIRE(impl != nullptr);
    auto& data = impl->inner().data;
    data.blockNumber = 19800000;
    // On-chain seconds 1714865051, stored in the internal millisecond domain; the RLP
    // bridge divides back to seconds, so the golden hash/bytes below stay byte-identical.
    data.timestamp = 1714865051000LL;
    data.gasLimit = "30000000";
    data.gasUsed = "8020412";
    data.baseFee = "5007423601";
    data.blobGasUsed = "786432";
    data.excessBlobGas = "0";
    // Pre-merge fields: post-merge mainnet blocks carry the empty-ommers hash, difficulty 0,
    // and nonce 0.
    data.uncleHash.assign(32, 0x00);
    auto uncleHash = bcos::crypto::HashType(
        std::string_view("0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347"),
        bcos::crypto::HashType::FromHex);
    data.uncleHash.assign(uncleHash.begin(), uncleHash.end());
    data.difficulty = "0";
    data.nonce.assign(8, static_cast<char>(0x00));
    bcos::Address coinbase(
        std::string_view("0x95222290dd7278aa3ddd389cc1e1d165cc4bafe5"), bcos::Address::FromHex);
    data.coinbase.assign(coinbase.begin(), coinbase.end());
    auto stateRoot = bcos::crypto::HashType(
        std::string_view("0xe08eb6a130d0ab2b301b63ebb512ba47cd55662d3fe403341ea42dc79665613c"),
        bcos::crypto::HashType::FromHex);
    data.stateRoot.assign(stateRoot.begin(), stateRoot.end());
    auto txsRoot = bcos::crypto::HashType(
        std::string_view("0xb710f4a7c9917d3e81f0cfdbbe9ea5854db0e745d37194d2342d3ea0585d285f"),
        bcos::crypto::HashType::FromHex);
    data.txsRoot.assign(txsRoot.begin(), txsRoot.end());
    auto receiptsRoot = bcos::crypto::HashType(
        std::string_view("0x27bfbe21bcb21d27ff8f5d442b4e5110080b3df4e1d1f0b6314d77605e9f7e6e"),
        bcos::crypto::HashType::FromHex);
    data.receiptRoot.assign(receiptsRoot.begin(), receiptsRoot.end());
    auto mixHash = bcos::h256(
        std::string_view("0xb50774a2180b910c41018b5651e87200c3d10c7b7cd0443b20e346b3f289b66a"),
        bcos::h256::FromHex);
    data.prevRandao.assign(mixHash.begin(), mixHash.end());
    auto withdrawalsRoot = bcos::h256(
        std::string_view("0x1a470d20b701c3a7f5272198de31ac4a769ffc4dd0637e0c9718c0adf5c346f5"),
        bcos::h256::FromHex);
    data.withdrawalsHash.assign(withdrawalsRoot.begin(), withdrawalsRoot.end());
    auto beaconRoot = bcos::h256(
        std::string_view("0x81fcf3a2ae3a5543a467906ebc642cc9fc7d18fd094a253ea5349323b87494a2"),
        bcos::h256::FromHex);
    data.parentBeaconRoot.assign(beaconRoot.begin(), beaconRoot.end());
    auto parentHash = bcos::crypto::HashType(
        std::string_view("0x4c0f381da82f7c5232f921308b040f2f51475040db7b1ada1970960872182b44"),
        bcos::crypto::HashType::FromHex);
    data.parentInfo.clear();
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

    EthBlockHeader ethHeader(*header);
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

// A missing mandatory field (all-zero stateRoot) must be rejected by validateHeader.
BOOST_AUTO_TEST_CASE(validateHeaderRejectsMissingMandatoryField)
{
    auto header = makeEthHeader();
    auto impl = std::dynamic_pointer_cast<bcostars::protocol::BlockHeaderImpl>(header);
    BOOST_REQUIRE(impl != nullptr);
    impl->inner().data.stateRoot.clear();  // all-zero -> "missing"

    bcos::Error::UniquePtr error;
    error = bcos::protocol::EthBlockHeader::calculateRLPHash(*header);
    BOOST_CHECK(error != nullptr);
    BOOST_CHECK_EQUAL(error->errorCode(), static_cast<int32_t>(EthBlockHeaderError::InvalidHeader));
}

// A "middle optional missing, later present" input: the decode layer is faithful (no error),
// but the header's EthBlockVersion is then inconsistent with its fields, so validateHeader
// must reject it. Here we build a Cancun header and clear excessBlobGas while leaving
// parentBeaconRoot set — encode/decode keeps the fields as-is, and validation fails because
// CANCUN requires excessBlobGas.
BOOST_AUTO_TEST_CASE(validateHeaderRejectsMiddleGap)
{
    auto header = makeEthHeader(EthBlockVersion::CANCUN);
    auto impl = std::dynamic_pointer_cast<bcostars::protocol::BlockHeaderImpl>(header);
    BOOST_REQUIRE(impl != nullptr);
    impl->inner().data.excessBlobGas.clear();  // gap: blobGasUsed present, excessBlobGas gone

    // Encode -> decode must be faithful (no decode error).
    EthBlockHeader ethHeader(*header);
    bytes rlp;
    ethHeader.rlpEncode(rlp);
    bcos::Error::UniquePtr decodeError;
    EthBlockHeader decoded;
    decodeError = EthBlockHeader::toEthBlockHeader(decoded, bcos::ref(rlp));
    BOOST_CHECK(!decodeError);

    // The gap makes the header invalid for CANCUN: validation must fail.
    bcos::Error::UniquePtr error;
    error = bcos::protocol::EthBlockHeader::calculateRLPHash(*header);
    BOOST_CHECK(error != nullptr);
    BOOST_CHECK_EQUAL(error->errorCode(), static_cast<int32_t>(EthBlockHeaderError::InvalidHeader));
}

// calculateHash on an Eth header must recompute the RLP hash (it internally calls
// calculateRLPHash), so hash() returns the correct value without a separate call.
BOOST_AUTO_TEST_CASE(calculateHashRecomputesRLPHash)
{
    auto header = makeEthHeader();

    // Directly calculateHash — must inject the RLP hash (auto-call of calculateRLPHash).
    auto impl = std::dynamic_pointer_cast<bcostars::protocol::BlockHeaderImpl>(header);
    BOOST_REQUIRE(impl != nullptr);
    bcos::crypto::Keccak256 keccak;
    impl->calculateHash(keccak);

    bcos::protocol::EthBlockHeader ethHeader(*header);
    bytes rlp;
    ethHeader.rlpEncode(rlp);
    auto expected = bcos::crypto::keccak256Hash(bcos::ref(rlp));
    BOOST_CHECK(header->hash() == expected);
}

// The genesis block (number 0) has an empty parent hash by definition and must pass
// validateHeader (regression for the parentInfo check).
BOOST_AUTO_TEST_CASE(genesisExemption)
{
    auto header = makeEthHeader();
    auto impl = std::dynamic_pointer_cast<bcostars::protocol::BlockHeaderImpl>(header);
    BOOST_REQUIRE(impl != nullptr);
    impl->inner().data.blockNumber = 0;
    impl->inner().data.parentInfo.clear();  // empty parent hash

    bcos::Error::UniquePtr error;
    error = bcos::protocol::EthBlockHeader::calculateRLPHash(*header);
    BOOST_CHECK(!error);
}

// uncleHash is never legitimately zero on Ethereum (the empty-ommers constant is 0x1dcc…);
// an all-zero uncleHash must be rejected (regression for the restored zero check).
BOOST_AUTO_TEST_CASE(uncleHashZeroRejected)
{
    auto header = makeEthHeader();
    auto impl = std::dynamic_pointer_cast<bcostars::protocol::BlockHeaderImpl>(header);
    BOOST_REQUIRE(impl != nullptr);
    impl->inner().data.uncleHash.clear();  // all-zero

    bcos::Error::UniquePtr error;
    error = bcos::protocol::EthBlockHeader::calculateRLPHash(*header);
    BOOST_CHECK(error != nullptr);
    BOOST_CHECK_EQUAL(error->errorCode(), static_cast<int32_t>(EthBlockHeaderError::InvalidHeader));
}

// toTarsHeader must clear residual optional fields when the destination header is reused:
// decoding a lower-version RLP into a header that previously held higher-version fields
// must not leak them.
BOOST_AUTO_TEST_CASE(toTarsHeaderClearsResidual)
{
    // Reuse the same header object: first a PRAGUE decode, then a PRE_LONDON decode.
    auto header = makeEthHeader(EthBlockVersion::PRAGUE);
    EthBlockHeader ethHeader(*header);
    bytes pragueRlp;
    ethHeader.rlpEncode(pragueRlp);

    auto preLondonHeader = makeEthHeader(EthBlockVersion::PRE_LONDON);
    EthBlockHeader preLondon(*preLondonHeader);
    bytes preLondonRlp;
    preLondon.rlpEncode(preLondonRlp);

    // Decode PRE_LONDON into a header that currently holds PRAGUE fields.
    auto reused = makeEthHeader(EthBlockVersion::PRAGUE);
    bcos::Error::UniquePtr error;
    error = EthBlockHeader::toTarsHeader(reused, bcos::ref(preLondonRlp));
    BOOST_CHECK(!error);
    // Residual PRAGUE-only fields must have been cleared.
    BOOST_CHECK(!reused->requestsHash().has_value());
    BOOST_CHECK(!reused->parentBeaconBlockRoot().has_value());
    BOOST_CHECK(reused->ethBlockVersion() == EthBlockVersion::PRE_LONDON);
}

// A wire header whose seconds timestamp would overflow int64 milliseconds must be rejected
// by the decode bridge (rlpDecode) before its x1000 conversion, surfacing through
// decodeTarsHeader as an error (regression for the seconds->ms bridge). INT64_MAX seconds
// passes the codec's int64 width gate (round-6) but has no int64 millisecond representation.
BOOST_AUTO_TEST_CASE(decodeTarsHeaderRejectsOverflowingTimestamp)
{
    // Re-encode a valid header's fields with a hostile timestamp: INT64_MAX encodes fine as a
    // uint64 RLP scalar but ×1000 has no int64 millisecond representation.
    auto header = makeEthHeader();
    EthBlockHeader ethHeader(*header);
    auto const& d = ethHeader.data();
    constexpr uint64_t hostileTimestamp = 0x7FFFFFFFFFFFFFFFULL;

    bytes rlp;
    codec::rlp::encode(rlp, d.parentInfo.blockHash, d.uncleHash, d.coinbase, d.stateRoot, d.txsRoot,
        d.receiptsRoot, bcos::bytesConstRef(d.logsBloom.data(), d.logsBloom.size()), d.difficulty,
        static_cast<uint64_t>(d.number), d.gasLimit, d.gasUsed, hostileTimestamp, d.extraData,
        d.prevRandao, d.nonce, d.baseFee, d.withdrawalsHash, d.blobGasUsed, d.excessBlobGas,
        d.parentBeaconRoot, d.requestsHash);

    auto decodedHeader = makeEthHeader();
    auto error = EthBlockHeader::decodeTarsHeader(decodedHeader, bcos::ref(rlp));
    BOOST_REQUIRE(error != nullptr);
    BOOST_CHECK_EQUAL(error->errorCode(), static_cast<int32_t>(EthBlockHeaderError::InvalidHeader));
}

// An invalid Eth header: calculateHash clears dataHash, so hash() must throw
// EmptyBlockHeaderHash (regression for the FIB-130 clear-on-failure behaviour).
BOOST_AUTO_TEST_CASE(calculateHashClearsOnInvalid)
{
    auto header = makeEthHeader();
    auto impl = std::dynamic_pointer_cast<bcostars::protocol::BlockHeaderImpl>(header);
    BOOST_REQUIRE(impl != nullptr);
    impl->inner().data.stateRoot.clear();  // make it invalid

    bcos::crypto::Keccak256 keccak;
    BOOST_CHECK_NO_THROW(impl->calculateHash(keccak));
    BOOST_CHECK_THROW(header->hash(), std::exception);
}

// decodeTarsHeader pins ethBlockVersion to NON_ETH explicitly instead of relying on the tars
// default 0 happening to equal it. The pin holds even when the destination header carried a
// real ETH version before the decode. Internal timestamps are milliseconds for every version
// (the RLP surface is seconds, converted unconditionally by rlpDecode/rlpEncode), so the
// decoded header's timestamp equals the ms source's and the seconds↔ms round-trip reproduces
// the input RLP byte-for-byte regardless of the pinned version.
BOOST_AUTO_TEST_CASE(decodeTarsHeaderPinsNonEthVersion)
{
    auto source = makeEthHeader(EthBlockVersion::PRAGUE);
    EthBlockHeader sourceBridge(*source);
    bytes rlp;
    sourceBridge.rlpEncode(rlp);

    auto decoded = makeEthHeader(EthBlockVersion::PRAGUE);  // nonzero version pre-decode
    bcos::Error::UniquePtr error;
    error = EthBlockHeader::decodeTarsHeader(decoded, bcos::ref(rlp));
    BOOST_CHECK(!error);
    BOOST_CHECK(decoded->ethBlockVersion() == EthBlockVersion::NON_ETH);
    BOOST_CHECK_EQUAL(decoded->timestamp(), source->timestamp());

    // Re-encoding the decoded header must reproduce the input RLP: ×1000 on decode, /1000 on
    // encode — the unconditional inverse pair at the RLP bridge.
    EthBlockHeader roundTrip(*decoded);
    bytes reencoded;
    roundTrip.rlpEncode(reencoded);
    BOOST_CHECK(reencoded == rlp);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
