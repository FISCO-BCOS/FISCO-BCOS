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
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
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

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
