/**
 *  Copyright (C) 2025 FISCO BCOS.
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
 * @file Web3NonceCheckerTest.cpp
 * @author: MO NAN
 * @date 2025/10/14
 */

#include <boost/test/unit_test.hpp>
#include "bcos-framework/bcos-framework/testutils/faker/FakeTransaction.h"
#include "bcos-txpool/txpool/storage/Web3Transactions.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-crypto/signature/secp256k1/Secp256k1Crypto.h"

using namespace bcos;
using namespace bcos::txpool;
using namespace bcos::protocol;
using namespace bcos::crypto;

namespace bcos::test
{

// A unified fixture for both AccountTransactions and Web3Transactions tests
struct TransactionsFixture
{
    TransactionsFixture()
    {
        hashImpl = std::make_shared<Keccak256>();
        signatureImpl = std::make_shared<Secp256k1Crypto>();
        cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);

        // For AccountTransactions tests
        key = signatureImpl->generateKeyPair();

        // For Web3Transactions tests
        keyA = signatureImpl->generateKeyPair();
        keyB = signatureImpl->generateKeyPair();
        senderA = keyA->address(hashImpl).toRawString();
        senderB = keyB->address(hashImpl).toRawString();
        hexSenderA = toHex(senderA);
        hexSenderB = toHex(senderB);
    }

    CryptoSuite::Ptr cryptoSuite;
    std::shared_ptr<Keccak256> hashImpl;
    std::shared_ptr<Secp256k1Crypto> signatureImpl;

    // For AccountTransactions
    KeyPairInterface::UniquePtr key;
    Transaction::Ptr makeTx(std::string nonce)
    {
        return bcos::test::fakeWeb3Tx(cryptoSuite, std::move(nonce), key);
    }

    // For Web3Transactions
    KeyPairInterface::UniquePtr keyA;
    KeyPairInterface::UniquePtr keyB;
    std::string senderA;
    std::string senderB;
    std::string hexSenderA;
    std::string hexSenderB;
    Transaction::Ptr txA(std::string n) { return bcos::test::fakeWeb3Tx(cryptoSuite, std::move(n), keyA); }
    Transaction::Ptr txB(std::string n) { return bcos::test::fakeWeb3Tx(cryptoSuite, std::move(n), keyB); }
};

BOOST_FIXTURE_TEST_SUITE(AccountTransactionsTest, TransactionsFixture)

BOOST_AUTO_TEST_CASE(add_and_seal_contiguous)
{
    AccountTransactions acc;
    BOOST_CHECK_EQUAL(acc.add(makeTx("1")), false);
    BOOST_CHECK_EQUAL(acc.add(makeTx("2")), false);
    BOOST_CHECK_EQUAL(acc.add(makeTx("3")), false);

    std::vector<Transaction::Ptr> sealed;
    acc.seal(100, sealed);
    BOOST_CHECK_EQUAL(sealed.size(), 3);
}

BOOST_AUTO_TEST_CASE(pending_gap_and_remove_then_seal)
{
    AccountTransactions acc;
    BOOST_CHECK(!acc.add(makeTx("1")));
    BOOST_CHECK(!acc.add(makeTx("2")));
    BOOST_CHECK(!acc.add(makeTx("5")));
    BOOST_CHECK(!acc.add(makeTx("6")));

    std::vector<Transaction::Ptr> sealed;
    acc.seal(100, sealed);
    BOOST_CHECK_EQUAL(sealed.size(), 4);

    acc.remove(2);
    std::vector<Transaction::Ptr> sealed2;
    acc.seal(100, sealed2);
    BOOST_CHECK_EQUAL(sealed2.size(), 2);
}

BOOST_AUTO_TEST_CASE(replace_same_nonce_and_mark)
{
    AccountTransactions acc;
    BOOST_CHECK(!acc.add(makeTx("10")));
    BOOST_CHECK(acc.add(makeTx("10")));
    BOOST_CHECK(!acc.add(makeTx("11")));

    acc.mark(11);
    std::vector<Transaction::Ptr> sealed;
    acc.seal(100, sealed);
    BOOST_CHECK_EQUAL(sealed.size(), 1);
}

BOOST_AUTO_TEST_CASE(seal_limit_behavior)
{
    AccountTransactions acc;
    BOOST_CHECK(!acc.add(makeTx("1")));
    BOOST_CHECK(!acc.add(makeTx("2")));
    BOOST_CHECK(!acc.add(makeTx("3")));

    std::vector<Transaction::Ptr> sealed;
    acc.seal(2, sealed);
    BOOST_CHECK_EQUAL(sealed.size(), 3);
}

BOOST_AUTO_TEST_CASE(seal_zero_limit_noop)
{
    AccountTransactions acc;
    BOOST_CHECK(!acc.add(makeTx("1")));
    std::vector<Transaction::Ptr> sealed;
    acc.seal(0, sealed);
    BOOST_CHECK(sealed.empty());
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(Web3TransactionsTest, TransactionsFixture)

BOOST_AUTO_TEST_CASE(add_only_when_account_exists)
{
    Web3Transactions pool;
    BOOST_CHECK_EQUAL(pool.add(txA("1")), false);
    pool.remove(hexSenderA, 0);
    pool.mark(hexSenderA, 0);
}

BOOST_AUTO_TEST_CASE(seal_across_multiple_accounts)
{
    Web3Transactions pool;
    auto sealed = pool.seal(100);
    BOOST_CHECK(sealed.empty());
    pool.remove(hexSenderA, 10);
    pool.mark(hexSenderA, 10);
    auto sealed2 = pool.seal(1);
    BOOST_CHECK(sealed2.empty());
}

BOOST_AUTO_TEST_CASE(mixed_accounts_behavior_via_add_path)
{
    Web3Transactions pool;
    BOOST_CHECK_EQUAL(pool.add(txA("1")), false);
    BOOST_CHECK_EQUAL(pool.add(txB("1")), false);
    pool.remove(hexSenderB, 1);
    pool.mark(hexSenderB, 1);
    auto sealed = pool.seal(10);
    BOOST_CHECK(sealed.empty());
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace bcos::test
