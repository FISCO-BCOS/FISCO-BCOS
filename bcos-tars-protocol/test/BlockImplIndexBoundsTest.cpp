/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * @brief Regression test for issue #5355: BlockImpl setTransaction / setReceipt /
 *        transactionHash must reject out-of-range indices instead of writing past the vector.
 */
#include "bcos-tars-protocol/protocol/BlockImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionMetaDataImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <boost/test/unit_test.hpp>
#include <stdexcept>

using namespace bcostars::protocol;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(BlockImplIndexBoundsTest)

namespace
{
bcos::crypto::CryptoSuite::Ptr makeSuite()
{
    return std::make_shared<bcos::crypto::CryptoSuite>(std::make_shared<bcos::crypto::Keccak256>(),
        std::make_shared<bcos::crypto::Secp256k1Crypto>(), nullptr);
}

bcos::protocol::Transaction::Ptr makeTx(TransactionFactoryImpl& factory, bcos::byte tag)
{
    return factory.createTransaction(0, "0xa", bcos::bytes{tag}, "0x1", 1, "c", "g", 0);
}
}  // namespace

BOOST_AUTO_TEST_CASE(setTransactionPastSizeThrows)
{
    auto suite = makeSuite();
    TransactionFactoryImpl txFactory(suite);
    auto block = std::make_shared<BlockImpl>();
    block->appendTransaction(makeTx(txFactory, 0x01));

    BOOST_CHECK_THROW(block->setTransaction(1, makeTx(txFactory, 0x02)), std::out_of_range);
    BOOST_CHECK_THROW(block->setTransaction(100, makeTx(txFactory, 0x02)), std::out_of_range);
    BOOST_CHECK_EQUAL(block->transactionsSize(), 1U);  // unchanged

    // in-range still works
    BOOST_CHECK_NO_THROW(block->setTransaction(0, makeTx(txFactory, 0x03)));
}

BOOST_AUTO_TEST_CASE(setReceiptPastTransactionCountThrows)
{
    auto suite = makeSuite();
    TransactionFactoryImpl txFactory(suite);
    TransactionReceiptFactoryImpl rFactory(suite);
    std::vector<bcos::protocol::LogEntry> logs;
    bcos::bytes out;
    auto receipt = rFactory.createReceipt(bcos::u256(1), "", logs, 0, bcos::ref(out), 1);

    // No transactions at all: the lazy resize sizes receipts to 0, so index 0 is out of range.
    auto empty = std::make_shared<BlockImpl>();
    BOOST_CHECK_THROW(empty->setReceipt(0, receipt), std::out_of_range);
    BOOST_CHECK_EQUAL(empty->receiptsSize(), 0U);

    // Two transactions: indices 0/1 valid, 2 is past the transaction count.
    auto block = std::make_shared<BlockImpl>();
    block->appendTransaction(makeTx(txFactory, 0x01));
    block->appendTransaction(makeTx(txFactory, 0x02));
    BOOST_CHECK_NO_THROW(block->setReceipt(1, receipt));
    BOOST_CHECK_EQUAL(block->receiptsSize(), 2U);
    BOOST_CHECK_THROW(block->setReceipt(2, receipt), std::out_of_range);
    BOOST_CHECK_EQUAL(block->receiptsSize(), 2U);

    // More receipts than transactions (appendReceipt): an out-of-range setReceipt must throw
    // without first truncating the receipts vector back to the transaction count.
    block->appendReceipt(receipt);
    BOOST_CHECK_EQUAL(block->receiptsSize(), 3U);
    BOOST_CHECK_THROW(block->setReceipt(3, receipt), std::out_of_range);
    BOOST_CHECK_EQUAL(block->receiptsSize(), 3U);
}

BOOST_AUTO_TEST_CASE(transactionHashPastMetaDataSizeThrows)
{
    auto block = std::make_shared<BlockImpl>();
    BOOST_CHECK_THROW((void)block->transactionHash(0), std::out_of_range);

    const bcos::crypto::HashType hash(1U);
    block->appendTransactionMetaData(std::make_shared<TransactionMetaDataImpl>(hash, "0xto"));
    BOOST_CHECK(block->transactionHash(0) == hash);
    BOOST_CHECK_THROW((void)block->transactionHash(1), std::out_of_range);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
