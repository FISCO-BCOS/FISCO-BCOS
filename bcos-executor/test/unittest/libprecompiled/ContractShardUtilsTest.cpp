/**
 *  Copyright (C) 2023 FISCO BCOS.
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
 * @file ContractShardUtilsTest.cpp
 * @author: JimmyShi22
 * @date 2023-01-03
 */

#include "bcos-table/src/ContractShardUtils.h"
#include "libprecompiled/PreCompiledFixture.h"
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-tool/VersionConverter.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <json/json.h>


using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::ledger;

namespace bcos::test
{
class ContractShardUtilsTestFixture : public PrecompiledFixture
{
public:
    ContractShardUtilsTestFixture() = default;

    ~ContractShardUtilsTestFixture() override = default;


    StorageWrapper storage = StorageWrapper(
        std::make_shared<bcos::storage::StateStorage>(PrecompiledFixture::storage, false),
        std::make_shared<storage::Recoder>());
};

BOOST_FIXTURE_TEST_SUITE(ContractShardUtilsTest, ContractShardUtilsTestFixture)

BOOST_AUTO_TEST_CASE(setAndGetTest)
{
    std::string contract = "/apps/0x11111";
    std::string shard = "/shards/hello";
    storage.createTable(contract, std::string(STORAGE_VALUE));

    // test empty
    auto emptyShard = ContractShardUtils::getContractShard(storage, contract);
    BOOST_CHECK_EQUAL("", emptyShard);

    // test set and get
    ContractShardUtils::setContractShard(storage, contract, shard);
    auto shardCmp = ContractShardUtils::getContractShard(storage, contract);
    BOOST_CHECK_EQUAL(shard, shardCmp);
}

BOOST_AUTO_TEST_CASE(setAndGetTreeTest)
{
#define FOR for (int i = 0; i < 20; i++)

    // set parent
    FOR
    {
        std::string contract = "/apps/0x000000000" + std::to_string(i);
        storage.createTable(contract, std::string(STORAGE_VALUE));

        std::string parent = "/apps/0x000000000" + std::to_string(i / 2);
        ContractShardUtils::setContractShardByParent(storage, parent, contract);
    }

    // test empty
    FOR
    {
        std::string contract = "/apps/0x000000000" + std::to_string(i);
        auto emptyShard = ContractShardUtils::getContractShard(storage, contract);
        BOOST_CHECK_EQUAL("", emptyShard);
    }

    // test tree
    std::string shard = "/shards/hello";
    std::string root = "/apps/0x000000000" + std::to_string(0);
    ContractShardUtils::setContractShard(storage, root, shard);
    FOR
    {
        std::string contract = "/apps/0x000000000" + std::to_string(i);
        auto shardCmp = ContractShardUtils::getContractShard(storage, contract);
        BOOST_CHECK_EQUAL(shard, shardCmp);
    }
}


BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test