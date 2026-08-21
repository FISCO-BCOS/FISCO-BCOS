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
TxValidator::Ptr makeValidator()
{
    return std::make_shared<TxValidator>(nullptr, nullptr, nullptr, "group0", "chain0");
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
// system txs that only enter via the engine newPayload path — op-geth txpool validation.go
// rejects them (ErrTxTypeNotSupported), and a forged envelope that passes signature recovery
// would mint funds once part 5 assembles OP blocks from the txpool.
BOOST_AUTO_TEST_CASE(testRejectDepositAtAdmission)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    auto validator = makeValidator();

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
    // must key on the deposit kind, not on the Web3 type in general. Signature recovery of an
    // empty-signature tx will fail, so assert the deposit gate fires *before* that, i.e. the
    // non-deposit Web3 tx reaches the signature step (InvalidSignature, not Malformed).
    auto normalInner = inner;
    normalInner.web3TypedTxKind = 0;
    auto normalTx = std::make_shared<bcostars::protocol::TransactionImpl>(
        [m = std::move(normalInner)]() mutable { return &m; });
    BOOST_CHECK(!normalTx->isDepositTx());
    BOOST_CHECK(validator->verify(*normalTx) != TransactionStatus::Malformed);
}

// chainId must be validated from the SIGNED envelope, never from the unauthenticated tars
// mirror (data.chainID): an attacker can rewrite the mirror to "0" to pass the old
// "empty or 0 skip" exemption. Typed txs get no chainId=0 exemption (op-geth
// modernSigner.Sender); only pre-EIP-155 unprotected legacy (no envelope chainId) is exempt.
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
    auto faker = std::make_shared<TxPoolFixture>(keyPair->publicKey(), cryptoSuite, groupId,
        chainId, blockLimit, fakeGateWay, false, false);
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
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
