/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "bcos-tars-protocol/protocol/BlockImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
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

BOOST_AUTO_TEST_SUITE(TarsBlockImplTest)

// The bcostars::Block adopting constructor plus the inner()/setInner() accessors.
BOOST_AUTO_TEST_CASE(fromTarsAndInnerAccessors)
{
    bcostars::Block tars;
    tars.type = 2;
    BlockImpl block(std::move(tars));
    BOOST_CHECK_EQUAL(static_cast<int>(block.blockType()), 2);

    block.inner().type = 3;  // mutable inner
    BOOST_CHECK_EQUAL(static_cast<int>(block.blockType()), 3);
    BOOST_CHECK_EQUAL(std::as_const(block).inner().type, 3);

    bcostars::Block replacement;
    replacement.type = 5;
    block.setInner(std::move(replacement));
    BOOST_CHECK_EQUAL(static_cast<int>(block.blockType()), 5);
}

// appendTransaction / setTransaction(index) and the receipt equivalents,
// including setReceipt's lazy resize of the receipts vector.
BOOST_AUTO_TEST_CASE(transactionAndReceiptByIndex)
{
    auto suite = makeSuite();
    TransactionFactoryImpl txFactory(suite);
    auto block = std::make_shared<BlockImpl>();

    block->appendTransaction(
        txFactory.createTransaction(0, "0xa", bcos::bytes{0x01}, "0x1", 1, "c", "g", 0));
    block->appendTransaction(
        txFactory.createTransaction(0, "0xb", bcos::bytes{0x02}, "0x2", 1, "c", "g", 0));
    BOOST_CHECK_EQUAL(block->transactionsSize(), 2U);

    // Replace the transaction at index 1 in place.
    block->setTransaction(
        1, txFactory.createTransaction(0, "0xc", bcos::bytes{0x03}, "0x3", 1, "c", "g", 0));
    BOOST_CHECK_EQUAL(block->transactionsSize(), 2U);

    // setReceipt with an empty receipts vector resizes it to the transaction
    // count before writing (one receipt slot per transaction).
    TransactionReceiptFactoryImpl rFactory(suite);
    std::vector<bcos::protocol::LogEntry> logs;
    bcos::bytes out;
    auto receipt = rFactory.createReceipt(bcos::u256(1), "", logs, 0, bcos::ref(out), 1);
    block->setReceipt(0, receipt);
    BOOST_CHECK_EQUAL(block->receiptsSize(), 2U);  // resized to transactionsSize()

    block->appendReceipt(receipt);
    BOOST_CHECK_EQUAL(block->receiptsSize(), 3U);
}

// nonceList round-trips through the any_view setter/getter.
BOOST_AUTO_TEST_CASE(nonceListRoundTrip)
{
    auto block = std::make_shared<BlockImpl>();
    std::vector<std::string> nonces{"0x1", "0x2", "0x3"};
    block->setNonceList(nonces);
    std::vector<std::string> readBack;
    for (auto&& nonce : block->nonceList())
    {
        readBack.push_back(nonce);
    }
    BOOST_CHECK_EQUAL_COLLECTIONS(readBack.begin(), readBack.end(), nonces.begin(), nonces.end());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
