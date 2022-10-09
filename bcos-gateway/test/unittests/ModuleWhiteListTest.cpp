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
 * @brief test for ModuleWhiteList
 * @file GatewayFactoryTest.cpp
 * @author: octopus
 * @date 2021-05-17
 */

#include <bcos-gateway/libratelimit/ModuleWhiteList.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace gateway;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(ModuleWhiteListTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(test_moduleWhiteList)
{
    bcos::gateway::ratelimit::ModuleWhiteList moduleWhiteList;

    BOOST_CHECK(!moduleWhiteList.isModuleExist(1001));

    BOOST_CHECK(moduleWhiteList.addModuleID(1001));
    BOOST_CHECK(moduleWhiteList.isModuleExist(1001));

    BOOST_CHECK(!moduleWhiteList.addModuleID(1001));
    BOOST_CHECK(moduleWhiteList.isModuleExist(1001));

    moduleWhiteList.removeModuleID(1001);
    BOOST_CHECK(!moduleWhiteList.isModuleExist(1001));
}

BOOST_AUTO_TEST_SUITE_END()