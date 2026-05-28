/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "bcos-tars-protocol/protocol/ExecutionMessageImpl.h"
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcostars::protocol;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(ExecutionMessageImplTest)

BOOST_AUTO_TEST_CASE(factoryProducesUsableInstance)
{
    ExecutionMessageFactoryImpl factory;
    auto msg = factory.createExecutionMessage();
    BOOST_REQUIRE(msg);
}

BOOST_AUTO_TEST_CASE(scalarFieldsRoundTrip)
{
    ExecutionMessageFactoryImpl factory;
    auto msg = factory.createExecutionMessage();

    msg->setType(bcos::protocol::ExecutionMessage::MESSAGE);
    msg->setContextID(101);
    msg->setSeq(7);
    msg->setOrigin("origin-addr");
    msg->setFrom("from-addr");
    msg->setTo("to-addr");
    msg->setABI("abi-string");
    msg->setDepth(3);
    msg->setGasLimit(900000);
    msg->setGasAvailable(800000);
    msg->setStatus(0);
    msg->setEvmStatus(1);
    msg->setMessage("ok");
    msg->setNewEVMContractAddress("new-contract");

    BOOST_CHECK_EQUAL(msg->type(), bcos::protocol::ExecutionMessage::MESSAGE);
    BOOST_CHECK_EQUAL(msg->contextID(), 101);
    BOOST_CHECK_EQUAL(msg->seq(), 7);
    BOOST_CHECK_EQUAL(msg->origin(), "origin-addr");
    BOOST_CHECK_EQUAL(msg->from(), "from-addr");
    BOOST_CHECK_EQUAL(msg->to(), "to-addr");
    BOOST_CHECK_EQUAL(msg->abi(), "abi-string");
    BOOST_CHECK_EQUAL(msg->depth(), 3);
    BOOST_CHECK_EQUAL(msg->gasLimit(), 900000);
    BOOST_CHECK_EQUAL(msg->gasAvailable(), 800000);
    BOOST_CHECK_EQUAL(msg->status(), 0);
    BOOST_CHECK_EQUAL(msg->evmStatus(), 1);
    BOOST_CHECK_EQUAL(msg->message(), "ok");
    BOOST_CHECK_EQUAL(msg->newEVMContractAddress(), "new-contract");
}

BOOST_AUTO_TEST_CASE(boolFlagsRoundTrip)
{
    ExecutionMessageFactoryImpl factory;
    auto msg = factory.createExecutionMessage();

    msg->setStaticCall(true);
    msg->setCreate(true);
    msg->setInternalCreate(true);
    msg->setInternalCall(true);
    msg->setDelegateCall(true);

    BOOST_CHECK(msg->staticCall());
    BOOST_CHECK(msg->create());
    BOOST_CHECK(msg->internalCreate());
    BOOST_CHECK(msg->internalCall());
    BOOST_CHECK(msg->delegateCall());
}

BOOST_AUTO_TEST_CASE(dataAndKeyLocksRoundTrip)
{
    ExecutionMessageFactoryImpl factory;
    auto msg = factory.createExecutionMessage();

    bcos::bytes data{0x01, 0x02, 0x03};
    msg->setData(data);
    BOOST_CHECK(msg->data().toBytes() == data);

    std::vector<std::string> keyLocks{"lockA", "lockB"};
    msg->setKeyLocks(keyLocks);
    BOOST_REQUIRE_EQUAL(msg->keyLocks().size(), 2U);
    BOOST_CHECK_EQUAL(msg->keyLocks()[0], "lockA");

    msg->setKeyLockAcquired("acquired-key");
    BOOST_CHECK_EQUAL(msg->keyLockAcquired(), "acquired-key");
}

BOOST_AUTO_TEST_CASE(delegateCallFieldsRoundTrip)
{
    ExecutionMessageFactoryImpl factory;
    auto msg = factory.createExecutionMessage();

    msg->setDelegateCallAddress("delegate-addr");
    msg->setDelegateCallSender("delegate-sender");
    bcos::bytes code{0xAA, 0xBB};
    msg->setDelegateCallCode(code);

    BOOST_CHECK_EQUAL(msg->delegateCallAddress(), "delegate-addr");
    BOOST_CHECK_EQUAL(msg->delegateCallSender(), "delegate-sender");
    BOOST_CHECK(msg->delegateCallCode().toBytes() == code);
}

BOOST_AUTO_TEST_CASE(gasPriceFieldsRoundTrip)
{
    ExecutionMessageFactoryImpl factory;
    auto msg = factory.createExecutionMessage();

    msg->setValue("0x64");
    msg->setGasPrice("0x10");
    msg->setMaxFeePerGas("0x20");
    msg->setMaxPriorityFeePerGas("0x5");
    msg->setEffectiveGasPrice("0x15");

    BOOST_CHECK_EQUAL(msg->value(), "0x64");
    BOOST_CHECK_EQUAL(msg->gasPrice(), "0x10");
    BOOST_CHECK_EQUAL(msg->maxFeePerGas(), "0x20");
    BOOST_CHECK_EQUAL(msg->maxPriorityFeePerGas(), "0x5");
    BOOST_CHECK_EQUAL(msg->effectiveGasPrice(), "0x15");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
