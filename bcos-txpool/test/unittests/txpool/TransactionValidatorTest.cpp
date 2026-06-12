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
#include "bcos-executor/src/CallParameters.h"
#include "bcos-framework/bcos-framework/testutils/faker/FakeTransaction.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/txpool/Constant.h"
#include "bcos-protocol/TransactionStatus.h"
#include "bcos-rpc/web3jsonrpc/model/Web3Transaction.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-transaction-executor/gas/EthTxGasSettlement.h"

#include "bcos-task/Wait.h"
#include "test/unittests/txpool/TxPoolFixture.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/protocol/CommonError.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/exception/diagnostic_information.hpp>
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

BOOST_AUTO_TEST_CASE(testValidateEip7623GasFloor)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    auto keyPair = signatureImpl->generateKeyPair();
    auto fakeGateWay = std::make_shared<FakeGateWay>();
    auto faker = std::make_shared<TxPoolFixture>(
        keyPair->publicKey(), cryptoSuite, "groupId", "chainId", 100000000, fakeGateWay, false);
    faker->init();

    auto txpoolConfig = faker->txpool()->txpoolConfig();
    auto ledger = faker->ledger();
    auto const eoaKey = cryptoSuite->signatureImpl()->generateKeyPair();

    bcos::bytes mixedCalldata(100);
    std::fill(mixedCalldata.begin(), mixedCalldata.begin() + 50, 0x00);
    std::fill(mixedCalldata.begin() + 50, mixedCalldata.end(), 0x42);
    std::string mixedInput(mixedCalldata.begin(), mixedCalldata.end());

    // Prague inactive: low gasLimit is not rejected by EIP-7623 floor check.
    {
        ledger->setTestFeatures({});
        auto tx = fakeWeb3Tx(cryptoSuite, "0", eoaKey, mixedInput);
        auto txImpl = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx);
        txImpl->mutableInner().data.gasLimit = 22000;
        auto result =
            task::syncWait(txpoolConfig->txValidator()->validateEip7623GasFloor(*tx, ledger));
        BOOST_CHECK(result == TransactionStatus::None);
    }

    bcos::ledger::Features pragueFeatures;
    pragueFeatures.set(bcos::ledger::Features::Flag::feature_evm_prague);
    ledger->setTestFeatures(pragueFeatures);

    // gasLimit below EIP-7623 floor (23500 for 100-byte mixed calldata) → Malformed.
    {
        auto tx = fakeWeb3Tx(cryptoSuite, "1", eoaKey, mixedInput);
        auto txImpl = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx);
        txImpl->mutableInner().data.gasLimit = 23499;
        auto result =
            task::syncWait(txpoolConfig->txValidator()->validateEip7623GasFloor(*tx, ledger));
        BOOST_CHECK(result == TransactionStatus::Malformed);
    }

    // gasLimit at floor → accepted.
    {
        auto tx = fakeWeb3Tx(cryptoSuite, "2", eoaKey, mixedInput);
        auto txImpl = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx);
        txImpl->mutableInner().data.gasLimit = 23500;
        auto result =
            task::syncWait(txpoolConfig->txValidator()->validateEip7623GasFloor(*tx, ledger));
        BOOST_CHECK(result == TransactionStatus::None);
    }

    // EIP-2930 access list increases gasLimitMinimum (2400 + 2*1900 = 6200 above base).
    {
        auto tx = fakeWeb3Tx(cryptoSuite, "3", eoaKey, "x");
        auto txImpl = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx);
        txImpl->mutableInner().web3TypedTxKind =
            static_cast<tars::Char>(bcos::rpc::TransactionType::EIP2930);
        bcostars::Web3AccessListEntry entry;
        entry.account = eoaKey->address(cryptoSuite->hashImpl()).hexPrefixed().substr(2);
        entry.storageKeys.emplace_back(std::vector<tars::Char>(32, 0x01));
        entry.storageKeys.emplace_back(std::vector<tars::Char>(32, 0x02));
        txImpl->mutableInner().data.accessList.emplace_back(std::move(entry));

        bcos::bytes const input = bcos::asBytes("x");
        evmc_message msg{};
        msg.kind = EVMC_CALL;
        msg.input_data = input.data();
        msg.input_size = input.size();
        executor::Eip2930AccessList list{
            {eoaKey->address(cryptoSuite->hashImpl()), {h256(0x01), h256(0x02)}}};
        auto const intrinsic =
            executor_v1::gas::computeTxIntrinsicGas(msg, std::addressof(list), 1);
        auto const minGas = intrinsic.gasLimitMinimum();
        BOOST_REQUIRE_GT(minGas, 21000);

        txImpl->mutableInner().data.gasLimit = minGas - 1;
        auto result =
            task::syncWait(txpoolConfig->txValidator()->validateEip7623GasFloor(*tx, ledger));
        BOOST_CHECK(result == TransactionStatus::Malformed);

        txImpl->mutableInner().data.gasLimit = minGas;
        result = task::syncWait(txpoolConfig->txValidator()->validateEip7623GasFloor(*tx, ledger));
        BOOST_CHECK(result == TransactionStatus::None);
    }

    // Non-Web3 transactions skip the floor check.
    {
        auto tx = fakeInvalidateTransacton("legacyInput", 0);
        auto txImpl = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx);
        txImpl->mutableInner().data.gasLimit = 21000;
        auto result =
            task::syncWait(txpoolConfig->txValidator()->validateEip7623GasFloor(*tx, ledger));
        BOOST_CHECK(result == TransactionStatus::None);
    }

    std::cout << "#### testValidateEip7623GasFloor finish" << std::endl;
}

BOOST_AUTO_TEST_CASE(testSubmitEip7623GasFloorRejected)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    auto keyPair = signatureImpl->generateKeyPair();
    auto fakeGateWay = std::make_shared<FakeGateWay>();
    auto faker = std::make_shared<TxPoolFixture>(
        keyPair->publicKey(), cryptoSuite, "groupId", "chainId", 100000000, fakeGateWay, false);
    faker->init();

    auto txpool = faker->txpool();
    auto txpoolStorage = txpool->txpoolStorage();
    auto ledger = faker->ledger();
    ledger->setSystemConfig(ledger::SYSTEM_KEY_TX_GAS_PRICE, "0");

    bcos::ledger::Features pragueFeatures;
    pragueFeatures.set(bcos::ledger::Features::Flag::feature_evm_prague);
    ledger->setTestFeatures(pragueFeatures);

    auto const eoaKey = cryptoSuite->signatureImpl()->generateKeyPair();
    faker->ledger()->initEoaContext(eoaKey->address(cryptoSuite->hashImpl()).hex(), "0");
    StorageState state{.nonce = "0", .balance = "1000000000000000000"};
    faker->ledger()->setStorageState(
        eoaKey->address(cryptoSuite->hashImpl()).hex(), std::move(state));

    bcos::bytes mixedCalldata(100);
    std::fill(mixedCalldata.begin(), mixedCalldata.begin() + 50, 0x00);
    std::fill(mixedCalldata.begin() + 50, mixedCalldata.end(), 0x42);
    std::string mixedInput(mixedCalldata.begin(), mixedCalldata.end());

    auto const poolSizeBefore = txpoolStorage->size();
    auto tx = fakeWeb3Tx(cryptoSuite, "0", eoaKey, mixedInput);
    auto txImpl = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx);
    txImpl->mutableInner().data.gasLimit = 23499;

    bool threw = false;
    try
    {
        task::syncWait(txpool->submitTransaction(tx, false));
    }
    catch (bcos::Error const& e)
    {
        threw = true;
        BOOST_CHECK_EQUAL(e.errorCode(), (int32_t)TransactionStatus::Malformed);
    }
    BOOST_CHECK(threw);
    BOOST_CHECK_EQUAL(txpoolStorage->size(), poolSizeBefore);

    std::cout << "#### testSubmitEip7623GasFloorRejected finish" << std::endl;
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos