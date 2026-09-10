/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * @brief Regression test for issue #5047: ContractABIDefinition lookups given a null Hash
 *        implementation must throw instead of dereferencing the null pointer.
 */
#include "bcos-cpp-sdk/utilities/abi/ContractABIDefinition.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <stdexcept>

using namespace bcos::cppsdk::abi;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(ContractABIDefinitionNullHashTest)

namespace
{
ContractABIDefinition makeAbiWithOneMethodAndEvent()
{
    ContractABIDefinition abi;
    auto method = std::make_shared<ContractABIMethodDefinition>();
    method->setName("transfer");
    method->setType(ContractABIMethodDefinition::FUNCTION_TYPE);
    method->setInputs({std::make_shared<NamedType>("to", "address"),
        std::make_shared<NamedType>("amount", "uint256")});
    abi.addMethod("transfer", method);

    auto event = std::make_shared<ContractABIMethodDefinition>();
    event->setName("Transfer");
    event->setType(ContractABIMethodDefinition::EVENT_TYPE);
    event->setInputs({std::make_shared<NamedType>("to", "address")});
    abi.addEvent(event);
    return abi;
}
}  // namespace

BOOST_AUTO_TEST_CASE(nullHashImplThrowsInsteadOfCrashing)
{
    auto abi = makeAbiWithOneMethodAndEvent();
    bcos::crypto::Hash::Ptr nullHash;

    BOOST_CHECK_THROW((void)abi.methodIDs("transfer", nullHash), std::invalid_argument);
    BOOST_CHECK_THROW((void)abi.getMethodByMethodID("a9059cbb", nullHash), std::invalid_argument);
    BOOST_CHECK_THROW((void)abi.getEventByTopic("0x00", nullHash), std::invalid_argument);
}

// The dereference lives in ContractABIMethodDefinition's leaf methods, which are public too.
BOOST_AUTO_TEST_CASE(nullHashImplThrowsAtLeafMethods)
{
    ContractABIMethodDefinition method;
    method.setName("transfer");
    method.setType(ContractABIMethodDefinition::FUNCTION_TYPE);
    bcos::crypto::Hash::Ptr nullHash;

    BOOST_CHECK_THROW((void)method.getMethodID(nullHash), std::invalid_argument);
    BOOST_CHECK_THROW((void)method.getMethodIDAsString(nullHash), std::invalid_argument);
    BOOST_CHECK_THROW((void)method.getEventTopicAsString(nullHash), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(validHashImplStillWorks)
{
    auto abi = makeAbiWithOneMethodAndEvent();
    auto keccak = std::make_shared<bcos::crypto::Keccak256>();

    auto ids = abi.methodIDs("transfer", keccak);
    BOOST_REQUIRE_EQUAL(ids.size(), 1U);
    BOOST_CHECK_EQUAL(ids[0], "a9059cbb");  // keccak256("transfer(address,uint256)")[:4]
    BOOST_CHECK(abi.getMethodByMethodID("a9059cbb", keccak) != nullptr);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
