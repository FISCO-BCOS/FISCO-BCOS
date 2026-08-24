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
#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPEncode.h>
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
    // The factory still takes these as hex strings, but the getters now return u256 /
    // std::optional<u256> (#5299 changed value() from std::string_view to u256), so compare
    // against numbers rather than the input strings.
    BOOST_CHECK_EQUAL(tx->value(), u256(0x64));
    BOOST_REQUIRE(tx->gasPrice().has_value());
    BOOST_CHECK_EQUAL(tx->gasPrice().value(), u256(0x10));
    BOOST_CHECK_EQUAL(tx->gasLimit(), 21000);
    BOOST_REQUIRE(tx->maxFeePerGas().has_value());
    BOOST_CHECK_EQUAL(tx->maxFeePerGas().value(), u256(0x20));
    BOOST_REQUIRE(tx->maxPriorityFeePerGas().has_value());
    BOOST_CHECK_EQUAL(tx->maxPriorityFeePerGas().value(), u256(0x5));
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
    // A V0 transaction ignores the supplied gas fields: the factory stores the literal "0x0" in
    // each, so the u256 / optional<u256> getters see a present, zero value (not nullopt).
    BOOST_CHECK_EQUAL(tx->value(), u256(0));
    BOOST_REQUIRE(tx->gasPrice().has_value());
    BOOST_CHECK_EQUAL(tx->gasPrice().value(), u256(0));
    BOOST_CHECK_EQUAL(tx->gasLimit(), 0);
    BOOST_REQUIRE(tx->maxFeePerGas().has_value());
    BOOST_CHECK_EQUAL(tx->maxFeePerGas().value(), u256(0));
    BOOST_REQUIRE(tx->maxPriorityFeePerGas().has_value());
    BOOST_CHECK_EQUAL(tx->maxPriorityFeePerGas().value(), u256(0));
}

// deposit-only (0x7e) tars slots: sourceHash/mint/isSystemTransaction round-trip through
// TransactionImpl accessors (sourceHash/mint/isDepositTx), plus the isSystemTransaction-vs-kind
// distinction that isDepositTx() must honor.
BOOST_AUTO_TEST_CASE(depositMetadataAccessors)
{
    auto tx = std::make_shared<TransactionImpl>();
    auto& inner = tx->mutableInner();
    inner.type = static_cast<tars::Char>(bcos::protocol::TransactionType::Web3Transaction);
    inner.web3TypedTxKind = static_cast<tars::Char>(0x7e);
    // hex without 0x prefix, matching Web3Transaction::takeToTarsTransaction (h256::hex())
    inner.sourceHash = "6ab967dfdd3aa359031bef6965cca32ed9a21ea969f7aeee2e58817142a645d7";
    // "0x"+hex, matching takeToTarsTransaction (mint() parses via bcos::u256)
    inner.mint = "0x16345785d8a0000";
    inner.isSystemTransaction = 1;

    BOOST_CHECK(tx->isDepositTx());
    BOOST_CHECK_EQUAL(
        tx->sourceHash(), "6ab967dfdd3aa359031bef6965cca32ed9a21ea969f7aeee2e58817142a645d7");
    BOOST_CHECK_EQUAL(tx->mint(), u256("0x16345785d8a0000"));
    // size() accounts for the deposit metadata
    BOOST_CHECK(tx->size() >= inner.sourceHash.size() + inner.mint.size());

    // A non-system deposit (isSystemTx=false) must still be a deposit: isDepositTx() keys off
    // web3TypedTxKind(), never off the per-transaction isSystemTransaction flag.
    auto nonSystemDeposit = std::make_shared<TransactionImpl>();
    nonSystemDeposit->mutableInner().type =
        static_cast<tars::Char>(bcos::protocol::TransactionType::Web3Transaction);
    nonSystemDeposit->mutableInner().web3TypedTxKind = static_cast<tars::Char>(0x7e);
    nonSystemDeposit->mutableInner().isSystemTransaction = 0;
    BOOST_CHECK(nonSystemDeposit->isDepositTx());

    // Empty mint reads back as zero.
    auto noMint = std::make_shared<TransactionImpl>();
    noMint->mutableInner().type =
        static_cast<tars::Char>(bcos::protocol::TransactionType::Web3Transaction);
    noMint->mutableInner().web3TypedTxKind = static_cast<tars::Char>(0x7e);
    BOOST_CHECK(noMint->isDepositTx());
    BOOST_CHECK_EQUAL(noMint->mint(), u256(0));
    BOOST_CHECK(noMint->sourceHash().empty());

    // Over-wide mint (>64 hex digits / >66 with 0x prefix) must return 0 — bcos::u256's
    // boost unchecked backend silently truncates >256 bits, so the length guard must catch
    // it before the parse (Shichen-Wu review #5434).
    auto wideMint = std::make_shared<TransactionImpl>();
    wideMint->mutableInner().type =
        static_cast<tars::Char>(bcos::protocol::TransactionType::Web3Transaction);
    wideMint->mutableInner().web3TypedTxKind = static_cast<tars::Char>(0x7e);
    wideMint->mutableInner().mint = std::string(65, 'a');  // 65 hex digits, no prefix
    BOOST_CHECK(wideMint->isDepositTx());
    BOOST_CHECK_EQUAL(wideMint->mint(), u256(0));

    // Exactly 64 hex digits (256-bit max) parses normally — the guard must not be off-by-one.
    auto maxMint = std::make_shared<TransactionImpl>();
    maxMint->mutableInner().type =
        static_cast<tars::Char>(bcos::protocol::TransactionType::Web3Transaction);
    maxMint->mutableInner().web3TypedTxKind = static_cast<tars::Char>(0x7e);
    maxMint->mutableInner().mint = "0x" + std::string(64, 'f');  // exactly 256-bit
    BOOST_CHECK(maxMint->isDepositTx());
    BOOST_CHECK(maxMint->mint() != u256(0));
    BOOST_CHECK_EQUAL(maxMint->mint(), u256("0x" + std::string(64, 'f')));

    // A regular typed tx (EIP-1559) is not a deposit.
    auto eip1559 = std::make_shared<TransactionImpl>();
    eip1559->mutableInner().type =
        static_cast<tars::Char>(bcos::protocol::TransactionType::Web3Transaction);
    eip1559->mutableInner().web3TypedTxKind = static_cast<tars::Char>(2);
    eip1559->mutableInner().isSystemTransaction = 1;
    BOOST_CHECK(!eip1559->isDepositTx());
    BOOST_CHECK(eip1559->sourceHash().empty());
    BOOST_CHECK_EQUAL(eip1559->mint(), u256(0));

    // A forged BCOS tx (type=0, kind=0x7e) is never a deposit: web3TypedTxKind() gates on
    // type()==Web3Transaction.
    auto forgedBcos = std::make_shared<TransactionImpl>();
    forgedBcos->mutableInner().type =
        static_cast<tars::Char>(bcos::protocol::TransactionType::BCOSTransaction);
    forgedBcos->mutableInner().web3TypedTxKind = static_cast<tars::Char>(0x7e);
    BOOST_CHECK(!forgedBcos->isDepositTx());
}

// Deposit hash: the canonical txHash of a 0x7e tx is keccak256 of the full envelope (type byte
// + RLP fields) — op-geth DepositTx.Hash() / op-reth TxDeposit::tx_hash(). calculateHash must
// handle the deposit branch (deposits carry no signature, so the normal reassemble path would
// throw on the missing 65-byte signature). Fill extraTransactionBytes with a real envelope and
// check extraTransactionHash equals keccak of those bytes verbatim.
BOOST_AUTO_TEST_CASE(depositHashIsKeccakOfEnvelope)
{
    auto suite = makeSuite();
    auto tx = std::make_shared<TransactionImpl>();
    auto& inner = tx->mutableInner();
    inner.type = static_cast<tars::Char>(bcos::protocol::TransactionType::Web3Transaction);
    inner.web3TypedTxKind = static_cast<tars::Char>(0x7e);
    // A minimal but well-formed 0x7e envelope: rlp([sourceHash, from, to, mint, value, gas,
    // isSystemTx, data]) preceded by the type byte.
    bcos::bytes body;
    bcos::codec::rlp::encode(body,
        bcos::h256("0x6ab967dfdd3aa359031bef6965cca32ed9a21ea969f7aeee2e58817142a645d7"),
        bcos::Address("0xdead000000000000000000000000000000000011"),
        bcos::Address("0x4200000000000000000000000000000000000022"), bcos::bytes{}, bcos::bytes{},
        static_cast<uint64_t>(500000), static_cast<uint64_t>(0), bcos::bytes{});
    bcos::bytes envelope{0x7e};
    bcos::codec::rlp::encodeHeader(envelope, bcos::codec::rlp::Header{true, body.size()});
    envelope.insert(envelope.end(), body.begin(), body.end());
    inner.extraTransactionBytes.assign(envelope.begin(), envelope.end());

    tx->calculateHash(*suite->hashImpl());
    auto const expect = bcos::crypto::keccak256Hash(bcos::ref(envelope));
    BOOST_CHECK(tx->hash() == expect);
    BOOST_CHECK_EQUAL(bcos::toHex(tx->hash().asBytes()), bcos::toHex(expect.asBytes()));
}

// web3ChainIdFromEnvelope must read the SIGNED envelope (extraTransactionBytes), never the
// forgeable tars mirror (data.chainID) — a regression swapping to the mirror would pass every
// other test in this file. Envelope carries a legacy EIP-155 preimage with chainId 42; the
// mirror is deliberately forged to 999.
BOOST_AUTO_TEST_CASE(web3ChainIdFromEnvelopeReadsEnvelopeNotTarsMirror)
{
    auto suite = makeSuite();
    namespace rlp = bcos::codec::rlp;

    bcos::bytes items;
    rlp::encode(items, static_cast<uint64_t>(1));   // nonce
    rlp::encode(items, static_cast<uint64_t>(10));  // gasPrice
    rlp::encode(items, static_cast<uint64_t>(21000));
    rlp::encode(items, bcos::bytes(20, 0x01));
    rlp::encode(items, static_cast<uint64_t>(0));   // value
    rlp::encode(items, bcos::bytes{});              // data
    rlp::encode(items, static_cast<uint64_t>(42));  // field 7 = EIP-155 chainId
    rlp::encode(items, static_cast<uint64_t>(0));   // 0 placeholder
    rlp::encode(items, static_cast<uint64_t>(0));   // 0 placeholder
    bcos::bytes env;
    rlp::encodeHeader(env, rlp::Header{true, items.size()});
    env.insert(env.end(), items.begin(), items.end());

    auto tx = std::make_shared<TransactionImpl>();
    auto& inner = tx->mutableInner();
    inner.type = static_cast<tars::Char>(bcos::protocol::TransactionType::Web3Transaction);
    inner.data.chainID = "999";  // forged string mirror — must NOT be consulted
    inner.extraTransactionBytes.assign(env.begin(), env.end());

    auto chainId = tx->web3ChainIdFromEnvelope();
    BOOST_REQUIRE(chainId.has_value());
    BOOST_CHECK_EQUAL(chainId.value(), 42u);

    // Non-Web3 transactions return nullopt regardless of the envelope content.
    auto legacy = std::make_shared<TransactionImpl>();
    auto& legacyInner = legacy->mutableInner();
    legacyInner.type = static_cast<tars::Char>(bcos::protocol::TransactionType::BCOSTransaction);
    legacyInner.extraTransactionBytes.assign(env.begin(), env.end());
    BOOST_CHECK(!legacy->web3ChainIdFromEnvelope().has_value());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
