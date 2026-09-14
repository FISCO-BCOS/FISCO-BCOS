/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * @brief Regression test for issue #5553: TransactionReceiptImpl must persist logIndex through
 *        the tars field, survive encode/decode, and NOT change the receipt hash (the hash covers
 *        only the fields listed in TarsHashable.h, and logIndex is stamped after hashing like
 *        transactionIndex / cumulativeGasUsed / logsBloom).
 */
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptImpl.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <boost/test/unit_test.hpp>

using namespace bcostars::protocol;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(TransactionReceiptLogIndexTest)

namespace
{
bcos::crypto::CryptoSuite::Ptr makeSuite()
{
    return std::make_shared<bcos::crypto::CryptoSuite>(std::make_shared<bcos::crypto::Keccak256>(),
        std::make_shared<bcos::crypto::Secp256k1Crypto>(), nullptr);
}
}  // namespace

BOOST_AUTO_TEST_CASE(logIndexRoundTripsAndDoesNotAffectHash)
{
    auto suite = makeSuite();
    TransactionReceiptFactoryImpl factory(suite);
    std::vector<bcos::protocol::LogEntry> logs;
    logs.emplace_back(bcos::bytes{0x01}, std::vector<bcos::h256>{bcos::h256(1U)}, bcos::bytes{});
    bcos::bytes out;
    auto receipt = factory.createReceipt(bcos::u256(21000), "", logs, 0, bcos::ref(out), 5);
    auto hashBefore = receipt->hash();
    BOOST_CHECK_EQUAL(receipt->logIndex(), 0U);

    receipt->setLogIndex(17);
    BOOST_CHECK_EQUAL(receipt->logIndex(), 17U);

    // encode -> decode keeps it
    bcos::bytes encoded;
    receipt->encode(encoded);
    auto decoded = factory.createReceipt(bcos::ref(encoded));
    BOOST_CHECK_EQUAL(decoded->logIndex(), 17U);

    // the receipt hash is untouched by the stamp (legacy receiptsRoot unchanged)
    BOOST_CHECK(receipt->hash() == hashBefore);
    BOOST_CHECK(decoded->hash() == hashBefore);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
