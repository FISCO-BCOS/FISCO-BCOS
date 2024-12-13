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
 * @file TransactionValiditorTest.cpp
 * @author: asherli
 * @date 2024-12-11
 */
#include "bcos-crypto/interfaces/crypto/KeyPairInterface.h"
#include "bcos-framework/bcos-framework/testutils/faker/FakeTransaction.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/txpool/Constant.h"
#include "bcos-protocol/TransactionStatus.h"
#include <txpool/validator/TransactionValidator.h>

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
        blockData[ledger->blockNumber() - blockLimit + 1]->transaction(0)->nonce();
    auto tx = fakeWeb3Tx(cryptoSuite, duplicatedNonce, eoaKey);
    // bcos nonce not effect web3 nonce

    u256 fakeNonce = u256(duplicatedNonce);
    faker->ledger()->initEoaContext(
        eoaKey->address(cryptoSuite->hashImpl()).hex(), fakeNonce.convert_to<std::string>());
    auto result = TransactionValidator::ValidateTransaction(tx);
    BOOST_CHECK(result == TransactionStatus::None);

    std::string outRangeValue = "0x" + std::string(TRANSACTION_VALUE_MAX_LENGTH, '1');
    std::string inputStr = "testTransactionValidatorTx";

    auto txError = fakeInvalidateTransacton(inputStr, outRangeValue);

    auto resultError = TransactionValidator::ValidateTransaction(txError);
    BOOST_CHECK(resultError == TransactionStatus::OverFlowValue);
    // fake input str large size transaction
    auto inputStrLarge = "0x" + std::string(MAX_INITCODE_SIZE, '1');
    tx = fakeWeb3Tx(cryptoSuite, duplicatedNonce, eoaKey, inputStrLarge);
    auto resultLarge = TransactionValidator::ValidateTransaction(tx);
    BOOST_CHECK(resultLarge == TransactionStatus::MaxInitCodeSizeExceeded);

    // check with state
    // auto resultWithStateNoAccount = TransactionValidator::ValidateTransactionWithState(tx, ledger);
    // BOOST_CHECK(resultWithStateNoAccount == TransactionStatus::NoAccount);

    std::optional<storage::Entry> codeHashOp = storage::Entry();
    codeHashOp->set(asBytes(""));
    ledger->setStorageAt(tx->sender(), bcos::ledger::ACCOUNT_TABLE_FIELDS::CODE_HASH, codeHashOp);


    std::optional<storage::Entry> balanceOp = storage::Entry();
    balanceOp->set(asBytes("0x123"));
    ledger->setStorageAt(tx->sender(), bcos::ledger::ACCOUNT_TABLE_FIELDS::BALANCE, balanceOp);

    auto resultWithState = task::syncWait(TransactionValidator::ValidateTransactionWithState(tx, ledger));
    BOOST_CHECK(resultWithState == TransactionStatus::None);

    // NoEOA
    auto const eoaKeyNew = cryptoSuite->signatureImpl()->generateKeyPair();
    // case3: transaction with invalid nonce(conflict with the ledger nonce)
    auto txNoEoa = fakeWeb3Tx(cryptoSuite, duplicatedNonce, eoaKeyNew);

    std::optional<storage::Entry> codeHashOpNew = storage::Entry();
    codeHashOpNew->set(asBytes("0x012345"));
    ledger->setStorageAt(
        txNoEoa->sender(), bcos::ledger::ACCOUNT_TABLE_FIELDS::CODE_HASH, codeHashOpNew);

    auto resultWithStateNoEOAAccount =
        task::syncWait(TransactionValidator::ValidateTransactionWithState(txNoEoa, ledger));
    BOOST_CHECK(resultWithStateNoEOAAccount == TransactionStatus::SenderNoEOA);

    std::string value = "0x1234567";
    auto txNoEoughtValue = fakeInvalidateTransacton(inputStr, value);
    ledger->setStorageAt(txNoEoughtValue->sender(), bcos::ledger::ACCOUNT_TABLE_FIELDS::BALANCE, balanceOp);
    auto resultWithStateNoEnoughBalance =
        task::syncWait(TransactionValidator::ValidateTransactionWithState(txNoEoughtValue, ledger));
    BOOST_CHECK(resultWithStateNoEnoughBalance == TransactionStatus::InsufficientFunds);

    txpool->txpoolStorage()->clear();
    std::cout << "#### testTransactionValidator finish" << std::endl;
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos