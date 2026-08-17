/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "bcos-tars-protocol/protocol/TransactionReceiptImpl.h"
#include "bcos-tars-protocol/Common.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/Exceptions.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::crypto;
using namespace bcostars::protocol;

namespace bcos::test
{
namespace
{
CryptoSuite::Ptr makeSuite()
{
    return std::make_shared<CryptoSuite>(
        std::make_shared<Keccak256>(), std::make_shared<Secp256k1Crypto>(), nullptr);
}

// Hand-encode a tars TransactionReceipt the way the PREVIOUS dev-stage format did:
// TransactionReceiptData at tag 1, then a raw string at tag 8 (the old `opReceiptMeta`
// RLP payload). Used to empirically pin what tars does when decoding old-format bytes into
// the new struct-typed field 8.
bcos::bytes encodeReceiptWithStringTag8(std::string const& oldMetaString)
{
    tars::TarsOutputStream<bcostars::protocol::BufferWriterByteVector> os;
    bcostars::TransactionReceiptData data;  // default-constructed (version=0, tag 1)
    os.write(data, 1);
    os.write(oldMetaString, 8);  // old `optional string opReceiptMeta`
    return os.getByteBuffer();
}
}  // namespace

BOOST_AUTO_TEST_SUITE(TarsTransactionReceiptImplTest)

BOOST_AUTO_TEST_CASE(createReceiptPopulatesCoreFields)
{
    auto suite = makeSuite();
    TransactionReceiptFactoryImpl factory(suite);
    std::vector<bcos::protocol::LogEntry> logs;
    bcos::bytes output{0x01, 0x02, 0x03};
    auto receipt = factory.createReceipt(bcos::u256(21000),
        "0x1234567890123456789012345678901234567890", logs, /*status=*/0, bcos::ref(output),
        /*blockNumber=*/42);
    BOOST_REQUIRE(receipt);
    BOOST_CHECK_EQUAL(receipt->status(), 0);
    BOOST_CHECK_EQUAL(receipt->gasUsed(), bcos::u256(21000));
    BOOST_CHECK_EQUAL(receipt->blockNumber(), 42);
    BOOST_CHECK_EQUAL(receipt->contractAddress(), "0x1234567890123456789012345678901234567890");
    BOOST_CHECK(receipt->version() >= 0);
    BOOST_CHECK(receipt->logEntries().empty());
    // A freshly built receipt has a non-empty hash (it is computed on build).
    BOOST_CHECK_NE(receipt->hash(), bcos::crypto::HashType{});
}

BOOST_AUTO_TEST_CASE(settersRoundTrip)
{
    auto suite = makeSuite();
    TransactionReceiptFactoryImpl factory(suite);
    std::vector<bcos::protocol::LogEntry> logs;
    bcos::bytes output;
    auto receipt = factory.createReceipt(bcos::u256(1), "", logs, 0, bcos::ref(output), 1);
    BOOST_REQUIRE(receipt);

    receipt->setEffectiveGasPrice("0x10");
    BOOST_CHECK_EQUAL(receipt->effectiveGasPrice(), "0x10");

    receipt->setCumulativeGasUsed("0x5208");
    BOOST_CHECK_EQUAL(receipt->cumulativeGasUsed(), "0x5208");

    receipt->setTransactionIndex(5);
    BOOST_CHECK_EQUAL(receipt->transactionIndex(), 5U);

    bcos::bytes bloom(256, 0x00);
    bloom[0] = 0xAB;
    receipt->setLogsBloom(bcos::ref(bloom));
    BOOST_CHECK_EQUAL(receipt->logsBloom().size(), 256U);
    BOOST_CHECK_EQUAL(receipt->logsBloom()[0], 0xAB);
}

BOOST_AUTO_TEST_CASE(logEntriesRoundTrip)
{
    auto suite = makeSuite();
    TransactionReceiptFactoryImpl factory(suite);
    std::vector<bcos::protocol::LogEntry> logs;
    bcos::bytes output;
    auto receipt = factory.createReceipt(bcos::u256(0), "", logs, 0, bcos::ref(output), 1);
    BOOST_REQUIRE(receipt);

    std::vector<bcos::protocol::LogEntry> newLogs;
    bcos::h256s topics{bcos::crypto::HashType()};
    newLogs.emplace_back(bcos::bytes{0xDE, 0xAD}, topics, bcos::bytes{0xBE, 0xEF});
    // setLogEntries is a concrete-impl method, not on the base interface.
    auto impl = std::dynamic_pointer_cast<TransactionReceiptImpl>(receipt);
    BOOST_REQUIRE(impl);
    impl->setLogEntries(newLogs);
    BOOST_REQUIRE_EQUAL(impl->logEntries().size(), 1U);
    BOOST_CHECK_EQUAL(impl->logEntries()[0].data().size(), 2U);
}

// --- Added coverage: hash guard, calculateHash, take/inner accessors, message. ---

// hash() on a receipt whose dataHash was never computed must throw rather than
// return a zero/garbage hash — a receipt is only valid once hashed.
BOOST_AUTO_TEST_CASE(hashThrowsBeforeCompute)
{
    TransactionReceiptImpl receipt;
    BOOST_CHECK_THROW(receipt.hash(), bcos::Exception);

    // After calculateHash the guard is satisfied and a real hash is returned.
    auto suite = makeSuite();
    receipt.calculateHash(*suite->hashImpl());
    BOOST_CHECK_NE(receipt.hash(), bcos::crypto::HashType{});
}

// takeLogEntries lazily materializes the entries from the tars payload when the
// C++-side cache is empty (the just-decoded state).
BOOST_AUTO_TEST_CASE(takeLogEntriesFromPayload)
{
    auto suite = makeSuite();
    TransactionReceiptFactoryImpl factory(suite);
    std::vector<bcos::protocol::LogEntry> logs;
    logs.emplace_back(bcos::bytes{0x11}, bcos::h256s{}, bcos::bytes{0x0a, 0x0b});
    bcos::bytes output;
    auto receipt = factory.createReceipt(bcos::u256(0), "", logs, 0, bcos::ref(output), 1);
    auto impl = std::dynamic_pointer_cast<TransactionReceiptImpl>(receipt);
    BOOST_REQUIRE(impl);
    auto taken = impl->takeLogEntries();
    BOOST_REQUIRE_EQUAL(taken.size(), 1U);
    BOOST_CHECK(taken[0].data().toBytes() == (bcos::bytes{0x0a, 0x0b}));
}

// inner()/setInner()/innerGetter() give lower layers direct access to the tars
// struct; make sure both setInner overloads replace the payload.
BOOST_AUTO_TEST_CASE(innerAccessorsRoundTrip)
{
    TransactionReceiptImpl receipt;
    bcostars::TransactionReceipt copyIn;
    copyIn.message = "from-copy";
    receipt.setInner(copyIn);
    BOOST_CHECK_EQUAL(receipt.message(), "from-copy");

    bcostars::TransactionReceipt moveIn;
    moveIn.message = "from-move";
    receipt.setInner(std::move(moveIn));
    BOOST_CHECK_EQUAL(receipt.message(), "from-move");

    // const and mutable inner() plus the getter are all reachable.
    BOOST_CHECK_EQUAL(std::as_const(receipt).inner().message, "from-move");
    receipt.inner().message = "mutated";
    BOOST_CHECK_EQUAL(receipt.message(), "mutated");
    BOOST_CHECK(static_cast<bool>(receipt.innerGetter()));
}

BOOST_AUTO_TEST_CASE(messageRoundTrip)
{
    TransactionReceiptImpl receipt;
    receipt.setMessage("execution reverted");
    BOOST_CHECK_EQUAL(receipt.message(), "execution reverted");
}

// size()/gasUsed()/logIndex() edge behavior on default and populated receipts.
BOOST_AUTO_TEST_CASE(sizeGasUsedAndLogIndex)
{
    // A default receipt has empty gasUsed (reads back as zero) and zero size.
    TransactionReceiptImpl empty;
    BOOST_CHECK_EQUAL(empty.gasUsed(), bcos::u256(0));
    BOOST_CHECK_EQUAL(empty.size(), 0U);
    // logIndex is a stub: always zero, setter is a no-op.
    empty.setLogIndex(9);
    BOOST_CHECK_EQUAL(empty.logIndex(), 0U);

    // size() accumulates output + log payload + message; check it tracks the
    // message delta rather than an exact byte total (robust to encoding).
    auto suite = makeSuite();
    TransactionReceiptFactoryImpl factory(suite);
    std::vector<bcos::protocol::LogEntry> logs;
    logs.emplace_back(bcos::bytes{0x11, 0x22}, bcos::h256s{}, bcos::bytes{0x0a, 0x0b, 0x0c});
    bcos::bytes output{0x01, 0x02, 0x03, 0x04};
    auto receipt = factory.createReceipt(bcos::u256(1), "", logs, 0, bcos::ref(output), 1);
    auto impl = std::dynamic_pointer_cast<TransactionReceiptImpl>(receipt);
    BOOST_REQUIRE(impl);
    auto before = impl->size();
    BOOST_CHECK_GE(before, 4U);  // at least the output bytes
    impl->setMessage("msg");
    BOOST_CHECK_EQUAL(impl->size(), before + 3);
}

// --- OpStackReceiptMeta: tars round-trip + legacy-data behavior (Task 6). ---
// Every round-trip below goes through the real tars wire format: setOpStackMeta -> encode
// -> decode -> opStackMeta.

// The critical presence guarantee: explicit zeros (da_footprint=0, deposit_nonce=0) must
// survive serialization. tars stores them as "0x0" (non-empty string), and opStackMeta()
// reports them as present-with-value-0 -- never conflated with "field was never set".
BOOST_AUTO_TEST_CASE(opStackMetaRoundTripZeroValues)
{
    auto suite = makeSuite();
    TransactionReceiptFactoryImpl factory(suite);
    std::vector<bcos::protocol::LogEntry> logs;
    bcos::bytes output;
    auto receipt = factory.createReceipt(bcos::u256(0), "", logs, 0, bcos::ref(output), 1);
    auto impl = std::dynamic_pointer_cast<TransactionReceiptImpl>(receipt);
    BOOST_REQUIRE(impl);

    bcos::protocol::OpStackReceiptMeta meta;
    meta.l1_fee = bcos::u256(123456789);
    meta.da_footprint = 0;  // explicit zero must keep its presence
    meta.deposit_nonce = 42;
    impl->setOpStackMeta(meta);

    bcos::bytes encoded;
    impl->encode(encoded);
    TransactionReceiptImpl decoded;
    decoded.decode(bcos::ref(encoded));

    auto got = decoded.opStackMeta();
    BOOST_REQUIRE(got.has_value());
    BOOST_REQUIRE(got->l1_fee.has_value());
    BOOST_CHECK_EQUAL(*got->l1_fee, bcos::u256(123456789));
    BOOST_REQUIRE(got->da_footprint.has_value());
    BOOST_CHECK_EQUAL(*got->da_footprint, 0u);
    BOOST_REQUIRE(got->deposit_nonce.has_value());
    BOOST_CHECK_EQUAL(*got->deposit_nonce, 42u);
    // Fields never set stay absent.
    BOOST_CHECK(!got->l1_gas_price.has_value());
    BOOST_CHECK(!got->operator_fee.has_value());
}

// u256 fields are stored as 32-byte hex strings; a value with the high 192 bits set must
// round-trip without truncation (no narrowing through a 64-bit intermediate).
BOOST_AUTO_TEST_CASE(opStackMetaRoundTripFullWidthU256)
{
    auto suite = makeSuite();
    TransactionReceiptFactoryImpl factory(suite);
    std::vector<bcos::protocol::LogEntry> logs;
    bcos::bytes output;
    auto receipt = factory.createReceipt(bcos::u256(0), "", logs, 0, bcos::ref(output), 1);
    auto impl = std::dynamic_pointer_cast<TransactionReceiptImpl>(receipt);
    BOOST_REQUIRE(impl);

    auto fullWidth = bcos::fromBigEndian<bcos::u256>(
        bcos::fromHex("1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef"));
    bcos::protocol::OpStackReceiptMeta meta;
    meta.l1_fee = fullWidth;
    meta.operator_fee = fullWidth - 1;
    impl->setOpStackMeta(meta);

    bcos::bytes encoded;
    impl->encode(encoded);
    TransactionReceiptImpl decoded;
    decoded.decode(bcos::ref(encoded));

    auto got = decoded.opStackMeta();
    BOOST_REQUIRE(got.has_value());
    BOOST_REQUIRE(got->l1_fee.has_value());
    BOOST_CHECK_EQUAL(*got->l1_fee, fullWidth);
    BOOST_REQUIRE(got->operator_fee.has_value());
    BOOST_CHECK_EQUAL(*got->operator_fee, fullWidth - 1);
}

// Deposit receipts carry deposit_nonce==0 (must survive as present-with-0) together with
// deposit_receipt_version==1. This is the exact shape the execution layer writes for a
// deposit transaction.
BOOST_AUTO_TEST_CASE(opStackMetaDepositRoundTrip)
{
    auto suite = makeSuite();
    TransactionReceiptFactoryImpl factory(suite);
    std::vector<bcos::protocol::LogEntry> logs;
    bcos::bytes output;
    auto receipt = factory.createReceipt(bcos::u256(0), "", logs, 0, bcos::ref(output), 1);
    auto impl = std::dynamic_pointer_cast<TransactionReceiptImpl>(receipt);
    BOOST_REQUIRE(impl);

    bcos::protocol::OpStackReceiptMeta meta;
    meta.deposit_nonce = 0;  // deposit nonce is zero in the OP deposit path
    meta.deposit_receipt_version = 1;
    impl->setOpStackMeta(meta);

    bcos::bytes encoded;
    impl->encode(encoded);
    TransactionReceiptImpl decoded;
    decoded.decode(bcos::ref(encoded));

    auto got = decoded.opStackMeta();
    BOOST_REQUIRE(got.has_value());
    BOOST_REQUIRE(got->deposit_nonce.has_value());
    BOOST_CHECK_EQUAL(*got->deposit_nonce, 0u);
    BOOST_REQUIRE(got->deposit_receipt_version.has_value());
    BOOST_CHECK_EQUAL(*got->deposit_receipt_version, 1u);
}

// OLD dev-stage data: tag 8 held `optional string opReceiptMeta` (RLP payload). Now that
// tag 8 is an OpStackReceiptMeta struct, tars reports a type mismatch -- it does NOT decode
// to an empty struct or garbage fields. Empirically confirms "direct replacement of tag 8"
// is NOT backward compatible with persisted old-format OP receipts (dev stage: none exist).
BOOST_AUTO_TEST_CASE(opStackMetaLegacyStringTag8Throws)
{
    auto wire = encodeReceiptWithStringTag8("0x0102deadbeef");
    TransactionReceiptImpl receipt;
    BOOST_CHECK_THROW(receipt.decode(bcos::ref(wire)), tars::TarsDecodeException);
}

// A normal legacy receipt never wrote field 8 at all. tars read(opStackMeta, 8, false)
// leaves the struct default (all empty strings) and opStackMeta() reports nullopt -- the
// backward-compatible path for the huge majority of existing receipts.
BOOST_AUTO_TEST_CASE(opStackMetaLegacyNoTag8DecodesToNullopt)
{
    tars::TarsOutputStream<bcostars::protocol::BufferWriterByteVector> os;
    bcostars::TransactionReceiptData data;
    os.write(data, 1);
    auto wire = os.getByteBuffer();

    TransactionReceiptImpl receipt;
    receipt.decode(bcos::ref(wire));
    BOOST_CHECK(!receipt.opStackMeta().has_value());
}

// hex-quantity rejection paths (I6): a corrupt hex string, an over-wide u256 (>32 bytes) and
// an over-wide u64 (>8 bytes) must each decode to nullopt -- never a silently truncated value
// -- while the 32-byte / 8-byte boundaries must still parse. Injected straight into the tars
// struct (inner-constructor) so the read-side getter's strict paths are exercised directly.
BOOST_AUTO_TEST_CASE(opStackMetaRejectsCorruptAndOverwideHex)
{
    auto tars = std::make_shared<bcostars::TransactionReceipt>();
    bcostars::OpStackReceiptMeta m;
    m.l1_fee = "0x1a";  // one valid field: keeps opStackMeta() from short-circuiting to nullopt
    m.l1_gas_price = "0xZZ";                           // invalid hex -> nullopt
    m.l1_blob_base_fee = "0x" + std::string(66, 'f');  // 33 bytes > 32 -> nullopt
    m.operator_fee = "0x" + std::string(64, 'f');      // 32 bytes == 32 boundary -> parses
    m.da_footprint = "0x" + std::string(18, 'f');      // 9 bytes > 8 -> nullopt
    m.deposit_nonce = "0x" + std::string(16, 'f');     // 8 bytes == 8 boundary -> parses
    tars->opStackMeta = m;
    TransactionReceiptImpl impl([tars]() { return tars.get(); });

    auto got = impl.opStackMeta();
    BOOST_REQUIRE(got.has_value());
    BOOST_REQUIRE(got->l1_fee.has_value());
    BOOST_CHECK_EQUAL(*got->l1_fee, bcos::u256(0x1a));
    BOOST_CHECK(!got->l1_gas_price.has_value());      // corrupt hex rejected
    BOOST_CHECK(!got->l1_blob_base_fee.has_value());  // 33-byte u256 rejected
    BOOST_REQUIRE(got->operator_fee.has_value());     // 32-byte u256 boundary accepted
    BOOST_CHECK_EQUAL(*got->operator_fee, ~bcos::u256(0));
    BOOST_CHECK(!got->da_footprint.has_value());    // 9-byte u64 rejected
    BOOST_REQUIRE(got->deposit_nonce.has_value());  // 8-byte u64 boundary accepted
    BOOST_CHECK_EQUAL(*got->deposit_nonce, std::numeric_limits<uint64_t>::max());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
