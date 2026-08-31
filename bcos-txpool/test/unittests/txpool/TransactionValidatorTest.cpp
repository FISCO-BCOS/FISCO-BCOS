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
#include "bcos-framework/bcos-framework/engine/RawTransactionDispatch.h"
#include "bcos-framework/bcos-framework/testutils/faker/FakeTransaction.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/txpool/Constant.h"
#include "bcos-protocol/TransactionStatus.h"
#include <bcos-codec/rlp/RLPEncode.h>

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

// Finding BO: a blob (0x03) tx reaching the P2P-sync chainId gate must be refused with
// BlobTxNotAllowed — the InvalidChainId previously shared with the Deposit/Malformed/
// chainId-mismatch arms made pool logs blame the chainId for a type-policy refusal.
// The fixture is an independently assembled EIP-4844 wire envelope, so it exercises the
// dispatch gate itself: deleting the gate flips this test red.
BOOST_AUTO_TEST_CASE(testBlobTxRejectedWithDedicatedStatus)
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
    auto ledger = faker->ledger();

    // Independently assembled EIP-4844 wire envelope: 0x03 || rlp([chainId, nonce,
    // maxPriorityFeePerGas, maxFeePerGas, gas, to, value, data, accessList,
    // maxFeePerBlobGas, blobVersionedHashes, yParity, r, s]).
    namespace codec_rlp = bcos::codec::rlp;
    auto const eoaKey = cryptoSuite->signatureImpl()->generateKeyPair();
    bcos::bytes fields;
    codec_rlp::encode(fields, static_cast<uint64_t>(1));
    codec_rlp::encode(fields, static_cast<uint64_t>(0));
    codec_rlp::encode(fields, static_cast<uint64_t>(1));
    codec_rlp::encode(fields, static_cast<uint64_t>(2));
    codec_rlp::encode(fields, static_cast<uint64_t>(21000));
    codec_rlp::encode(fields, eoaKey->address(hashImpl).asBytes());
    codec_rlp::encode(fields, static_cast<uint64_t>(0));
    codec_rlp::encode(fields, bcos::bytes{});
    bcos::bytes emptyList;
    codec_rlp::encodeHeader(emptyList, {.isList = true, .payloadLength = 0});
    fields.insert(fields.end(), emptyList.begin(), emptyList.end());  // accessList
    codec_rlp::encode(fields, static_cast<uint64_t>(1));              // maxFeePerBlobGas
    fields.insert(fields.end(), emptyList.begin(), emptyList.end());  // blobVersionedHashes
    codec_rlp::encode(fields, static_cast<uint64_t>(1));              // yParity
    codec_rlp::encode(fields, bcos::bytes(32, 0x01));                 // r
    codec_rlp::encode(fields, bcos::bytes(32, 0x02));                 // s
    bcos::bytes wire;
    wire.push_back(0x03);
    codec_rlp::encodeHeader(wire, {.isList = true, .payloadLength = fields.size()});
    wire.insert(wire.end(), fields.begin(), fields.end());

    // Sanity: the fixture keys the dispatch table on 0x03 independent of the gate.
    BOOST_CHECK(bcos::engine::dispatchRawTransaction(bcos::ref(wire)) ==
                bcos::engine::RawTransactionKind::Blob);

    bcostars::Transaction transaction;
    transaction.type = static_cast<tars::Char>(bcos::protocol::TransactionType::Web3Transaction);
    transaction.extraTransactionBytes.assign(wire.begin(), wire.end());
    auto blobTx = std::make_shared<bcostars::protocol::TransactionImpl>(
        [m_transaction = std::move(transaction)]() mutable { return &m_transaction; });

    auto result = task::syncWait(txpoolConfig->txValidator()->validateChainId(*blobTx, ledger));
    BOOST_CHECK(result == TransactionStatus::BlobTxNotAllowed);
}

BOOST_AUTO_TEST_CASE(testValidateChainIdTypedAdmitAndUnsupported)
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
    auto ledger = faker->ledger();
    ledger->setSystemConfig(std::string(bcos::ledger::SYSTEM_KEY_WEB3_CHAIN_ID), "1");

    namespace codec_rlp = bcos::codec::rlp;
    auto makeTypedChainIdWire = [](uint8_t type, uint64_t envelopeChainId) {
        bcos::bytes fields;
        codec_rlp::encode(fields, envelopeChainId);
        bcos::bytes wire;
        wire.push_back(type);
        codec_rlp::encodeHeader(wire, {.isList = true, .payloadLength = fields.size()});
        wire.insert(wire.end(), fields.begin(), fields.end());
        return wire;
    };
    auto asWeb3Tx = [](bcos::bytes const& wire) {
        bcostars::Transaction transaction;
        transaction.type =
            static_cast<tars::Char>(bcos::protocol::TransactionType::Web3Transaction);
        transaction.extraTransactionBytes.assign(wire.begin(), wire.end());
        return std::make_shared<bcostars::protocol::TransactionImpl>(
            [m_transaction = std::move(transaction)]() mutable { return &m_transaction; });
    };

    // 0x01 / 0x02 / 0x04 with inner-list field0 matching the node chainId are admitted.
    for (uint8_t const type : {uint8_t{0x01}, uint8_t{0x02}, uint8_t{0x04}})
    {
        auto const wire = makeTypedChainIdWire(type, 1);
        BOOST_CHECK(bcos::engine::dispatchRawTransaction(bcos::ref(wire)) !=
                    bcos::engine::RawTransactionKind::Unsupported);
        auto result =
            task::syncWait(txpoolConfig->txValidator()->validateChainId(*asWeb3Tx(wire), ledger));
        BOOST_CHECK(result == TransactionStatus::None);
    }

    // Matching type, mismatched chainId.
    {
        auto const wire = makeTypedChainIdWire(0x02, 999);
        auto result =
            task::syncWait(txpoolConfig->txValidator()->validateChainId(*asWeb3Tx(wire), ledger));
        BOOST_CHECK(result == TransactionStatus::InvalidChainId);
    }

    // R3 #2: legacy SEALED envelope at real signature width (32-byte r/s, v=38 = chainId 1)
    // must be admitted — validateChainId was previously driven only with typed envelopes,
    // and inline-width classifier fixtures hide the emptySeen cursor defect that misreads
    // real-width tails (Malformed instead of Protected).
    {
        auto makeLegacySealedWire = []() {
            bcos::bytes items;
            for (int i = 0; i < 6; ++i)
            {
                codec_rlp::encode(items, static_cast<uint64_t>(0));
            }
            codec_rlp::encode(items, static_cast<uint64_t>(38));  // v: chainId 1, parity 1
            items.push_back(0xa0);                                // 32-byte r
            items.insert(items.end(), 32, 0x11);
            items[items.size() - 32] = 0xc1;  // first payload byte >= 0xc0: a list header if the
                                              // emptySeen walk starts mid-r (pre-fix)
            items.push_back(0xa0);            // 32-byte s
            items.insert(items.end(), 32, 0x22);
            bcos::bytes wire;
            codec_rlp::encodeHeader(wire, {.isList = true, .payloadLength = items.size()});
            wire.insert(wire.end(), items.begin(), items.end());
            return wire;
        };
        auto const wire = makeLegacySealedWire();
        BOOST_CHECK(bcos::engine::dispatchRawTransaction(bcos::ref(wire)) !=
                    bcos::engine::RawTransactionKind::Unsupported);
        auto result =
            task::syncWait(txpoolConfig->txValidator()->validateChainId(*asWeb3Tx(wire), ledger));
        BOOST_CHECK(result == TransactionStatus::None);
    }

    // Unsupported type byte (0x05) and empty extra are Malformed, not InvalidChainId.
    {
        bcos::bytes unsupported{0x05};
        auto result = task::syncWait(
            txpoolConfig->txValidator()->validateChainId(*asWeb3Tx(unsupported), ledger));
        BOOST_CHECK(result == TransactionStatus::Malformed);
    }
    {
        auto result = task::syncWait(
            txpoolConfig->txValidator()->validateChainId(*asWeb3Tx(bcos::bytes{}), ledger));
        BOOST_CHECK(result == TransactionStatus::Malformed);
    }
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

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
