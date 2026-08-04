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
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <boost/test/unit_test.hpp>
#include <stdexcept>

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

// --- Coverage + validation-path tests (added to raise coverage and probe the
// --- factory's error handling for real defects). ---

// createTransaction() with no args returns a fresh, tainted transaction.
BOOST_AUTO_TEST_CASE(createEmptyTransactionIsTainted)
{
    auto suite = makeSuite();
    TransactionFactoryImpl factory(suite);
    auto tx = factory.createTransaction();
    BOOST_REQUIRE(tx);
    BOOST_CHECK(tx->tainted());
}

// createTransaction(Transaction&) is the "adopt an existing transaction"
// overload: it moves the inner payload and must carry over every book-keeping
// flag. A dropped flag here would silently change consensus/sync behavior, so
// assert each one explicitly.
BOOST_AUTO_TEST_CASE(createFromInputPreservesAllFlags)
{
    auto suite = makeSuite();
    TransactionFactoryImpl factory(suite);
    auto src =
        factory.createTransaction(0, "0xto", bcos::bytes{0x01}, "0x9", 77, "chainA", "groupA", 555);
    // Flip every flag away from its default so a missed copy is observable.
    src->setSynced(true);
    src->setSealed(true);
    src->setInvalid(true);
    src->setSystemTx(true);
    src->setStoreToBackend(true);
    src->setBatchId(4242);
    bcos::crypto::HashType batchHash(
        "0x1234000000000000000000000000000000000000000000000000000000000000");
    src->setBatchHash(batchHash);
    // nonce() returns a string_view into src's payload, which createTransaction
    // moves away below — copy it to an owning string before the move so the
    // comparison does not read a dangling view.
    std::string srcNonce{src->nonce()};
    auto srcImportTime = src->importTime();

    auto adopted = factory.createTransaction(*src);
    BOOST_REQUIRE(adopted);
    BOOST_CHECK(adopted->synced());
    BOOST_CHECK(adopted->sealed());
    BOOST_CHECK(adopted->invalid());
    BOOST_CHECK(adopted->systemTx());
    BOOST_CHECK(adopted->storeToBackend());
    BOOST_CHECK_EQUAL(adopted->batchId(), 4242);
    BOOST_CHECK_EQUAL(adopted->batchHash(), batchHash);
    // Moved payload must survive the adoption.
    BOOST_CHECK_EQUAL(adopted->nonce(), srcNonce);
    BOOST_CHECK_EQUAL(adopted->importTime(), srcImportTime);
    BOOST_CHECK_EQUAL(adopted->chainId(), "chainA");
}

// A V1 transaction whose value is not a hex string must be rejected at decode,
// both through createTransaction(bytes,...) and decodeTransaction(bytes,...).
// This is the txpool-facing guard against malformed quantity fields.
BOOST_AUTO_TEST_CASE(decodeRejectsNonHexQuantityV1)
{
    auto suite = makeSuite();
    TransactionFactoryImpl factory(suite);
    auto tx = factory.createTransaction(
        1, "0xto", bcos::bytes{0x01}, "0x1", 100, "chain0", "group0", 0, "abi", "0x64", "0x10");
    auto* impl = dynamic_cast<TransactionImpl*>(tx.get());
    BOOST_REQUIRE(impl);
    impl->mutableInner().data.value = "not-a-hex-quantity";  // corrupt the field
    bcos::bytes encoded;
    impl->encode(encoded);

    BOOST_CHECK_THROW(
        factory.createTransaction(bcos::ref(encoded), false, false), std::invalid_argument);
    BOOST_CHECK_THROW(factory.decodeTransaction(bcos::ref(encoded)), std::invalid_argument);
}

// isHexStringV2 is deliberately lenient: an empty quantity is treated as valid.
// Pin that behavior so a stricter change is a conscious decision, not a
// silent regression.
BOOST_AUTO_TEST_CASE(decodeAcceptsEmptyQuantityV1)
{
    auto suite = makeSuite();
    TransactionFactoryImpl factory(suite);
    auto tx = factory.createTransaction(
        1, "0xto", bcos::bytes{0x01}, "0x1", 100, "chain0", "group0", 0, "abi", "0x64", "0x10");
    auto* impl = dynamic_cast<TransactionImpl*>(tx.get());
    BOOST_REQUIRE(impl);
    impl->mutableInner().data.maxPriorityFeePerGas = "";  // empty == valid per isHexStringV2
    bcos::bytes encoded;
    impl->encode(encoded);
    BOOST_CHECK_NO_THROW(factory.decodeTransaction(bcos::ref(encoded)));
}

// Decoding with checkHash=true must reject a transaction whose stored dataHash
// does not match the recomputed hash — the anti-tamper guard on the wire form.
BOOST_AUTO_TEST_CASE(decodeRejectsHashMismatch)
{
    auto suite = makeSuite();
    TransactionFactoryImpl factory(suite);
    auto tx =
        factory.createTransaction(0, "0xto", bcos::bytes{0x02}, "0x1", 100, "chain0", "group0", 0);
    auto* impl = dynamic_cast<TransactionImpl*>(tx.get());
    BOOST_REQUIRE(impl);
    auto& dataHash = impl->mutableInner().dataHash;
    BOOST_REQUIRE(!dataHash.empty());
    dataHash[0] = static_cast<std::decay_t<decltype(dataHash[0])>>(dataHash[0] ^ 0xff);  // tamper
    bcos::bytes encoded;
    impl->encode(encoded);

    BOOST_CHECK_THROW(factory.createTransaction(bcos::ref(encoded), /*checkSig=*/false,
                          /*checkHash=*/true),
        std::invalid_argument);
    // With checkHash disabled the same bytes decode without complaint.
    BOOST_CHECK_NO_THROW(
        factory.createTransaction(bcos::ref(encoded), /*checkSig=*/false, /*checkHash=*/false));
}

// The full builder rejects a non-hex quantity up front for V1 transactions.
BOOST_AUTO_TEST_CASE(builderRejectsNonHexQuantityV1)
{
    auto suite = makeSuite();
    TransactionFactoryImpl factory(suite);
    BOOST_CHECK_THROW(factory.createTransaction(1, "0xto", bcos::bytes{0x01}, "0x1", 100, "chain0",
                          "group0", 0, "abi", /*value=*/"100"),
        std::invalid_argument);
}

// A V0 transaction ignores the gas/fee fields: the builder forces them to
// canonical zero regardless of what the caller passes.
BOOST_AUTO_TEST_CASE(builderV0ForcesZeroGasFields)
{
    auto suite = makeSuite();
    TransactionFactoryImpl factory(suite);
    auto tx = factory.createTransaction(0, "0xto", bcos::bytes{0x01}, "0x1", 100, "chain0",
        "group0", 0, "abi", "0xdeadbeef", "0xdeadbeef", 999, "0xdeadbeef", "0xdeadbeef");
    BOOST_REQUIRE(tx);
    BOOST_CHECK_EQUAL(tx->value(), "0x0");
    BOOST_CHECK_EQUAL(tx->gasPrice(), "0x0");
    BOOST_CHECK_EQUAL(tx->gasLimit(), 0);
    BOOST_CHECK_EQUAL(tx->maxFeePerGas(), "0x0");
    BOOST_CHECK_EQUAL(tx->maxPriorityFeePerGas(), "0x0");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
