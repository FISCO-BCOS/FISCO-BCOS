/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
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

BOOST_AUTO_TEST_SUITE(TarsTransactionFactoryImplTest)

BOOST_AUTO_TEST_CASE(buildWithAllGasFields)
{
    auto suite = makeSuite();
    TransactionFactoryImpl factory(suite);
    auto tx = factory.createTransaction(/*version=*/1, "0xto", bcos::bytes{0x01}, "0xnonce",
        /*blockLimit=*/500, "chain0", "group0", /*importTime=*/123, "abi", "0x64", "0x10",
        /*gasLimit=*/21000, "0x20", "0x5");
    BOOST_REQUIRE(tx);
    BOOST_CHECK_EQUAL(tx->version(), 1);
    BOOST_CHECK_EQUAL(tx->nonce(), "0xnonce");
    BOOST_CHECK_EQUAL(tx->blockLimit(), 500);
    BOOST_CHECK_EQUAL(tx->chainId(), "chain0");
    BOOST_CHECK_EQUAL(tx->groupId(), "group0");
    BOOST_CHECK_EQUAL(tx->importTime(), 123);
    BOOST_CHECK_EQUAL(tx->abi(), "abi");
    BOOST_CHECK_EQUAL(tx->value(), "0x64");
    BOOST_CHECK_EQUAL(tx->gasPrice(), "0x10");
    BOOST_CHECK_EQUAL(tx->gasLimit(), 21000);
    BOOST_CHECK_EQUAL(tx->maxFeePerGas(), "0x20");
    BOOST_CHECK_EQUAL(tx->maxPriorityFeePerGas(), "0x5");
    // A built transaction has a computed hash.
    BOOST_CHECK_NE(tx->hash(), bcos::crypto::HashType{});
}

BOOST_AUTO_TEST_CASE(buildWithKeyPairSignsAndVerifies)
{
    auto suite = makeSuite();
    TransactionFactoryImpl factory(suite);
    auto keyPair = suite->signatureImpl()->generateKeyPair();
    auto tx = factory.createTransaction(
        1, "0xto", bcos::bytes{0x0a, 0x0b}, "0x1", 100, "chain0", "group0", 0, *keyPair);
    BOOST_REQUIRE(tx);
    // Signed transaction carries a non-empty signature.
    BOOST_CHECK(!tx->signatureData().empty());
    BOOST_CHECK_NE(tx->hash(), bcos::crypto::HashType{});
}

BOOST_AUTO_TEST_CASE(encodeDecodeRoundTripWithSigCheck)
{
    auto suite = makeSuite();
    TransactionFactoryImpl factory(suite);
    auto keyPair = suite->signatureImpl()->generateKeyPair();
    auto tx = factory.createTransaction(
        1, "0xto", bcos::bytes{0x0a}, "0x1", 100, "chain0", "group0", 0, *keyPair);
    BOOST_REQUIRE(tx);

    bcos::bytes encoded;
    tx->encode(encoded);
    // Decode with signature + hash checks enabled — exercises the verify path.
    auto decoded =
        factory.createTransaction(bcos::ref(encoded), /*checkSig=*/true, /*checkHash=*/true);
    BOOST_REQUIRE(decoded);
    BOOST_CHECK_EQUAL(decoded->hash(), tx->hash());
    BOOST_CHECK_EQUAL(decoded->nonce(), tx->nonce());
}

BOOST_AUTO_TEST_CASE(decodeTransactionStandalone)
{
    auto suite = makeSuite();
    TransactionFactoryImpl factory(suite);
    auto tx =
        factory.createTransaction(1, "0xto", bcos::bytes{0x01}, "0x1", 100, "chain0", "group0", 0);
    bcos::bytes encoded;
    tx->encode(encoded);
    auto decoded = factory.decodeTransaction(bcos::ref(encoded));
    BOOST_REQUIRE(decoded);
    BOOST_CHECK_EQUAL(decoded->chainId(), "chain0");
}

BOOST_AUTO_TEST_CASE(cryptoSuiteAccessorRoundTrips)
{
    auto suite = makeSuite();
    TransactionFactoryImpl factory(suite);
    BOOST_CHECK(factory.cryptoSuite() == suite);
    auto other = makeSuite();
    factory.setCryptoSuite(other);
    BOOST_CHECK(factory.cryptoSuite() == other);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
