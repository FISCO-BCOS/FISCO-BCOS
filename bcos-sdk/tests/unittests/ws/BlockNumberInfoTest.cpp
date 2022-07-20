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
 * @file BlockNumberTest.cpp
 * @author: octopus
 * @date 2021-10-26
 */

#include <bcos-cpp-sdk/ws/BlockNumberInfo.h>
#include <bcos-cpp-sdk/ws/Common.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <future>

using namespace bcos;
using namespace bcos::cppsdk;
using namespace bcos::cppsdk::service;

using namespace bcos;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(BlockNumberTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(test_BlockNumber)
{
    {
        int64_t blockNumber = 11;
        std::string group = "group";
        std::string node = "node";

        auto bni = std::make_shared<bcos::cppsdk::service::BlockNumberInfo>();
        bni->setBlockNumber(blockNumber);
        bni->setGroup(group);
        bni->setNode(node);

        BOOST_CHECK_EQUAL(bni->blockNumber(), blockNumber);
        BOOST_CHECK_EQUAL(bni->group(), group);
        BOOST_CHECK_EQUAL(bni->node(), node);
    }
}

BOOST_AUTO_TEST_SUITE_END()