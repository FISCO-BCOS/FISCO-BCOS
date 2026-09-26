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
 * @file test_PrecompiledMap_disabledInL2.cpp
 * @brief disabledInL2() / predicateAnd() / kL2DisabledSet behavior (A6.8)
 */
#include "precompiled/L2DisabledSet.h"
#include "vm/Precompiled.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-framework/ledger/Features.h>
#include <bcos-framework/protocol/Protocol.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::ledger;
using namespace bcos::precompiled;

namespace
{
// Minimal stub: PrecompiledMap::at() only reads the stored pointer and runs the
// predicate, it never calls call(). The base ctor asserts a non-null hash, so we
// hand it a real Keccak256.
struct StubPrecompiled : public precompiled::Precompiled
{
    StubPrecompiled() : precompiled::Precompiled(std::make_shared<crypto::Keccak256>()) {}
    std::shared_ptr<PrecompiledExecResult> call(
        std::shared_ptr<TransactionExecutive>, PrecompiledExecResult::Ptr) override
    {
        return nullptr;
    }
};

constexpr uint32_t kVersion = static_cast<uint32_t>(protocol::BlockVersion::V3_3_VERSION);
}  // namespace

BOOST_AUTO_TEST_SUITE(PrecompiledMapDisabledInL2Test)

BOOST_AUTO_TEST_CASE(ConsortiumLanePredicateLetsThrough)
{
    PrecompiledMap map;
    map.insert(SYS_CONFIG_ADDRESS, std::make_shared<StubPrecompiled>(), disabledInL2(false));

    Features features;  // the predicate ignores features entirely
    auto impl = map.at(SYS_CONFIG_ADDRESS, kVersion, false, features);
    BOOST_CHECK(impl != nullptr);
}

BOOST_AUTO_TEST_CASE(EthereumLanePredicateBlocks)
{
    PrecompiledMap map;
    map.insert(SYS_CONFIG_ADDRESS, std::make_shared<StubPrecompiled>(), disabledInL2(true));

    Features features;
    auto impl = map.at(SYS_CONFIG_ADDRESS, kVersion, false, features);
    BOOST_CHECK(impl == nullptr);
}

BOOST_AUTO_TEST_CASE(KL2DisabledSetCoversAllEighteen)
{
    // 18 = 13 stateful business precompiles (original PR-5 set) + 5 FISCO-private
    // crypto/util precompiles added in response to PR review on #5286 (CAST,
    // PAILLIER, GROUP_SIG, RING_SIG, DISCRETE_ZKP — all absent from OP-Stack,
    // so leaving them enabled on the Ethereum lane leaks FISCO-only outputs and breaks
    // chain interop). The three previously-static ones (CAST/GROUP_SIG/RING_SIG)
    // also had their isStaticPrecompiled bypass removed in the same fix.
    BOOST_CHECK_EQUAL(kL2DisabledSet.size(), 18U);

    auto has = [](std::string_view addr) {
        return std::find(kL2DisabledSet.begin(), kL2DisabledSet.end(), addr) !=
               kL2DisabledSet.end();
    };
    BOOST_CHECK(has(SYS_CONFIG_ADDRESS));
    BOOST_CHECK(has(CONSENSUS_ADDRESS));
    BOOST_CHECK(has(BALANCE_PRECOMPILED_ADDRESS));
    BOOST_CHECK(has(ACCOUNT_ADDRESS));
    // Newly added in the PR-review fix — all FISCO-private, all absent from OP-Stack.
    BOOST_CHECK(has(CAST_ADDRESS));
    BOOST_CHECK(has(PAILLIER_ADDRESS));
    BOOST_CHECK(has(GROUP_SIG_ADDRESS));
    BOOST_CHECK(has(RING_SIG_ADDRESS));
    BOOST_CHECK(has(DISCRETE_ZKP_ADDRESS));
    // Negative: CRYPTO is FISCO-private too but stays out of the disabled set (it is
    // the only remaining entry in the static-precompile bypass, used by SDK
    // helpers that have no OP-Stack equivalent address collision).
    BOOST_CHECK(!has(CRYPTO_ADDRESS));
}

BOOST_AUTO_TEST_CASE(PredicateAndComposesWithExistingFlag)
{
    // Mirror the BALANCE site: feature gate ANDed with disabledInL2(ethLane). The lane
    // is captured when the predicate is built, so the two lanes are two maps here, just
    // as they are two boot-time executors in production.
    auto balanceGate = [](uint32_t, bool, Features const& features) {
        return features.get(Features::Flag::feature_balance_precompiled);
    };
    auto makeMap = [&](bool ethLane) {
        PrecompiledMap map;
        map.insert(BALANCE_PRECOMPILED_ADDRESS, std::make_shared<StubPrecompiled>(),
            predicateAnd(balanceGate, disabledInL2(ethLane)));
        return map;
    };

    Features balanceOn;
    balanceOn.set(Features::Flag::feature_balance_precompiled);

    // Ethereum lane, balance feature set -> the lane wins, hidden.
    auto ethLaneMap = makeMap(true);
    BOOST_CHECK(ethLaneMap.at(BALANCE_PRECOMPILED_ADDRESS, kVersion, false, balanceOn) == nullptr);

    // Consortium lane, balance feature set -> visible.
    auto consortiumMap = makeMap(false);
    BOOST_CHECK(consortiumMap.at(BALANCE_PRECOMPILED_ADDRESS, kVersion, false, balanceOn) != nullptr);

    // Consortium lane, balance feature unset -> hidden (balance gate fails).
    Features none;
    BOOST_CHECK(consortiumMap.at(BALANCE_PRECOMPILED_ADDRESS, kVersion, false, none) == nullptr);
}

BOOST_AUTO_TEST_SUITE_END()
