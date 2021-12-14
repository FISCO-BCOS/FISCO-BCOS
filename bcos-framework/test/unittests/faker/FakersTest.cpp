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
 * @brief Unit tests for the fakers
 * @file FakersTest.cpp
 */
#include "../../../interfaces/executor/PrecompiledTypeDef.h"
#include "../../../interfaces/multigroup/ChainNodeInfo.h"
#include "../../../interfaces/multigroup/GroupInfo.h"
#include "../../../testutils/TestPromptFixture.h"
#include <testutils/faker/FakeFrontService.h>
#include <testutils/faker/FakeKVStorage.h>
#include <testutils/faker/FakeLedger.h>
#include <testutils/faker/FakeScheduler.h>
#include <testutils/faker/FakeSealer.h>
#include <testutils/faker/FakeTxPool.h>
#include <boost/test/unit_test.hpp>
#include <string>

namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(FakersTest, TestPromptFixture)
BOOST_AUTO_TEST_CASE(testGroupInfo)
{
    std::make_shared<bcos::group::GroupInfo>("", "");
}
BOOST_AUTO_TEST_CASE(testNodeInfo)
{
    std::make_shared<bcos::group::ChainNodeInfo>("", 0);
}

BOOST_AUTO_TEST_CASE(fakeSchedulerTest)
{
    std::make_shared<FakeScheduler>(nullptr, nullptr);
}

BOOST_AUTO_TEST_CASE(fakeFrontServiceConstructor)
{
    std::make_shared<FakeFrontService>(nullptr);
}

BOOST_AUTO_TEST_CASE(fakeLedgerConstructor)
{
    std::make_shared<FakeLedger>();
}
BOOST_AUTO_TEST_CASE(fakeKVStorage)
{
    // std::make_shared<FakeKVStorage>(nullptr);
}

BOOST_AUTO_TEST_CASE(fakeStorageConstructor)
{
    // std::make_shared<FakeStorage>();
}

BOOST_AUTO_TEST_CASE(fakeSealerConstructor)
{
    std::make_shared<FakeSealer>();
}

BOOST_AUTO_TEST_CASE(fakeTxPoolConstructor)
{
    std::make_shared<FakeTxPool>();

    std::cout << bcos::precompiled::SYS_CONFIG_NAME << std::endl;
    std::cout << bcos::precompiled::TABLE_NAME << std::endl;
    std::cout << bcos::precompiled::CONSENSUS_NAME << std::endl;
    std::cout << bcos::precompiled::CNS_NAME << std::endl;
    std::cout << bcos::precompiled::PARALLEL_CONFIG_NAME << std::endl;
    std::cout << bcos::precompiled::CONTRACT_LIFECYCLE_NAME << std::endl;
    std::cout << bcos::precompiled::KV_TABLE_NAME << std::endl;
    std::cout << bcos::precompiled::CRYPTO_NAME << std::endl;
    std::cout << bcos::precompiled::DAG_TRANSFER_NAME << std::endl;
    std::cout << bcos::precompiled::CONTRACT_AUTH_NAME << std::endl;
    std::cout << bcos::precompiled::DEPLOY_WASM_NAME << std::endl;
    std::cout << bcos::precompiled::BFS_NAME << std::endl;

    std::cout << bcos::precompiled::SYS_CONFIG_ADDRESS << std::endl;
    std::cout << bcos::precompiled::TABLE_ADDRESS << std::endl;
    std::cout << bcos::precompiled::CONSENSUS_ADDRESS << std::endl;
    std::cout << bcos::precompiled::CNS_ADDRESS << std::endl;
    std::cout << bcos::precompiled::CONTRACT_AUTH_ADDRESS << std::endl;
    std::cout << bcos::precompiled::PARALLEL_CONFIG_ADDRESS << std::endl;
    std::cout << bcos::precompiled::CONTRACT_LIFECYCLE_ADDRESS << std::endl;
    std::cout << bcos::precompiled::CHAIN_GOVERNANCE_ADDRESS << std::endl;
    std::cout << bcos::precompiled::KV_TABLE_ADDRESS << std::endl;
    std::cout << bcos::precompiled::CRYPTO_ADDRESS << std::endl;
    std::cout << bcos::precompiled::WORKING_SEALER_MGR_ADDRESS << std::endl;
    std::cout << bcos::precompiled::DAG_TRANSFER_ADDRESS << std::endl;
    std::cout << bcos::precompiled::DEPLOY_WASM_ADDRESS << std::endl;
    std::cout << bcos::precompiled::BFS_ADDRESS << std::endl;
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
