/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "bcos-tars-protocol/protocol/TransactionSubmitResultImpl.h"
#include <boost/test/unit_test.hpp>

using namespace bcostars::protocol;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(TarsTransactionSubmitResultImplTest)

BOOST_AUTO_TEST_CASE(defaultsAreZeroed)
{
    TransactionSubmitResultImpl result;
    BOOST_CHECK_EQUAL(result.status(), 0U);
    BOOST_CHECK_EQUAL(result.transactionIndex(), 0);
    BOOST_CHECK_EQUAL(result.type(), 0);
    BOOST_CHECK(result.sender().empty());
    BOOST_CHECK(result.to().empty());
    // An unset 32-byte hash field decodes back to the zero hash.
    BOOST_CHECK_EQUAL(result.txHash(), bcos::crypto::HashType{});
    BOOST_CHECK_EQUAL(result.blockHash(), bcos::crypto::HashType{});
}

BOOST_AUTO_TEST_CASE(scalarSettersRoundTrip)
{
    TransactionSubmitResultImpl result;
    result.setStatus(15);
    result.setTransactionIndex(9);
    result.setType(2);
    result.setSender("0xabc");
    result.setTo("0xdef");
    result.setNonce("0x2a");

    BOOST_CHECK_EQUAL(result.status(), 15U);
    BOOST_CHECK_EQUAL(result.transactionIndex(), 9);
    BOOST_CHECK_EQUAL(result.type(), 2);
    BOOST_CHECK_EQUAL(result.sender(), "0xabc");
    BOOST_CHECK_EQUAL(result.to(), "0xdef");
    BOOST_CHECK_EQUAL(result.nonce(), "0x2a");
}

BOOST_AUTO_TEST_CASE(hashSettersRoundTripThroughTarsBytes)
{
    TransactionSubmitResultImpl result;
    bcos::crypto::HashType txHash;
    txHash[0] = 0xAB;
    txHash[31] = 0xCD;
    result.setTxHash(txHash);
    BOOST_CHECK_EQUAL(result.txHash(), txHash);

    bcos::crypto::HashType blockHash;
    blockHash[1] = 0x12;
    result.setBlockHash(blockHash);
    BOOST_CHECK_EQUAL(result.blockHash(), blockHash);
}

BOOST_AUTO_TEST_CASE(innerReflectsMutations)
{
    TransactionSubmitResultImpl result;
    result.setStatus(7);
    // inner() exposes the underlying tars struct — must reflect the setter.
    BOOST_CHECK_EQUAL(result.inner().status, 7);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
