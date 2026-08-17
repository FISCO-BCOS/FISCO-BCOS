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
 * @brief regression test for issue #5318: a transaction with a non-hex `to` must be
 *        rejected at txpool admission time, instead of being packed into a block and
 *        deterministically failing execution (which PBFT re-proposes forever,
 *        halting consensus)
 * @file Issue5318InvalidToTest.cpp
 */
#include "bcos-framework/bcos-framework/testutils/faker/FakeTransaction.h"
#include "bcos-framework/protocol/GlobalConfig.h"
#include "bcos-protocol/TransactionStatus.h"
#include "bcos-txpool/txpool/validator/TxValidator.h"
#include "test/unittests/txpool/TxPoolFixture.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>

#include <boost/test/unit_test.hpp>
using namespace bcos;
using namespace bcos::txpool;
using namespace bcos::protocol;

namespace bcos::test
{
namespace
{
protocol::Transaction::Ptr makeTxWithTo(std::string toField,
    protocol::TransactionType type = protocol::TransactionType::BCOSTransaction)
{
    auto tx = fakeInvalidateTransacton("issue5318", 0);
    auto txImpl = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx);
    txImpl->mutableInner().data.to = std::move(toField);
    txImpl->mutableInner().type = static_cast<tars::Char>(type);
    return tx;
}

TxValidator::Ptr makeValidator()
{
    return std::make_shared<TxValidator>(nullptr, nullptr, nullptr, "group0", "chain0");
}
}  // namespace

BOOST_FIXTURE_TEST_SUITE(Issue5318InvalidToTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(rejectNonHexTo)
{
    auto validator = makeValidator();

    // the exact repro of issue #5318: java-sdk copied a BFS `path` argument verbatim
    // into `to` on a Solidity chain
    BOOST_CHECK(validator->validateTransaction(*makeTxWithTo("HelloWorld")) ==
                TransactionStatus::Malformed);
    // correct length (40) but non-hex characters — would throw inside
    // boost::algorithm::unhex during execution
    BOOST_CHECK(validator->validateTransaction(*makeTxWithTo(std::string(40, 'z'))) ==
                TransactionStatus::Malformed);
    // hex but wrong length
    BOOST_CHECK(validator->validateTransaction(*makeTxWithTo(std::string(39, 'a'))) ==
                TransactionStatus::Malformed);
    BOOST_CHECK(validator->validateTransaction(*makeTxWithTo(std::string(41, 'a'))) ==
                TransactionStatus::Malformed);
    // 0x prefix alone is not a deployment marker (deployment uses an empty `to`)
    BOOST_CHECK(
        validator->validateTransaction(*makeTxWithTo("0x")) == TransactionStatus::Malformed);
    // valid hex address hidden behind an invalid prefix
    BOOST_CHECK(validator->validateTransaction(*makeTxWithTo("xx" + std::string(40, 'a'))) ==
                TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(acceptValidTo)
{
    auto validator = makeValidator();

    // deployment: empty `to`
    BOOST_CHECK(validator->validateTransaction(*makeTxWithTo("")) == TransactionStatus::None);
    // unprefixed 20-byte hex address (BCOS SDK form)
    BOOST_CHECK(validator->validateTransaction(*makeTxWithTo(std::string(40, 'a'))) ==
                TransactionStatus::None);
    BOOST_CHECK(validator->validateTransaction(*makeTxWithTo(
                    "0123456789abcdefABCDEF0123456789abcdefAB")) == TransactionStatus::None);
    // 0x-prefixed (Web3Transaction::takeToTarsTransaction writes hexPrefixed())
    BOOST_CHECK(validator->validateTransaction(*makeTxWithTo("0x" + std::string(40, 'a'))) ==
                TransactionStatus::None);
    BOOST_CHECK(validator->validateTransaction(*makeTxWithTo("0X" + std::string(40, 'A'))) ==
                TransactionStatus::None);
    // web3 transaction with prefixed address
    BOOST_CHECK(validator->validateTransaction(*makeTxWithTo("0x" + std::string(40, 'b'),
                    protocol::TransactionType::Web3Transaction)) == TransactionStatus::None);
}

BOOST_AUTO_TEST_CASE(enforceImportRejectsInvalidTo)
{
    // proposal verification path: a transaction missing from the local pool is imported
    // via MemoryStorage::enforceSubmitTransaction, which bypasses validateTransaction() —
    // a malicious/unfixed leader could otherwise smuggle a malformed `to` into a block
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    bcos::crypto::KeyPairInterface::Ptr keyPair = signatureImpl->generateKeyPair();
    auto fakeGateWay = std::make_shared<FakeGateWay>();
    auto faker = std::make_shared<TxPoolFixture>(keyPair->publicKey(), cryptoSuite,
        "group_test_for_txpool", "chain_test_for_txpool", 10, fakeGateWay, false, false);
    faker->init();
    auto txpoolStorage = faker->txpool()->txpoolStorage();

    // build transactions that pass the ledger nonce / blockLimit checks of the enforce
    // path, so a rejection can only come from the `to` validation under test
    auto blockLimit = faker->ledger()->blockNumber() + 5;
    auto makeEnforceTx = [&](std::string_view toField, const std::string& nonce) {
        return fakeTransaction(cryptoSuite, keyPair, toField, asBytes("issue5318"), nonce,
            blockLimit, "chain_test_for_txpool", "group_test_for_txpool");
    };

    // positive control: a valid `to` is imported through the same path
    auto validTxs = std::make_shared<protocol::Transactions>();
    validTxs->emplace_back(makeEnforceTx(std::string(40, 'a'), "10086100861"));
    BOOST_CHECK(txpoolStorage->batchVerifyAndSubmitTransaction(nullptr, validTxs));

    auto poisonTxs = std::make_shared<protocol::Transactions>();
    poisonTxs->emplace_back(makeEnforceTx("HelloWorld", "10086100862"));
    BOOST_CHECK(!txpoolStorage->batchVerifyAndSubmitTransaction(nullptr, poisonTxs));

    txpoolStorage->clear();
}

BOOST_AUTO_TEST_CASE(wasmChainSkipsAddressCheck)
{
    auto validator = makeValidator();

    // on a WASM chain `to` is a BFS path, not a hex address — the check must not apply
    g_BCOSConfig.setIsWasm(true);
    BOOST_CHECK(validator->validateTransaction(*makeTxWithTo("/apps/HelloWorld")) ==
                TransactionStatus::None);
    g_BCOSConfig.setIsWasm(false);
    BOOST_CHECK(validator->validateTransaction(*makeTxWithTo("/apps/HelloWorld")) ==
                TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
