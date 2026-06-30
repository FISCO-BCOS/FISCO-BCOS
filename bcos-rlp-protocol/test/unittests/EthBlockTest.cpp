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
 * @file EthBlockTest.cpp
 * @brief Unit tests for EthBlockHeader, EthWithdrawal, EthBlock
 * @date 2026/6/30
 */
#include "bcos-rlp-protocol/EthBlockHeader.h"
#include "bcos-rlp-protocol/EthBlock.h"
#include "bcos-rlp-protocol/EthWithdrawal.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::protocol;

namespace bcos::test
{

BOOST_FIXTURE_TEST_SUITE(EthBlockTest, TestPromptFixture)

// =====================================================================
// EthWithdrawal tests
// =====================================================================

BOOST_AUTO_TEST_CASE(withdrawalEncodeDecode)
{
    EthWithdrawal wd;
    wd.index = 5;
    wd.validatorIndex = 3;
    wd.address = Address("0xabcf8e0d4e8f2f5b8f1c3a4b5d6e7f8a9b0c1d2e");
    wd.amount = 1000000000000000000ULL;

    bcos::bytes encoded;
    wd.encode(encoded);

    EthWithdrawal decoded;
    auto err = decoded.decode(bcos::ref(encoded));
    BOOST_CHECK(!err);
    BOOST_CHECK(wd == decoded);
}

BOOST_AUTO_TEST_CASE(withdrawalEmpty)
{
    EthWithdrawal wd;
    bcos::bytes encoded;
    wd.encode(encoded);

    EthWithdrawal decoded;
    auto err = decoded.decode(bcos::ref(encoded));
    BOOST_CHECK(!err);
    BOOST_CHECK(wd == decoded);
}

// =====================================================================
// EthBlockHeader tests
// =====================================================================

BOOST_AUTO_TEST_CASE(headerEncodeDecode)
{
    EthBlockHeader header;
    header.setParentHash(h256("0x1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef"));
    header.setNumber(12345);
    header.setTimestamp(1700000000);
    header.setGasLimit(30000000);
    header.setGasUsed(21000);
    header.setCoinbase(Address("0xdead000000000000000000000000000000000000"));
    header.setExtraData(bcos::bytes{0x12, 0x34, 0x56});

    header.calculateHash();
    auto hash1 = header.hash();

    // Encode and decode
    bcos::bytes encoded;
    header.encode(encoded);

    EthBlockHeader decoded;
    auto err = decoded.decode(bcos::ref(encoded));
    BOOST_CHECK(!err);
    decoded.calculateHash();

    BOOST_CHECK(header.parentHash() == decoded.parentHash());
    BOOST_CHECK_EQUAL(header.number(), decoded.number());
    BOOST_CHECK_EQUAL(header.timestamp(), decoded.timestamp());
    BOOST_CHECK_EQUAL(header.gasLimit(), decoded.gasLimit());
    BOOST_CHECK_EQUAL(header.gasUsed(), decoded.gasUsed());
    BOOST_CHECK(header.coinbase() == decoded.coinbase());
    BOOST_CHECK(header.extraData().toBytes() == decoded.extraData().toBytes());
    BOOST_CHECK(header.hash() == decoded.hash());
}

BOOST_AUTO_TEST_CASE(headerOptionalFields)
{
    EthBlockHeader header;
    header.setNumber(1);
    header.setTimestamp(1000);
    header.setBaseFee(u256(1000000000));
    header.setBlobGasUsed(u256(100));
    header.setExcessBlobGas(u256(50));
    header.setSlotNumber(42);

    bcos::bytes encoded;
    header.encode(encoded);

    EthBlockHeader decoded;
    auto err = decoded.decode(bcos::ref(encoded));
    BOOST_CHECK(!err);

    BOOST_CHECK(decoded.baseFee().has_value());
    BOOST_CHECK_EQUAL(*decoded.baseFee(), u256(1000000000));
    BOOST_CHECK(decoded.blobGasUsed().has_value());
    BOOST_CHECK_EQUAL(*decoded.blobGasUsed(), u256(100));
    BOOST_CHECK(decoded.excessBlobGas().has_value());
    BOOST_CHECK_EQUAL(*decoded.excessBlobGas(), u256(50));
    BOOST_CHECK(decoded.slotNumber().has_value());
    BOOST_CHECK_EQUAL(*decoded.slotNumber(), 42);

    BOOST_CHECK(!decoded.withdrawalsRoot().has_value());
    BOOST_CHECK(!decoded.parentBeaconBlockRoot().has_value());
}

BOOST_AUTO_TEST_CASE(headerRoundtripWithHash)
{
    EthBlockHeader header;
    header.setNumber(100);
    header.setTimestamp(2000000);
    header.setParentHash(h256("0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    header.setStateRoot(crypto::HashType("0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
    header.setTxsRoot(crypto::HashType("0xcccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"));
    header.setReceiptsRoot(crypto::HashType("0xdddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"));
    header.calculateHash();

    bcos::bytes encoded;
    header.encode(encoded);

    EthBlockHeader decoded(bcos::ref(encoded));
    BOOST_CHECK_EQUAL(decoded.number(), 100);
    BOOST_CHECK_EQUAL(decoded.timestamp(), 2000000);
    BOOST_CHECK(header.hash() == decoded.hash());
}

BOOST_AUTO_TEST_CASE(headerEncodedLength)
{
    EthBlockHeader header;
    header.setNumber(100);
    header.setTimestamp(2000);

    bcos::bytes encoded;
    header.encode(encoded);
    BOOST_CHECK_EQUAL(header.encodedLength(), encoded.size());
}

// =====================================================================
// EthBlock tests
// =====================================================================

BOOST_AUTO_TEST_CASE(blockEncodeDecodeEmpty)
{
    EthBlock block;
    bcos::bytes encoded;
    block.encode(encoded);

    EthBlock decoded;
    auto err = decoded.decode(bcos::ref(encoded));
    BOOST_CHECK(!err);
    BOOST_CHECK_EQUAL(decoded.transactionRlps().size(), 0);
    BOOST_CHECK_EQUAL(decoded.withdrawals().size(), 0);
}

BOOST_AUTO_TEST_CASE(blockWithTransactions)
{
    // Build a simple Legacy transaction RLP
    bcos::bytes txRlp;
    codec::rlp::encode(txRlp,
        0ULL, 1ULL, 21000ULL,
        bcos::bytes{}, u256(0), bcos::bytes{},
        uint64_t(0x1b), bcos::bytes{1}, bcos::bytes{1});

    bcos::bytes txRlp2;
    codec::rlp::encode(txRlp2,
        1ULL, 10ULL, 50000ULL,
        Address("0x1234567890123456789012345678901234567890").ref(),
        u256(1000), bcos::bytes{0xde, 0xad},
        uint64_t(0x1b), bcos::bytes{0x11, 0x22}, bcos::bytes{0x33, 0x44});

    EthBlock block;
    block.blockHeader().setNumber(42);
    block.blockHeader().setTimestamp(1700000000);
    block.blockHeader().calculateHash();
    block.appendTransaction(txRlp);
    block.appendTransaction(std::move(txRlp2));

    BOOST_CHECK_EQUAL(block.transactionRlps().size(), 2);

    bcos::bytes encoded;
    block.encode(encoded);

    EthBlock decoded;
    auto err = decoded.decode(bcos::ref(encoded));
    BOOST_CHECK(!err);
    BOOST_CHECK_EQUAL(decoded.transactionRlps().size(), 2);
    BOOST_CHECK_EQUAL(decoded.blockHeader().number(), 42);
    BOOST_CHECK_EQUAL(decoded.blockHeader().timestamp(), 1700000000);

    for (size_t i = 0; i < 2; ++i)
    {
        auto origHash = crypto::keccak256Hash(bcos::ref(block.transactionRlps()[i]));
        auto decodedHash = crypto::keccak256Hash(bcos::ref(decoded.transactionRlps()[i]));
        BOOST_CHECK(origHash == decodedHash);
    }
}

BOOST_AUTO_TEST_CASE(blockWithWithdrawals)
{
    EthWithdrawal wd1;
    wd1.index = 0;
    wd1.amount = 1000000000000000000ULL;

    EthWithdrawal wd2;
    wd2.index = 1;
    wd2.validatorIndex = 2;
    wd2.address = Address("0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef");
    wd2.amount = 2000000000000000000ULL;

    EthBlock block;
    block.blockHeader().setNumber(100);
    block.blockHeader().setTimestamp(5000);
    block.blockHeader().calculateHash();
    block.appendWithdrawal(wd1);
    block.appendWithdrawal(wd2);

    bcos::bytes encoded;
    block.encode(encoded);

    EthBlock decoded;
    auto err = decoded.decode(bcos::ref(encoded));
    BOOST_CHECK(!err);
    BOOST_CHECK_EQUAL(decoded.withdrawals().size(), 2);
    BOOST_CHECK(decoded.withdrawals()[0] == wd1);
    BOOST_CHECK(decoded.withdrawals()[1] == wd2);
}

BOOST_AUTO_TEST_CASE(blockFullRoundtrip)
{
    EthBlock block;

    // Header
    auto& hdr = block.blockHeader();
    hdr.setNumber(500);
    hdr.setTimestamp(1800000000);
    hdr.setParentHash(h256("0x1111111111111111111111111111111111111111111111111111111111111111"));
    hdr.setStateRoot(crypto::HashType("0x2222222222222222222222222222222222222222222222222222222222222222"));
    hdr.setTxsRoot(crypto::HashType("0x3333333333333333333333333333333333333333333333333333333333333333"));
    hdr.setReceiptsRoot(crypto::HashType("0x4444444444444444444444444444444444444444444444444444444444444444"));
    hdr.setGasLimit(30000000);
    hdr.setGasUsed(21000);
    hdr.setCoinbase(Address("0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    hdr.setExtraData(bcos::bytes{0xca, 0xfe});
    hdr.setBaseFee(u256(1000000000));
    hdr.calculateHash();

    // Transaction
    bcos::bytes tx1;
    codec::rlp::encode(tx1, 0ULL, 1ULL, 21000ULL,
        bcos::bytes{}, u256(0), bcos::bytes{},
        uint64_t(0x1b), bcos::bytes{1}, bcos::bytes{1});
    block.appendTransaction(tx1);

    // Withdrawal
    EthWithdrawal wd;
    wd.index = 0;
    wd.amount = 5000000000000000000ULL;
    block.appendWithdrawal(wd);

    bcos::bytes encoded;
    block.encode(encoded);

    EthBlock decoded;
    auto err = decoded.decode(bcos::ref(encoded));
    BOOST_CHECK(!err);

    BOOST_CHECK_EQUAL(decoded.blockHeader().number(), 500);
    BOOST_CHECK_EQUAL(decoded.blockHeader().timestamp(), 1800000000);
    BOOST_CHECK(decoded.blockHeader().parentHash() == hdr.parentHash());
    BOOST_CHECK_EQUAL(decoded.blockHeader().gasLimit(), 30000000);
    BOOST_CHECK_EQUAL(decoded.blockHeader().gasUsed(), 21000);
    BOOST_CHECK_EQUAL(*decoded.blockHeader().baseFee(), u256(1000000000));

    BOOST_CHECK_EQUAL(decoded.transactionRlps().size(), 1);
    BOOST_CHECK_EQUAL(decoded.withdrawals().size(), 1);
    BOOST_CHECK(decoded.withdrawals()[0] == wd);
}

BOOST_AUTO_TEST_CASE(blockClear)
{
    EthBlock block;
    block.blockHeader().setNumber(10);
    block.appendTransaction(bcos::bytes{0x01});
    BOOST_CHECK_EQUAL(block.transactionRlps().size(), 1);

    block.clear();
    BOOST_CHECK_EQUAL(block.blockHeader().number(), 0);
    BOOST_CHECK_EQUAL(block.transactionRlps().size(), 0);
    BOOST_CHECK_EQUAL(block.withdrawals().size(), 0);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
