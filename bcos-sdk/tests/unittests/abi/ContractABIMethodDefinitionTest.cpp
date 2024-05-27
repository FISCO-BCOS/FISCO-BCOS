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
 * @file cm.cpp
 * @author: octopus
 * @date 2022-05-30
 */

#include "bcos-cpp-sdk/utilities/abi/ContractABIMethodDefinition.h"
#include "bcos-cpp-sdk/utilities/abi/ContractABIDefinition.h"
#include "bcos-crypto/hash/Keccak256.h"
#include <bcos-cpp-sdk/utilities/abi/ContractABIType.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/tools/old/interface.hpp>
#include <memory>
#include <stdexcept>
#include <utility>

using namespace bcos;
using namespace bcos::cppsdk;
using namespace bcos::cppsdk::abi;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(ContractABIMethodDefinitionTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(test_namedType)
{
    {
        auto namedType = std::make_shared<NamedType>("a", "int");

        BOOST_CHECK_EQUAL(namedType->name(), "a");
        BOOST_CHECK_EQUAL(namedType->type(), "int");
        BOOST_CHECK_EQUAL(namedType->indexed(), false);
    }

    {
        auto namedType = std::make_shared<NamedType>("a", "string", true);

        BOOST_CHECK_EQUAL(namedType->name(), "a");
        BOOST_CHECK_EQUAL(namedType->type(), "string");
        BOOST_CHECK_EQUAL(namedType->indexed(), true);
    }

    {
        auto namedType = std::make_shared<NamedType>("a", "bytes");

        BOOST_CHECK_EQUAL(namedType->name(), "a");
        BOOST_CHECK_EQUAL(namedType->type(), "bytes");
    }

    {
        auto namedType = std::make_shared<NamedType>("a", "int[][10][]", false);

        BOOST_CHECK_EQUAL(namedType->name(), "a");
        BOOST_CHECK_EQUAL(namedType->type(), "int[][10][]");
    }

    {
        auto namedType = std::make_shared<NamedType>("a", "tuple", false);

        auto c0 = std::make_shared<NamedType>("c0", "int", false);
        auto c1 = std::make_shared<NamedType>("c1", "string", false);
        auto c2 = std::make_shared<NamedType>("c2", "address", false);
        auto c3 = std::make_shared<NamedType>("c3", "bytes", false);
        auto c4 = std::make_shared<NamedType>("c4", "bytes32", false);

        namedType->components().push_back(c0);
        namedType->components().push_back(c1);
        namedType->components().push_back(c2);
        namedType->components().push_back(c3);
        namedType->components().push_back(c4);

        BOOST_CHECK_EQUAL(namedType->getTypeAsString(), "(int,string,address,bytes,bytes32)");
    }

    {
        auto namedType = std::make_shared<NamedType>("a", "tuple[][10][]", false);

        auto c0 = std::make_shared<NamedType>("c0", "int", false);
        auto c1 = std::make_shared<NamedType>("c1", "string", false);
        auto c2 = std::make_shared<NamedType>("c2", "address", false);
        auto c3 = std::make_shared<NamedType>("c3", "bytes", false);
        auto c4 = std::make_shared<NamedType>("c4", "bytes32", false);

        namedType->components().push_back(c0);
        namedType->components().push_back(c1);
        namedType->components().push_back(c2);
        namedType->components().push_back(c3);
        namedType->components().push_back(c4);

        BOOST_CHECK_EQUAL(
            namedType->getTypeAsString(), "(int,string,address,bytes,bytes32)[][10][]");
    }
}


BOOST_AUTO_TEST_CASE(test_namedTypeHelper)
{
    {
        auto helper = std::make_shared<NamedTypeHelper>("int");
        BOOST_CHECK_THROW(helper->reset(""), std::runtime_error);
        BOOST_CHECK_THROW(helper->reset("int[a]"), std::runtime_error);
    }

    {
        auto helper = std::make_shared<NamedTypeHelper>("int");

        BOOST_CHECK_EQUAL(helper->baseType(), "int");
        BOOST_CHECK_EQUAL(helper->type(), "int");
        BOOST_CHECK_EQUAL(helper->isDynamicList(), false);
        BOOST_CHECK_EQUAL(helper->isFixedList(), false);
        BOOST_CHECK_EQUAL(helper->isTuple(), false);
        BOOST_CHECK_EQUAL(helper->extentsSize(), 0);
        BOOST_CHECK_EQUAL(helper->extentsSize(), 0);
        helper->removeExtent();
        BOOST_CHECK_EQUAL(helper->extentsSize(), 0);
    }

    {
        auto helper = std::make_shared<NamedTypeHelper>("string");

        BOOST_CHECK_EQUAL(helper->baseType(), "string");
        BOOST_CHECK_EQUAL(helper->type(), "string");
        BOOST_CHECK_EQUAL(helper->isDynamicList(), false);
        BOOST_CHECK_EQUAL(helper->isFixedList(), false);
        BOOST_CHECK_EQUAL(helper->isTuple(), false);
        BOOST_CHECK_EQUAL(helper->extentsSize(), 0);
        BOOST_CHECK_EQUAL(helper->extentsSize(), 0);
        helper->removeExtent();
        BOOST_CHECK_EQUAL(helper->extentsSize(), 0);
    }

    {
        auto helper = std::make_shared<NamedTypeHelper>("bytes32");

        BOOST_CHECK_EQUAL(helper->baseType(), "bytes32");
        BOOST_CHECK_EQUAL(helper->type(), "bytes32");
        BOOST_CHECK_EQUAL(helper->isDynamicList(), false);
        BOOST_CHECK_EQUAL(helper->isFixedList(), false);
        BOOST_CHECK_EQUAL(helper->isTuple(), false);
        BOOST_CHECK_EQUAL(helper->extentsSize(), 0);
        BOOST_CHECK_EQUAL(helper->extentsSize(), 0);
        helper->removeExtent();
        BOOST_CHECK_EQUAL(helper->extentsSize(), 0);
    }
    {
        auto helper = std::make_shared<NamedTypeHelper>("string[]");

        BOOST_CHECK_EQUAL(helper->baseType(), "string");
        BOOST_CHECK_EQUAL(helper->type(), "string[]");
        BOOST_CHECK_EQUAL(helper->isDynamicList(), true);
        BOOST_CHECK_EQUAL(helper->isFixedList(), false);
        BOOST_CHECK_EQUAL(helper->isTuple(), false);
        BOOST_CHECK_EQUAL(helper->extentsSize(), 1);
        helper->removeExtent();
        BOOST_CHECK_EQUAL(helper->extentsSize(), 0);
    }

    {
        auto helper = std::make_shared<NamedTypeHelper>("string[][1][2][3]");

        BOOST_CHECK_EQUAL(helper->baseType(), "string");
        BOOST_CHECK_EQUAL(helper->type(), "string[][1][2][3]");
        BOOST_CHECK_EQUAL(helper->isDynamicList(), false);
        BOOST_CHECK_EQUAL(helper->isFixedList(), true);
        BOOST_CHECK_EQUAL(helper->isTuple(), false);
        BOOST_CHECK_EQUAL(helper->extentsSize(), 4);
        BOOST_CHECK_EQUAL(helper->extent(1), 0);
        BOOST_CHECK_EQUAL(helper->extent(2), 1);
        BOOST_CHECK_EQUAL(helper->extent(3), 2);
        BOOST_CHECK_EQUAL(helper->extent(4), 3);

        helper->removeExtent();

        BOOST_CHECK_EQUAL(helper->isDynamicList(), false);
        BOOST_CHECK_EQUAL(helper->isFixedList(), true);
        BOOST_CHECK_EQUAL(helper->isTuple(), false);

        BOOST_CHECK_EQUAL(helper->extentsSize(), 3);
        BOOST_CHECK_EQUAL(helper->extent(1), 0);
        BOOST_CHECK_EQUAL(helper->extent(2), 1);
        BOOST_CHECK_EQUAL(helper->extent(3), 2);

        helper->removeExtent();

        BOOST_CHECK_EQUAL(helper->isDynamicList(), false);
        BOOST_CHECK_EQUAL(helper->isFixedList(), true);
        BOOST_CHECK_EQUAL(helper->isTuple(), false);

        BOOST_CHECK_EQUAL(helper->extentsSize(), 2);
        BOOST_CHECK_EQUAL(helper->extent(1), 0);
        BOOST_CHECK_EQUAL(helper->extent(2), 1);

        helper->removeExtent();

        BOOST_CHECK_EQUAL(helper->isDynamicList(), true);
        BOOST_CHECK_EQUAL(helper->isFixedList(), false);
        BOOST_CHECK_EQUAL(helper->isTuple(), false);

        BOOST_CHECK_EQUAL(helper->extentsSize(), 1);
        BOOST_CHECK_EQUAL(helper->extent(1), 0);

        helper->removeExtent();

        BOOST_CHECK_EQUAL(helper->isDynamicList(), false);
        BOOST_CHECK_EQUAL(helper->isFixedList(), false);
        BOOST_CHECK_EQUAL(helper->isTuple(), false);
        BOOST_CHECK_EQUAL(helper->extentsSize(), 0);

        helper->removeExtent();
    }

    {
        auto helper = std::make_shared<NamedTypeHelper>("tuple[][1][2][3]");

        BOOST_CHECK_EQUAL(helper->baseType(), "tuple");
        BOOST_CHECK_EQUAL(helper->type(), "tuple[][1][2][3]");
        BOOST_CHECK_EQUAL(helper->isDynamicList(), false);
        BOOST_CHECK_EQUAL(helper->isFixedList(), true);
        BOOST_CHECK_EQUAL(helper->isTuple(), true);

        BOOST_CHECK_EQUAL(helper->extentsSize(), 4);
        BOOST_CHECK_EQUAL(helper->extent(1), 0);
        BOOST_CHECK_EQUAL(helper->extent(2), 1);
        BOOST_CHECK_EQUAL(helper->extent(3), 2);
        BOOST_CHECK_EQUAL(helper->extent(4), 3);

        helper->removeExtent();

        BOOST_CHECK_EQUAL(helper->isDynamicList(), false);
        BOOST_CHECK_EQUAL(helper->isFixedList(), true);
        BOOST_CHECK_EQUAL(helper->isTuple(), true);

        BOOST_CHECK_EQUAL(helper->extentsSize(), 3);
        BOOST_CHECK_EQUAL(helper->extent(1), 0);
        BOOST_CHECK_EQUAL(helper->extent(2), 1);
        BOOST_CHECK_EQUAL(helper->extent(3), 2);

        helper->removeExtent();

        BOOST_CHECK_EQUAL(helper->isDynamicList(), false);
        BOOST_CHECK_EQUAL(helper->isFixedList(), true);
        BOOST_CHECK_EQUAL(helper->isTuple(), true);

        BOOST_CHECK_EQUAL(helper->extentsSize(), 2);
        BOOST_CHECK_EQUAL(helper->extent(1), 0);
        BOOST_CHECK_EQUAL(helper->extent(2), 1);

        helper->removeExtent();

        BOOST_CHECK_EQUAL(helper->isDynamicList(), true);
        BOOST_CHECK_EQUAL(helper->isFixedList(), false);
        BOOST_CHECK_EQUAL(helper->isTuple(), true);

        BOOST_CHECK_EQUAL(helper->extentsSize(), 1);
        BOOST_CHECK_EQUAL(helper->extent(1), 0);

        helper->removeExtent();

        BOOST_CHECK_EQUAL(helper->isDynamicList(), false);
        BOOST_CHECK_EQUAL(helper->isFixedList(), false);
        BOOST_CHECK_EQUAL(helper->isTuple(), true);
        BOOST_CHECK_EQUAL(helper->extentsSize(), 0);

        helper->removeExtent();
    }
}

BOOST_AUTO_TEST_SUITE_END()
