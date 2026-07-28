/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "bcos-protocol/TransactionSubmitResultFactoryImpl.h"
#include "bcos-protocol/TransactionSubmitResultImpl.h"
#include <boost/test/unit_test.hpp>

using namespace bcos::protocol;

BOOST_AUTO_TEST_SUITE(TransactionSubmitResultImplTest)

BOOST_AUTO_TEST_CASE(defaultsMatchFactoryContract)
{
    TransactionSubmitResultImpl r;
    BOOST_CHECK_EQUAL(r.status(), static_cast<uint32_t>(TransactionStatus::None));
    BOOST_CHECK_EQUAL(r.transactionIndex(), 0);
    BOOST_CHECK(r.sender().empty());
    BOOST_CHECK(r.to().empty());
    BOOST_CHECK_EQUAL(r.type(), 0);
    BOOST_CHECK(!r.transactionReceipt());
}

BOOST_AUTO_TEST_CASE(settersAndGettersRoundTrip)
{
    TransactionSubmitResultImpl r;

    r.setStatus(42);
    BOOST_CHECK_EQUAL(r.status(), 42U);

    bcos::crypto::HashType txHash;
    txHash[0] = 0x11;
    r.setTxHash(txHash);
    BOOST_CHECK_EQUAL(r.txHash(), txHash);

    bcos::crypto::HashType blockHash;
    blockHash[31] = 0xAA;
    r.setBlockHash(blockHash);
    BOOST_CHECK_EQUAL(r.blockHash(), blockHash);

    r.setTransactionIndex(7);
    BOOST_CHECK_EQUAL(r.transactionIndex(), 7);

    NonceType nonce("0x1234");
    r.setNonce(nonce);
    BOOST_CHECK_EQUAL(r.nonce(), nonce);

    r.setSender("sender_addr");
    BOOST_CHECK_EQUAL(r.sender(), "sender_addr");

    r.setTo("to_addr");
    BOOST_CHECK_EQUAL(r.to(), "to_addr");

    r.setType(3);
    BOOST_CHECK_EQUAL(r.type(), 3);

    // Receipt: round-trip via setter that takes ConstPtr.
    TransactionReceipt::ConstPtr empty;
    r.setTransactionReceipt(empty);
    BOOST_CHECK(!r.transactionReceipt());
}

BOOST_AUTO_TEST_CASE(factoryProducesIndependentInstances)
{
    TransactionSubmitResultFactoryImpl factory;
    auto first = factory.createTxSubmitResult();
    auto second = factory.createTxSubmitResult();

    BOOST_REQUIRE(first);
    BOOST_REQUIRE(second);
    BOOST_CHECK(first.get() != second.get());

    first->setStatus(1);
    BOOST_CHECK_EQUAL(first->status(), 1U);
    BOOST_CHECK_EQUAL(second->status(), static_cast<uint32_t>(TransactionStatus::None));
}

BOOST_AUTO_TEST_SUITE_END()
