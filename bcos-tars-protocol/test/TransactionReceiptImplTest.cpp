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

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
