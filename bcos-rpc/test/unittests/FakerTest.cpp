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
 * @brief fake test for the ci
 * @file EmptyTest.cpp
 * @author: yujiechen
 * @date 2021-09-15
 */
#include <bcos-rpc/jsonrpc/groupmgr/GroupManager.h>
#include <bcos-rpc/jsonrpc/groupmgr/LocalGroupManager.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <thread>

using namespace bcos;
using namespace bcos::rpc;
using namespace bcos::group;
namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(FakersTest, TestPromptFixture)
BOOST_AUTO_TEST_CASE(testCreate)
{
    std::make_shared<GroupManager>("", nullptr);
    std::make_shared<LocalGroupManager>("", nullptr, nullptr);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos