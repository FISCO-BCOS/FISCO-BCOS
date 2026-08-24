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
 * @brief Production-map invariant for disabledInL2(): every kL2DisabledSet member
 *        registered by the REAL initEvmEnvironment must vanish under
 *        feature_l2_ethereum_compat, while a non-L2 precompile stays visible. A
 *        dropped disabledInL2() at any of the 18 EVM insert sites is invisible to
 *        the synthetic-map tests but caught here. (A6.8)
 */
#include "fixture/TransactionFixture.h"
#include "precompiled/L2DisabledSet.h"
#include "vm/Precompiled.h"
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-framework/ledger/Features.h>
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

// Set every feature/auth bit that an original (non-L2) gate at an EVM insert site
// depends on, so the only thing toggling visibility in this test is the L2 flag.
// SHARDING -> feature_sharding, BALANCE -> feature_balance_precompiled,
// PAILLIER -> feature_paillier (PR-review fix on #5286 added PAILLIER to
// kL2DisabledSet under a predicateAnd of feature_paillier + disabledInL2 — so the
// "pbft mode" probe must satisfy feature_paillier or the lookup returns nullptr
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
}  // namespace

BOOST_FIXTURE_TEST_SUITE(PrecompiledMapProductionInvariantTest, TransactionFixture)

// The real EVM map: with the original gates satisfied (no L2 flag) every member of
// kL2DisabledSet must resolve; flip on feature_l2_ethereum_compat and every member
// must disappear; a non-L2 precompile (CRYPTO_ADDRESS) must remain visible.
BOOST_AUTO_TEST_CASE(EvmProductionMapHidesAllL2DisabledMembers)
{
    prepareEnv(/*isCheckAuth*/ true, /*isKeyPage*/ false, protocol::BlockVersion::MAX_VERSION);
    auto const* map = executor->precompiledMapForTest();
    BOOST_REQUIRE(map != nullptr);

    Features pbft = enablingFeatures();  // L2 flag unset
    BOOST_REQUIRE(!pbft.get(Features::Flag::feature_l2_ethereum_compat));

    Features l2 = enablingFeatures();
    l2.set(Features::Flag::feature_l2_ethereum_compat);

    // isAuth=true so the AUTH_* version-OR-auth gate is satisfied either way.
    constexpr bool isAuth = true;

    for (auto addr : kL2DisabledSet)
    {
        BOOST_TEST_INFO("address=" << addr);
        // pbft mode (original gate satisfied) -> registered and visible.
        BOOST_CHECK_MESSAGE(map->at(addr, kMaxVersion, isAuth, pbft) != nullptr,
            "expected " << addr << " visible in pbft mode (gate or missing registration?)");
        // L2 mode -> hidden by disabledInL2().
        BOOST_CHECK_MESSAGE(map->at(addr, kMaxVersion, isAuth, l2) == nullptr,
            "expected " << addr << " hidden under feature_l2_ethereum_compat "
                        << "(dropped disabledInL2() at its insert site?)");
    }

    // Stays-visible probe: CRYPTO_ADDRESS is inserted with no predicate, so the L2
    // flag must not hide it. CRYPTO is also the only entry left in the
    // static-precompile bypass; the L2-disabled FISCO-private precompiles
    // (CAST/GROUP_SIG/RING_SIG/PAILLIER/DISCRETE_ZKP) were moved out of the
    // static set in the same PR-review fix and now flow through this predicate
    // path (and are covered by the `for (auto addr : kL2DisabledSet)` loop above).
    BOOST_CHECK(map->at(CRYPTO_ADDRESS, kMaxVersion, isAuth, l2) != nullptr);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
