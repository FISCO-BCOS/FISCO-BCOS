/**
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief unit test for the txpool
 * @file TransactionValidatorTest.cpp
 * @author: asherli
 * @date 2024-12-11
 */
#include "bcos-crypto/interfaces/crypto/KeyPairInterface.h"
#include "bcos-framework/bcos-framework/testutils/faker/FakeTransaction.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/txpool/Constant.h"
#include "bcos-protocol/TransactionStatus.h"

#include "bcos-task/Wait.h"
#include "test/unittests/txpool/TxPoolFixture.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>
#include <iostream>
#include <optional>
using namespace bcos;
using namespace bcos::txpool;
using namespace bcos::protocol;
using namespace bcos::crypto;
using namespace std::string_view_literals;

namespace bcos
{
namespace test
{
namespace
{
// Distinct name from Issue5318InvalidToTest.cpp's makeValidator (same unity TU).
TxValidator::Ptr makeAdmissionValidator(bcos::crypto::CryptoSuite::Ptr cryptoSuite = nullptr)
{
    return std::make_shared<TxValidator>(
        nullptr, nullptr, std::move(cryptoSuite), "group0", "chain0");
}
}  // namespace

BOOST_FIXTURE_TEST_SUITE(TxPoolTest, TestPromptFixture)
BOOST_AUTO_TEST_CASE(testTransactionValidator)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    auto keyPair = signatureImpl->generateKeyPair();
    std::string groupId = "group_test_for_txpool";
    std::string chainId = "chain_test_for_txpool";
    int64_t blockLimit = 10;
    auto fakeGateWay = std::make_shared<FakeGateWay>();
    auto faker = std::make_shared<TxPoolFixture>(
        keyPair->publicKey(), cryptoSuite, groupId, chainId, blockLimit, fakeGateWay, false, false);
    faker->init();

    auto txpoolConfig = faker->txpool()->txpoolConfig();
    auto txpool = faker->txpool();
    auto txpoolStorage = txpool->txpoolStorage();
    auto ledger = faker->ledger();

    auto const eoaKey = cryptoSuite->signatureImpl()->generateKeyPair();
    // case3: transaction with invalid nonce(conflict with the ledger nonce)
    auto const& blockData = ledger->ledgerData();
    size_t importedTxNum = 1;

    auto duplicatedNonce =
        blockData[ledger->blockNumber() - blockLimit + 1]->transactions()[0]->nonce();
    std::string duplicatedNonceStr(duplicatedNonce);
    auto tx = fakeWeb3Tx(cryptoSuite, duplicatedNonceStr, eoaKey);
    // bcos nonce not effect web3 nonce

    u256 fakeNonce = u256(duplicatedNonce);
    faker->ledger()->initEoaContext(
        eoaKey->address(cryptoSuite->hashImpl()).hex(), fakeNonce.convert_to<std::string>());
    auto result = txpoolConfig->txValidator()->validateTransaction(*tx);
    BOOST_CHECK(result == TransactionStatus::None);

    std::string inputStr = "testTransactionValidatorTx";
    // fake input str large size transaction
    auto inputStrLarge = "0x" + std::string(MAX_INITCODE_SIZE, '1');
    // std::string duplicatedNonceStr(duplicatedNonce);
    tx = fakeWeb3Tx(cryptoSuite, duplicatedNonceStr, eoaKey, inputStrLarge);
    auto resultLarge = txpoolConfig->txValidator()->validateTransaction(*tx);
    BOOST_CHECK(resultLarge == TransactionStatus::MaxInitCodeSizeExceeded);

    // check with state
    // auto resultWithStateNoAccount = TransactionValidator::ValidateTransactionWithState(tx,
    // ledger); BOOST_CHECK(resultWithStateNoAccount == TransactionStatus::NoAccount);

    std::optional<storage::Entry> codeHashOp = storage::Entry();
    codeHashOp->set(asBytes(""));
    ledger->setStorageAt(
        toHex(tx->sender()), std::string(bcos::ledger::ACCOUNT_TABLE_FIELDS::CODE), codeHashOp);


    // NoEOA
    auto const eoaKeyNew = cryptoSuite->signatureImpl()->generateKeyPair();
    // case3: transaction with invalid nonce(conflict with the ledger nonce)
    // std::string duplicatedNonceStr(duplicatedNonce);
    auto txNoEoa = fakeWeb3Tx(cryptoSuite, duplicatedNonceStr, eoaKeyNew);

    // std::optional<storage::Entry> codeHashOpNew = storage::Entry();
    // codeHashOpNew->set(asBytes("0x0123456"));
    // ledger->setStorageAt(
    //     toHex(txNoEoa->sender()), std::string(bcos::ledger::ACCOUNT_TABLE_FIELDS::CODE),
    //     codeHashOpNew);

    // auto resultWithStateNoEOAAccount =
    //     task::syncWait(TransactionValidator::ValidateTransactionWithState(txNoEoa, ledger));
    // BOOST_CHECK(resultWithStateNoEOAAccount == TransactionStatus::SenderNoEOA);

    const uint64_t value = 1234567;
    auto txNoEoughtValue = fakeInvalidateTransacton(inputStr, value);
    auto txImpl = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(txNoEoughtValue);
    txImpl->mutableInner().type =
        static_cast<uint8_t>(bcos::protocol::TransactionType::Web3Transaction);
    auto resultWithStateNoEnoughBalance =
        task::syncWait(txpoolConfig->txValidator()->validateBalance(*txNoEoughtValue, ledger));
    BOOST_CHECK(resultWithStateNoEnoughBalance == TransactionStatus::InsufficientFunds);

    txpool->txpoolStorage()->clear();
    std::cout << "#### testTransactionValidator finish" << std::endl;
}

BOOST_AUTO_TEST_CASE(testValidateBalanceIncludesGasCost)
{
    // FIB-75: validateBalance should:
    // 1. Reject transactions where tx.gasPrice < systemGasPrice
    // 2. Use tx.gasPrice (not systemGasPrice) to compute gas cost
    // 3. Reject transactions where balance < txValue + txGasLimit * txGasPrice
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    auto keyPair = signatureImpl->generateKeyPair();
    std::string groupId = "group_test_for_txpool";
    std::string chainId = "chain_test_for_txpool";
    int64_t blockLimit = 10;
    auto fakeGateWay = std::make_shared<FakeGateWay>();
    auto faker = std::make_shared<TxPoolFixture>(
        keyPair->publicKey(), cryptoSuite, groupId, chainId, blockLimit, fakeGateWay, false, false);
    faker->init();

    auto txpoolConfig = faker->txpool()->txpoolConfig();
    auto ledger = faker->ledger();

    // Set system gas price to "1000"
    ledger->setSystemConfig(std::string(bcos::ledger::SYSTEM_KEY_TX_GAS_PRICE), "1000");

    std::string inputStr = "testGasCostValidation";

    // Case 1: tx.gasPrice below systemGasPrice → rejected
    {
        auto tx = fakeInvalidateTransacton(inputStr, 0);
        auto txImpl = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx);
        txImpl->mutableInner().type =
            static_cast<uint8_t>(bcos::protocol::TransactionType::Web3Transaction);
        txImpl->mutableInner().data.gasLimit = 1000;
        txImpl->mutableInner().data.gasPrice = "0x64";  // 100 (hex) < systemGasPrice 1000
        auto result = task::syncWait(txpoolConfig->txValidator()->validateBalance(*tx, ledger));
        BOOST_CHECK(result == TransactionStatus::InsufficientFunds);
    }

    // Case 2: tx.gasPrice >= systemGasPrice, but balance insufficient for gas cost
    // tx.gasPrice = 1000, gasLimit = 1000, value = 0 → totalRequired = 1,000,000
    // balance = 0 (default in FakeLedger without scheduler) → rejected
    {
        auto tx = fakeInvalidateTransacton(inputStr, 0);
        auto txImpl = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx);
        txImpl->mutableInner().type =
            static_cast<uint8_t>(bcos::protocol::TransactionType::Web3Transaction);
        txImpl->mutableInner().data.gasLimit = 1000;
        txImpl->mutableInner().data.gasPrice = "0x3e8";  // 1000 (hex) == systemGasPrice
        auto result = task::syncWait(txpoolConfig->txValidator()->validateBalance(*tx, ledger));
        BOOST_CHECK(result == TransactionStatus::InsufficientFunds);
    }

    // Case 3: value-only check with no gasLimit (backward-compatible with Case 3 of existing test)
    {
        auto tx = fakeInvalidateTransacton(inputStr, 1234567);
        auto txImpl = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx);
        txImpl->mutableInner().type =
            static_cast<uint8_t>(bcos::protocol::TransactionType::Web3Transaction);
        txImpl->mutableInner().data.gasPrice = "0x3e8";  // 1000 (hex), passes gasPrice check
        // gasLimit defaults to 0, so gasCost = 0, totalRequired = 1234567, balance 0 → reject
        auto result = task::syncWait(txpoolConfig->txValidator()->validateBalance(*tx, ledger));
        BOOST_CHECK(result == TransactionStatus::InsufficientFunds);
    }

    std::cout << "#### testValidateBalanceIncludesGasCost finish" << std::endl;
}

// A 0x7e deposit must be rejected at admission even when "self-signed": deposits are unsigned
// system txs that only enter via the engine newPayload path (op-geth: ErrTxTypeNotSupported).
BOOST_AUTO_TEST_CASE(testRejectDepositAtAdmission)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    auto validator = makeAdmissionValidator(cryptoSuite);

    // Build a deposit-shaped tars tx: type=Web3, web3TypedTxKind=0x7e. isDepositTx() must
    // trigger on web3TypedTxKind alone (never on isSystemTransaction).
    bcostars::Transaction inner{};
    inner.type = static_cast<tars::Char>(TransactionType::Web3Transaction);
    inner.web3TypedTxKind = static_cast<tars::Char>(0x7e);
    inner.sourceHash = "0x" + std::string(64, 'a');
    inner.data.input.assign({'x', 'y'});
    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
        [m = std::move(inner)]() mutable { return &m; });
    BOOST_CHECK(tx->isDepositTx());
    BOOST_CHECK(validator->verify(*tx) == TransactionStatus::Malformed);

    // A deposit that also claims to be a system tx must still be rejected — isDepositTx()
    // keys on web3TypedTxKind, not on the system flag.
    auto systemInner = inner;
    systemInner.isSystemTransaction = 1;
    auto systemTx = std::make_shared<bcostars::protocol::TransactionImpl>(
        [m = std::move(systemInner)]() mutable { return &m; });
    BOOST_CHECK(validator->verify(*systemTx) == TransactionStatus::Malformed);

    // A normal Web3 tx (web3TypedTxKind=0) must NOT be rejected by this gate: the reject
    // must key on the deposit kind, not on the Web3 type in general. (The full verify() of a
    // non-deposit Web3 tx — signature recovery + nonce check — is covered by the existing
    // testTransactionValidator cases; here we only assert the gate's classification.)
    auto normalInner = inner;
    normalInner.web3TypedTxKind = 0;
    auto normalTx = std::make_shared<bcostars::protocol::TransactionImpl>(
        [m = std::move(normalInner)]() mutable { return &m; });
    BOOST_CHECK(!normalTx->isDepositTx());
    // The gate is the FIRST check in verify(): a non-deposit Web3 tx must pass it and reach
    // signature recovery (which fails on an empty signature — InvalidSignature, NOT Malformed).
    BOOST_CHECK(validator->verify(*normalTx) != TransactionStatus::Malformed);

    // Mirror-rewrite bypass shape: the tars mirror claims a normal type (kind=0x02) while the
    // envelope is a genuine 0x7e deposit. isDepositTx() (mirror-keyed) says "not a deposit", so
    // the gate must key on the envelope's first byte and reject this too.
    {
        bcostars::Transaction forged = inner;
        forged.web3TypedTxKind = static_cast<tars::Char>(0x02);  // mirror claims EIP-1559
        bcos::bytes body;
        bcos::codec::rlp::encode(body,
            bcos::h256("0x6ab967dfdd3aa359031bef6965cca32ed9a21ea969f7aeee2e58817142a645d7"),
            bcos::Address("0xdead000000000000000000000000000000000011"),
            bcos::Address("0x4200000000000000000000000000000000000022"), bcos::bytes{},
            bcos::bytes{}, static_cast<uint64_t>(500000), static_cast<uint64_t>(0), bcos::bytes{});
        bcos::bytes envelope{0x7e};
        bcos::codec::rlp::encodeHeader(envelope, bcos::codec::rlp::Header{true, body.size()});
        envelope.insert(envelope.end(), body.begin(), body.end());
        forged.extraTransactionBytes.assign(envelope.begin(), envelope.end());
        auto forgedTx = std::make_shared<bcostars::protocol::TransactionImpl>(
            [m = std::move(forged)]() mutable { return &m; });
        BOOST_CHECK(!forgedTx->isDepositTx());  // mirror says not a deposit
        BOOST_CHECK(validator->verify(*forgedTx) == TransactionStatus::Malformed);
    }

    // A kind=0x02 tx with a NON-deposit envelope must pass the deposit gate (rejected later by
    // signature recovery) — the envelope-byte check must not over-match typed txs.
    {
        bcostars::Transaction typed = inner;
        typed.web3TypedTxKind = static_cast<tars::Char>(0x02);
        bcos::bytes preimage{0x02};
        bcos::codec::rlp::encode(preimage, static_cast<uint64_t>(1), static_cast<uint64_t>(0),
            static_cast<uint64_t>(1), static_cast<uint64_t>(1), static_cast<uint64_t>(21000),
            bcos::Address("0xdead000000000000000000000000000000000011"), static_cast<uint64_t>(0),
            bcos::bytes{}, bcos::bytes{});
        typed.extraTransactionBytes.assign(preimage.begin(), preimage.end());
        auto typedTx = std::make_shared<bcostars::protocol::TransactionImpl>(
            [m = std::move(typed)]() mutable { return &m; });
        BOOST_CHECK(validator->verify(*typedTx) != TransactionStatus::Malformed);
    }
}

// chainId is validated from the SIGNED envelope, never the tars mirror: typed txs get no
// chainId=0 exemption (op-geth); only pre-EIP-155 legacy (no envelope chainId) is exempt.
BOOST_AUTO_TEST_CASE(testValidateChainIdFromEnvelope)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    auto keyPair = signatureImpl->generateKeyPair();
    auto groupId = "group_test_for_txpool";
    auto chainId = "chain_test_for_txpool";
    auto blockLimit = 10;
    auto fakeGateWay = std::make_shared<FakeGateWay>();
    auto faker = std::make_shared<TxPoolFixture>(
        keyPair->publicKey(), cryptoSuite, groupId, chainId, blockLimit, fakeGateWay, false, false);
    faker->init();
    auto ledger = faker->ledger();
    auto validator = faker->txpool()->txpoolConfig()->txValidator();

    // Seed the WEB3_CHAIN_ID system config to a known value (123).
    ledger->setSystemConfig(ledger::SYSTEM_KEY_WEB3_CHAIN_ID, "123");

    // A legacy EIP-155 tx whose envelope chainId (123) matches the config passes even when the
    // tars mirror lies (chainID="0"): the mirror is unauthenticated and must not be trusted.
    {
        auto tx = fakeWeb3Tx(cryptoSuite, "1", keyPair);
        auto txImpl = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx);
        // The fake preimage is pre-EIP-155 (6 fields, no chainId tail) → envelope nullopt →
        // exempt like an unprotected legacy tx.
        txImpl->mutableInner().data.chainID = "0";  // hostile mirror value
        auto result = task::syncWait(validator->validateChainId(*tx, ledger));
        BOOST_CHECK(result == TransactionStatus::None);
    }

    // A typed tx carries its chainId as RLP field 0 of the signed preimage. Envelope chainId
    // must match the config exactly — a "0" tars mirror must NOT provide an exemption (op-geth
    // modernSigner has no chainId=0 exemption for typed txs).
    auto makeTypedTx = [&](uint64_t envelopeChainId) {
        bcostars::Transaction inner{};
        inner.type = static_cast<tars::Char>(TransactionType::Web3Transaction);
        inner.web3TypedTxKind = static_cast<tars::Char>(0x02);  // EIP-1559
        // preimage = 0x02 || rlp([chainId, nonce, maxPriority, maxFee, gas, to, value, data, []])
        // codec::rlp::encode over the variadic list already emits the list header; only the
        // 0x02 type byte is prepended.
        bcos::bytes typed{0x02};
        bcos::codec::rlp::encode(typed, envelopeChainId, static_cast<uint64_t>(0),
            static_cast<uint64_t>(1), static_cast<uint64_t>(1), static_cast<uint64_t>(21000),
            bcos::Address("0xdead000000000000000000000000000000000011"), static_cast<uint64_t>(0),
            bcos::bytes{}, bcos::bytes{});
        inner.extraTransactionBytes.assign(typed.begin(), typed.end());
        return std::make_shared<bcostars::protocol::TransactionImpl>(
            [m = std::move(inner)]() mutable { return &m; });
    };
    {
        auto tx = makeTypedTx(123);
        auto txImpl = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx);
        txImpl->mutableInner().data.chainID = "0";  // hostile mirror must not bypass
        auto result = task::syncWait(validator->validateChainId(*tx, ledger));
        BOOST_CHECK(result == TransactionStatus::None);  // envelope 123 == config 123
    }
    {
        auto tx = makeTypedTx(456);
        auto txImpl = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx);
        txImpl->mutableInner().data.chainID = "123";  // mirror lies to match config
        auto result = task::syncWait(validator->validateChainId(*tx, ledger));
        BOOST_CHECK(result == TransactionStatus::InvalidChainId);  // envelope 456 != config
    }

    // A typed tx whose envelope has no parseable chainId (e.g. a 0x7e deposit envelope with the
    // kind mirror rewritten to 0x02) → nullopt + kind!=0 → strict InvalidChainId. This is the
    // mirror-rewrite bypass shape: the deposit gate keys on the envelope first byte, so this
    // forged tx must be rejected here even if it reached validateChainId.
    {
        auto tx = makeTypedTx(123);
        auto txImpl = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx);
        // Replace the typed preimage with a genuine 0x7e deposit envelope (8 fields, field 0 =
        // 32-byte sourceHash → exceeds the uint64 width gate → nullopt).
        bcostars::Transaction inner = txImpl->mutableInner();
        inner.web3TypedTxKind = static_cast<tars::Char>(0x02);  // mirror still claims EIP-1559
        bcos::bytes body;
        bcos::codec::rlp::encode(body,
            bcos::h256("0x6ab967dfdd3aa359031bef6965cca32ed9a21ea969f7aeee2e58817142a645d7"),
            bcos::Address("0xdead000000000000000000000000000000000011"),
            bcos::Address("0x4200000000000000000000000000000000000022"), bcos::bytes{},
            bcos::bytes{}, static_cast<uint64_t>(500000), static_cast<uint64_t>(0), bcos::bytes{});
        bcos::bytes envelope{0x7e};
        bcos::codec::rlp::encodeHeader(envelope, bcos::codec::rlp::Header{true, body.size()});
        envelope.insert(envelope.end(), body.begin(), body.end());
        inner.extraTransactionBytes.assign(envelope.begin(), envelope.end());
        auto forged = std::make_shared<bcostars::protocol::TransactionImpl>(
            [m = std::move(inner)]() mutable { return &m; });
        auto result = task::syncWait(validator->validateChainId(*forged, ledger));
        BOOST_CHECK(result == TransactionStatus::InvalidChainId);
    }

    // Legacy EIP-155 with a real chainId tail: the envelope chainId (123) is authoritative.
    {
        bcostars::Transaction inner{};
        inner.type = static_cast<tars::Char>(TransactionType::Web3Transaction);
        inner.web3TypedTxKind = static_cast<tars::Char>(0);  // legacy
        // preimage = rlp([nonce, gasPrice, gasLimit, to, value, data, chainId, 0, 0]) — 9 fields.
        auto makeLegacy = [&](uint64_t tailChainId) {
            bcos::bytes legacy;
            bcos::codec::rlp::encode(legacy, static_cast<uint64_t>(0), static_cast<uint64_t>(1),
                static_cast<uint64_t>(21000),
                bcos::Address("0xdead000000000000000000000000000000000011"),
                static_cast<uint64_t>(0), bcos::bytes{}, static_cast<uint64_t>(tailChainId),
                static_cast<uint64_t>(0), static_cast<uint64_t>(0));
            return legacy;
        };
        auto legacyBytes = makeLegacy(123);
        inner.extraTransactionBytes.assign(legacyBytes.begin(), legacyBytes.end());
        auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
            [m = std::move(inner)]() mutable { return &m; });
        auto result = task::syncWait(validator->validateChainId(*tx, ledger));
        BOOST_CHECK(result == TransactionStatus::None);  // envelope 123 == config 123
        auto legacyBytes456 = makeLegacy(456);
        inner.extraTransactionBytes.assign(legacyBytes456.begin(), legacyBytes456.end());
        auto tx2 = std::make_shared<bcostars::protocol::TransactionImpl>(
            [m = std::move(inner)]() mutable { return &m; });
        auto result2 = task::syncWait(validator->validateChainId(*tx2, ledger));
        BOOST_CHECK(result2 == TransactionStatus::InvalidChainId);  // envelope 456 != config
    }

    // Empty config value: no crash — the u256 parse rejects the tx (InvalidChainId). (The
    // config-absent branch — getSystemConfig nullopt → whole check skipped — is covered by the
    // pre-existing txpool tests; production always seeds WEB3_CHAIN_ID, default "0".)
    {
        ledger->setSystemConfig(ledger::SYSTEM_KEY_WEB3_CHAIN_ID, "");
        auto tx = makeTypedTx(123);
        auto result = task::syncWait(validator->validateChainId(*tx, ledger));
        BOOST_CHECK(result == TransactionStatus::InvalidChainId);
        ledger->setSystemConfig(ledger::SYSTEM_KEY_WEB3_CHAIN_ID, "123");
    }

    // Config value larger than uint64::max is a supported chainId (LedgerConfig u256 parse):
    // no envelope chainId can match → strict InvalidChainId, and no exception escapes.
    {
        ledger->setSystemConfig(
            ledger::SYSTEM_KEY_WEB3_CHAIN_ID, "18446744073709551617");  // 2^64+1
        auto tx = makeTypedTx(123);
        BOOST_CHECK_NO_THROW({
            auto result = task::syncWait(validator->validateChainId(*tx, ledger));
            BOOST_CHECK(result == TransactionStatus::InvalidChainId);
        });
        ledger->setSystemConfig(ledger::SYSTEM_KEY_WEB3_CHAIN_ID, "123");
    }
}

// EIP-2 low-s must be enforced on the P2P import path too (Transaction::verify), not only at
// RPC decode: without the symmetric gate a malleated (high-s) tx imported from a peer would
// pass admission.
BOOST_AUTO_TEST_CASE(testVerifyRejectsHighSOnP2P)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    auto keyPair = signatureImpl->generateKeyPair();

    // A valid Web3 tx with a canonical (low-s) signature.
    auto tx = fakeWeb3Tx(cryptoSuite, "42", keyPair);
    BOOST_CHECK_NO_THROW(tx->verify(*hashImpl, *signatureImpl));

    // Malleability flip: s' = n - s is high-s but recovers the same sender. Rebuild the tars
    // signature r(32)||s(32)||yParity(1) with the flipped s; verify() must reject it even
    // though the flipped signature still recovers the original sender.
    auto sigBytes = tx->signatureData().toBytes();
    BOOST_REQUIRE(sigBytes.size() == 65);
    bcos::u256 s = bcos::fromBigEndian<bcos::u256>(bcos::bytesConstRef(sigBytes.data() + 32, 32));
    BOOST_REQUIRE(s <= bcos::crypto::c_secp256k1nOver2);
    auto flipped = bcos::toBigEndian(bcos::crypto::c_secp256k1n - s);
    BOOST_REQUIRE(flipped.size() == 32);
    std::copy(flipped.begin(), flipped.end(), sigBytes.begin() + 32);
    auto txImpl = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx);
    BOOST_REQUIRE(txImpl);
    txImpl->setSignatureData(sigBytes);
    // Re-taint so verify() re-runs the full recovery + EIP-2 ladder (a prior verify()/forceSender
    // already cleared the taint; setSignatureData does not reset it).
    txImpl->clearSenderAndHash();

    BOOST_CHECK_THROW(tx->verify(*hashImpl, *signatureImpl), std::invalid_argument);
}

// Killer test for the reserved-type-byte admission gate: an envelope whose first byte is a
// reserved EIP-2718 marker (0x00 / 0x05 / 0x7f) must be Malformed even when the tars mirror
// claims a supported kind; a genuine 0x02 marker passes the gate (rejected later only by
// signature recovery, which is not Malformed). Deleting the gate leaves this test red.
BOOST_AUTO_TEST_CASE(testRejectReservedTypeBytesAtAdmission)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    auto validator = makeAdmissionValidator(cryptoSuite);

    auto makeTxWithEnvelope = [&](bcos::bytes envelope) {
        bcostars::Transaction inner{};
        inner.type = static_cast<tars::Char>(TransactionType::Web3Transaction);
        inner.web3TypedTxKind = static_cast<tars::Char>(0x02);  // mirror claims EIP-1559
        inner.extraTransactionBytes.assign(envelope.begin(), envelope.end());
        return std::make_shared<bcostars::protocol::TransactionImpl>(
            [m = std::move(inner)]() mutable { return &m; });
    };
    // Marker byte followed by an empty RLP list — the gate must reject on the marker alone,
    // before any decode.
    for (auto reserved : {0x00, 0x05, 0x7f})
    {
        auto tx = makeTxWithEnvelope(bcos::bytes{static_cast<bcos::byte>(reserved), 0xc0});
        BOOST_CHECK(validator->verify(*tx) == TransactionStatus::Malformed);
    }
    auto okTx = makeTxWithEnvelope(bcos::bytes{0x02, 0xc0});
    BOOST_CHECK(validator->verify(*okTx) != TransactionStatus::Malformed);
}

// Killer tests for the P2P verify() signature rungs: the 65-byte length check and the
// recovery-id (sig[64]) > 1 gate. Message-pinned so a wrong-source throw cannot satisfy them.
BOOST_AUTO_TEST_CASE(testVerifyRejectsMalformedSignatureShapes)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    auto keyPair = signatureImpl->generateKeyPair();

    // (a) 64-byte signature: reassembleWeb3RawTransaction (inside calculateHash) rejects the
    // explicitly-sized form before recovery.
    {
        auto tx = fakeWeb3Tx(cryptoSuite, "42", keyPair);
        BOOST_CHECK_NO_THROW(tx->verify(*hashImpl, *signatureImpl));
        auto sigBytes = tx->signatureData().toBytes();
        BOOST_REQUIRE_EQUAL(sigBytes.size(), 65u);
        sigBytes.pop_back();
        auto txImpl = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx);
        BOOST_REQUIRE(txImpl);
        txImpl->setSignatureData(sigBytes);
        txImpl->clearSenderAndHash();
        BOOST_CHECK_EXCEPTION(
            tx->verify(*hashImpl, *signatureImpl), std::invalid_argument, [](auto const& e) {
                return std::string(e.what()).find("expect 65") != std::string::npos;
            });
    }
    // (b) recovery id 2 (the v=29/30 envelope form): rejected on the legacy preimage too — the
    // tars signature byte is always recid/yParity (0/1) by construction, never the legacy v
    // encoding (27/28/35+).
    {
        auto tx = fakeWeb3Tx(cryptoSuite, "43", keyPair);
        auto sigBytes = tx->signatureData().toBytes();
        BOOST_REQUIRE_EQUAL(sigBytes.size(), 65u);
        sigBytes.back() = 2;
        auto txImpl = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx);
        BOOST_REQUIRE(txImpl);
        txImpl->setSignatureData(sigBytes);
        txImpl->clearSenderAndHash();
        BOOST_CHECK_EXCEPTION(
            tx->verify(*hashImpl, *signatureImpl), std::invalid_argument, [](auto const& e) {
                return std::string(e.what()).find("recovery id exceeds 1") != std::string::npos;
            });
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
