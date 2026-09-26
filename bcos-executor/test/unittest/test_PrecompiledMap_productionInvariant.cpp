/*
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file test_PrecompiledMap_productionInvariant.cpp
 * @brief Production-map invariant for disabledInL2(bool): every kL2DisabledSet member
 *        registered by the REAL initEvmEnvironment must vanish on the Ethereum lane
 *        (executor_version >= ETHEREUM_EXECUTOR_VERSION, read from the boot-time
 *        ledgerConfig), while a non-gated precompile stays visible on both lanes. A
 *        dropped disabledInL2() at any of the 18 EVM insert sites is invisible to
 *        the synthetic-map tests but caught here. (A6.8)
 */
#include "fixture/TransactionFixture.h"
#include "precompiled/L2DisabledSet.h"
#include "vm/Precompiled.h"
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-framework/ledger/Features.h>
#include <bcos-framework/ledger/SystemConfigs.h>
#include <bcos-framework/protocol/Protocol.h>

#include <boost/test/unit_test.hpp>
using namespace bcos;
using namespace bcos::executor;
using namespace bcos::ledger;
using namespace bcos::precompiled;

namespace bcos::test
{
namespace
{
constexpr uint32_t kMaxVersion = static_cast<uint32_t>(protocol::BlockVersion::MAX_VERSION);

// Set every feature/auth bit that an original (non-lane) gate at an EVM insert site
// depends on, so the only thing toggling visibility in this test is the lane the
// executor booted on.
// SHARDING -> feature_sharding, BALANCE -> feature_balance_precompiled,
// PAILLIER -> feature_paillier (PR-review fix on #5286 added PAILLIER to
// kL2DisabledSet under a predicateAnd of feature_paillier + disabledInL2 — so the
// consortium-lane probe must satisfy feature_paillier or the lookup returns nullptr
// for the wrong reason). The AUTH_* and ACCOUNT* gates are version-driven and
// pass at MAX_VERSION.
Features enablingFeatures()
{
    Features f;
    f.set(Features::Flag::feature_sharding);
    f.set(Features::Flag::feature_balance_precompiled);
    f.set(Features::Flag::feature_paillier);
    return f;
}

// A MockLedger whose only divergence is the executor_version row: updateLedgerConfig()
// in the TransactionExecutor constructor folds it into the boot-time ledgerConfig, so
// initEvmEnvironment captures ethLane=true in its disabledInL2() predicates — the same
// wiring a real Ethereum-lane node gets at boot.
class EthLaneLedger : public MockLedger
{
public:
    using MockLedger::MockLedger;
    task::Task<ledger::SystemConfigs> fetchAllSystemConfigs(protocol::BlockNumber) override
    {
        ledger::SystemConfigs configs;
        configs.set(ledger::SystemConfig::executor_version,
            std::to_string(ledger::ETHEREUM_EXECUTOR_VERSION), 0);
        co_return configs;
    }
};
}  // namespace

BOOST_FIXTURE_TEST_SUITE(PrecompiledMapProductionInvariantTest, TransactionFixture)

// The real EVM map, both lanes: with the original gates satisfied, every member of
// kL2DisabledSet must resolve on the consortium lane and disappear on the Ethereum
// lane; a non-gated precompile (CRYPTO_ADDRESS) must remain visible on both.
BOOST_AUTO_TEST_CASE(EvmProductionMapHidesAllL2DisabledMembers)
{
    prepareEnv(/*isCheckAuth*/ true, /*isKeyPage*/ false, protocol::BlockVersion::MAX_VERSION);

    // Consortium lane: the fixture's MockLedger reports no executor_version row, so the
    // boot-time ledgerConfig has executorVersion()==0 and initEvmEnvironment captured
    // disabledInL2(false) at every insert site.
    auto const* consortiumMap = executor->precompiledMapForTest();
    BOOST_REQUIRE(consortiumMap != nullptr);

    // Ethereum lane: same build path, but the ledger's executor_version row puts the
    // boot-time ledgerConfig on the Ethereum lane, so the same insert sites capture
    // disabledInL2(true).
    auto ethExecutor = bcos::executor::TransactionExecutorFactory::build(
        std::make_shared<EthLaneLedger>(storage), txpool, nullptr, storage,
        std::make_shared<NativeExecutionMessageFactory>(),
        std::make_shared<storage::StateStorageFactory>(0), hashImpl,
        /*isAuthCheck*/ true, std::string("eth-lane-executor"));
    auto const* ethLaneMap = ethExecutor->precompiledMapForTest();
    BOOST_REQUIRE(ethLaneMap != nullptr);

    Features gates = enablingFeatures();

    // isAuth=true so the AUTH_* version-OR-auth gate is satisfied either way.
    constexpr bool isAuth = true;

    for (auto addr : kL2DisabledSet)
    {
        BOOST_TEST_INFO("address=" << addr);
        // Consortium lane (original gate satisfied) -> registered and visible.
        BOOST_CHECK_MESSAGE(consortiumMap->at(addr, kMaxVersion, isAuth, gates) != nullptr,
            "expected " << addr << " visible off the Ethereum lane (gate or missing registration?)");
        // Ethereum lane -> hidden by disabledInL2(true), whatever the features say.
        BOOST_CHECK_MESSAGE(ethLaneMap->at(addr, kMaxVersion, isAuth, gates) == nullptr,
            "expected " << addr << " hidden on the Ethereum lane "
                        << "(dropped disabledInL2() at its insert site?)");
    }

    // Stays-visible probe: CRYPTO_ADDRESS is inserted with no predicate, so the lane
    // must not hide it. CRYPTO is also the only entry left in the
    // static-precompile bypass; the lane-disabled FISCO-private precompiles
    // (CAST/GROUP_SIG/RING_SIG/PAILLIER/DISCRETE_ZKP) were moved out of the
    // static set in the same PR-review fix and now flow through this predicate
    // path (and are covered by the `for (auto addr : kL2DisabledSet)` loop above).
    BOOST_CHECK(ethLaneMap->at(CRYPTO_ADDRESS, kMaxVersion, isAuth, gates) != nullptr);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
