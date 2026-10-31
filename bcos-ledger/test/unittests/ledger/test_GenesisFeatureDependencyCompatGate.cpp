/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file test_GenesisFeatureDependencyCompatGate.cpp
 * @brief The genesis [features] dependency check only applies from compatibility_version
 *        3.18.0. A new node joining a released (< 3.18) chain initializes an empty datadir
 *        through the same first-init path, so its genesis must keep initializing even when
 *        it predates a dependency rule.
 */
#include "L2GenesisTestStorage.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/GenesisConfig.h"
#include "bcos-ledger/Ledger.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-task/Wait.h"
#include "bcos-tool/Exceptions.h"
#include <bcos-framework/testutils/faker/FakeBlock.h>
#include <boost/test/unit_test.hpp>
#include <future>
#include <memory>
#include <set>

using namespace bcos;
using namespace bcos::ledger;
using namespace bcos::protocol;
using namespace bcos::crypto;

namespace bcos::test
{
namespace
{
GenesisConfig makeCompatGateGenesis(
    BlockVersion compatibilityVersion, std::vector<FeatureSet> features)
{
    GenesisConfig genesis;
    genesis.m_txGasLimit = 3000000000;
    genesis.m_compatibilityVersion = static_cast<uint32_t>(compatibilityVersion);
    genesis.m_executorVersion = 1;
    genesis.m_chainID = "chain0";
    genesis.m_groupID = "group0";
    genesis.m_features = std::move(features);
    return genesis;
}

std::set<std::string> compatGateSysConfigKeys(storage::StorageInterface& storage)
{
    std::promise<std::vector<std::string>> promise;
    storage.asyncGetPrimaryKeys(SYS_CONFIG, std::nullopt,
        [&promise](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_REQUIRE(!error);
            promise.set_value(std::move(keys));
        });
    auto keys = promise.get_future().get();
    return {keys.begin(), keys.end()};
}

LedgerConfig compatGateParam()
{
    LedgerConfig param;
    param.setBlockNumber(0);
    param.setHash(HashType(""));
    param.setBlockTxCountLimit(0);
    return param;
}

struct GenesisCompatGateFixture
{
    GenesisCompatGateFixture() { m_blockFactory = createBlockFactory(createNormalCryptoSuite()); }

    // Initializes @p genesis on fresh storage; returns the SYS_CONFIG keys it wrote.
    std::set<std::string> initialize(GenesisConfig const& genesis)
    {
        auto storage = makeL2GenesisTestStorage();
        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);
        auto param = compatGateParam();
        BOOST_REQUIRE(task::syncWait(ledger::buildGenesisBlock(*ledger, genesis, param)));
        return compatGateSysConfigKeys(*storage);
    }

    BlockFactory::Ptr m_blockFactory;
};
}  // namespace

BOOST_FIXTURE_TEST_SUITE(GenesisFeatureDependencyCompatGateTest, GenesisCompatGateFixture)

// A 3.17 genesis enabling feature_balance_precompiled without feature_balance (accepted by
// v3.17.1) initializes on an empty datadir, and the write path is the unchanged one: the rows
// are exactly those of the same genesis without [features] plus the one flag row.
BOOST_AUTO_TEST_CASE(PreV318GenesisSkipsDependencyCheck)
{
    auto const plainKeys = initialize(makeCompatGateGenesis(BlockVersion::V3_17_0_VERSION, {}));
    auto const keys = initialize(makeCompatGateGenesis(BlockVersion::V3_17_0_VERSION,
        {FeatureSet{.flag = Features::Flag::feature_balance_precompiled, .enable = 1}}));

    BOOST_CHECK(!plainKeys.contains("feature_balance_precompiled"));
    BOOST_CHECK(!keys.contains("feature_balance"));
    auto expected = plainKeys;
    expected.insert("feature_balance_precompiled");
    BOOST_CHECK_EQUAL_COLLECTIONS(keys.begin(), keys.end(), expected.begin(), expected.end());
}

// From compatibility_version 3.18.0 the same rules refuse the genesis before any write.
BOOST_AUTO_TEST_CASE(V318GenesisRefusesMissingDependency)
{
    struct Case
    {
        Features::Flag flag;
        std::string_view missing;
    };
    for (auto const& [flag, missing] :
        {Case{Features::Flag::feature_balance_precompiled, "feature_balance"},
            Case{Features::Flag::feature_balance_policy1, "feature_balance_precompiled"}})
    {
        BOOST_TEST_CONTEXT("flag=" << magic_enum::enum_name(flag))
        {
            auto storage = makeL2GenesisTestStorage();
            auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);
            auto param = compatGateParam();
            auto genesis = makeCompatGateGenesis(
                BlockVersion::V3_18_0_VERSION, {FeatureSet{.flag = flag, .enable = 1}});
            BOOST_CHECK_EXCEPTION(
                task::syncWait(ledger::buildGenesisBlock(*ledger, genesis, param)),
                bcos::tool::InvalidSetFeature, [&](auto const& e) {
                    auto const* comment = boost::get_error_info<errinfo_comment>(e);
                    return comment != nullptr && comment->find(missing) != std::string::npos;
                });
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
