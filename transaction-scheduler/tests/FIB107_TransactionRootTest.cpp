/*
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
 * @file FIB107_TransactionRootTest.cpp
 * @brief Regression test for FIB-107: unhandled exceptions in calculateTransactionRoot
 */

#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-crypto/interfaces/crypto/CommonType.h"
#include "bcos-tars-protocol/protocol/BlockFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-transaction-scheduler/BaselineScheduler.h"
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::scheduler_v1;

class FIB107Fixture
{
public:
    FIB107Fixture()
      : cryptoSuite(std::make_shared<bcos::crypto::CryptoSuite>(
            std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr)),
        blockHeaderFactory(
            std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite)),
        transactionFactory(
            std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite)),
        receiptFactory(
            std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite)),
        blockFactory(std::make_shared<bcostars::protocol::BlockFactoryImpl>(
            cryptoSuite, blockHeaderFactory, transactionFactory, receiptFactory)),
        hashImpl(std::make_shared<bcos::crypto::Keccak256>())
    {}

    bcos::crypto::CryptoSuite::Ptr cryptoSuite;
    std::shared_ptr<bcostars::protocol::BlockHeaderFactoryImpl> blockHeaderFactory;
    std::shared_ptr<bcostars::protocol::TransactionFactoryImpl> transactionFactory;
    std::shared_ptr<bcostars::protocol::TransactionReceiptFactoryImpl> receiptFactory;
    std::shared_ptr<bcostars::protocol::BlockFactoryImpl> blockFactory;
    crypto::Hash::Ptr hashImpl;
};

BOOST_FIXTURE_TEST_SUITE(FIB107_TransactionRootTest, FIB107Fixture)

BOOST_AUTO_TEST_CASE(emptyBlockReturnsEmptyHash)
{
    // Block with 0 transactions and 0 metadata should return empty hash
    auto block = std::make_shared<bcostars::protocol::BlockImpl>();
    BOOST_CHECK_EQUAL(block->transactionsSize(), 0u);
    BOOST_CHECK_EQUAL(block->transactionsMetaDataSize(), 0u);

    auto result = calculateTransactionRoot(*block, *hashImpl);
    BOOST_CHECK_EQUAL(result, bcos::crypto::HashType{});
}

BOOST_AUTO_TEST_CASE(validBlockReturnsNonEmptyHash)
{
    // Block with valid transactions should return a non-empty merkle root
    auto block = std::make_shared<bcostars::protocol::BlockImpl>();
    bcos::bytes input;
    block->appendTransaction(
        transactionFactory->createTransaction(0, "to", input, "12345", 100, "chain", "group", 0));
    block->appendTransaction(
        transactionFactory->createTransaction(0, "to", input, "12346", 100, "chain", "group", 0));

    BOOST_CHECK_GT(block->transactionsSize(), 0u);

    auto result = calculateTransactionRoot(*block, *hashImpl);
    BOOST_CHECK_NE(result, bcos::crypto::HashType{});
}

BOOST_AUTO_TEST_CASE(malformedTransactionDoesNotPropagate)
{
    // A block containing a transaction with empty dataHash and extraTransactionHash
    // would cause TransactionImpl::hash() to throw. The fixed code should catch
    // this and return an empty hash instead of propagating the exception.
    auto block = std::make_shared<bcostars::protocol::BlockImpl>();

    // Create a malformed transaction with no hash data set
    auto malformedTx = std::make_shared<bcostars::protocol::TransactionImpl>();
    // The default-constructed TransactionImpl has empty dataHash and extraTransactionHash,
    // so calling hash() on it will throw EmptyTransactionHash.
    block->appendTransaction(malformedTx);

    BOOST_CHECK_GT(block->transactionsSize(), 0u);

    // Should not throw -- the exception is caught internally
    bcos::crypto::HashType result;
    BOOST_CHECK_NO_THROW(result = calculateTransactionRoot(*block, *hashImpl));
    BOOST_CHECK_EQUAL(result, bcos::crypto::HashType{});
}

BOOST_AUTO_TEST_SUITE_END()
