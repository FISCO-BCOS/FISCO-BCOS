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
 * @file TestEthCallGasEstimation.cpp
 * @brief Pins the per-frame gas requirement eth_estimateGas reports
 *        (bcos-framework/protocol/EthCallGasEstimation.h):
 *        the exact inverse of the EIP-150 63/64 rule, the
 *        max(used, max over call sites of prefix + h(child)) recursion, and the
 *        EIP-2200 SSTORE sentry allowance.
 */

#include "bcos-framework/protocol/EthCallGasEstimation.h"
#include <boost/test/unit_test.hpp>

using namespace bcos::protocol::gas;

namespace
{
/// The EIP-150 cap: most gas a frame holding @p left can forward.
int64_t forwardable(int64_t left)
{
    return left - left / 64;
}

/// Brute-force inverse: the least L with f(L) >= required.
int64_t bruteForceInverse(int64_t required)
{
    for (int64_t left = 0;; ++left)
    {
        if (forwardable(left) >= required)
            return left;
    }
}

/// Report a call site the way EthereumHost does: via the allowance the child was granted.
/// `callerRemaining` is what the caller really had at the site, so the inversion inside
/// childAllowance must land within a gas of it.
void siteWithCallerRemaining(
    CallGasSession& session, int32_t childDepth, int64_t callerRemaining, bool hasValue = false)
{
    // allowance = f(callerRemaining) (+ stipend), i.e. what evmone hands the child.
    auto allowance = forwardable(callerRemaining) + (hasValue ? c_sstoreSentryGas : 0);
    session.childAllowance(childDepth, allowance, hasValue);
}
}  // namespace

BOOST_AUTO_TEST_SUITE(EthCallGasEstimationTest)

/// h must be the exact inverse. ceil(64r/63) is NOT: it over-counts by one at every r that is a
/// multiple of 63 (r = 64 -> 66 instead of 65), which is the rounding the algorithm must avoid.
BOOST_AUTO_TEST_CASE(MinimumBudgetToForwardIsExactInverse)
{
    BOOST_CHECK_EQUAL(minimumBudgetToForward(0), 0);
    BOOST_CHECK_EQUAL(minimumBudgetToForward(1), 1);
    BOOST_CHECK_EQUAL(minimumBudgetToForward(63), 63);
    BOOST_CHECK_EQUAL(minimumBudgetToForward(64), 65);    // ceil(64*64/63) == 66
    BOOST_CHECK_EQUAL(minimumBudgetToForward(126), 127);  // ceil(64*126/63) == 128
    BOOST_CHECK_EQUAL(minimumBudgetToForward(127), 129);  // ceil(64*127/63) == 130

    for (int64_t required = 1; required <= 20000; ++required)
    {
        auto const budget = minimumBudgetToForward(required);
        BOOST_REQUIRE_EQUAL(budget, bruteForceInverse(required));
        BOOST_REQUIRE_GE(forwardable(budget), required);
        BOOST_REQUIRE_LT(forwardable(budget - 1), required);
    }
}

/// A frame with no children needs exactly what it consumed.
BOOST_AUTO_TEST_CASE(LeafFrameRequiresWhatItUsed)
{
    CallGasSession session;
    session.frameEnter(0, 100'000);
    session.frameExit(0, 21'000);
    BOOST_REQUIRE(session.executionBudget().has_value());
    BOOST_CHECK_EQUAL(*session.executionBudget(), 79'000);
}

/// A call site demands prefix + h(child requirement): the caller must be able to *forward* the
/// child's budget after already spending the prefix, and forwarding loses 1/64.
BOOST_AUTO_TEST_CASE(CallSiteRequirementAddsTheInversePenalty)
{
    CallGasSession session;
    session.frameEnter(0, 100'000);
    siteWithCallerRemaining(session, 1, 1'000);  // prefix ~= 99'000
    session.frameEnter(1, 600);                  // the child's entry, inside f(1000) == 985
    session.frameExit(1, 0);                     // the child used 600
    session.frameExit(0, 99'900);                // the parent used 100 of its own

    // 99'000 + h(600), not 99'000 + 600: the parent must hold enough that 63/64 of the
    // remainder still covers the child.
    BOOST_REQUIRE(session.executionBudget().has_value());
    BOOST_CHECK_EQUAL(*session.executionBudget(), 99'000 + minimumBudgetToForward(600));
    BOOST_CHECK_GT(*session.executionBudget(), 100);  // and not merely the parent's own usage
}

/// Call sites take the maximum, never the sum: gas the frame keeps after a child returns is
/// still spendable at the next one.
BOOST_AUTO_TEST_CASE(CallSiteRequirementsAreMaxedNotSummed)
{
    CallGasSession session;
    session.frameEnter(0, 10'000);
    siteWithCallerRemaining(session, 1, 8'000);  // prefix ~= 2'000
    session.frameEnter(1, 1'000);
    session.frameExit(1, 500);                   // child 1 needed 500
    siteWithCallerRemaining(session, 1, 7'000);  // prefix ~= 3'000
    session.frameEnter(1, 1'000);
    session.frameExit(1, 900);    // child 2 needed 100
    session.frameExit(0, 7'000);  // the parent used 3'000

    auto const first = 2'000 + minimumBudgetToForward(500);
    auto const second = 3'000 + minimumBudgetToForward(100);
    BOOST_REQUIRE(session.executionBudget().has_value());
    BOOST_CHECK_EQUAL(*session.executionBudget(), std::max({first, second, int64_t{3'000}}));
    BOOST_CHECK_LT(*session.executionBudget(), first + second);
}

/// `used` is a floor on the requirement, not the answer: it covers the frame's own work after
/// its last child returns.
BOOST_AUTO_TEST_CASE(UsedGasCoversTheTailAfterTheLastChild)
{
    CallGasSession session;
    session.frameEnter(0, 20'000);
    siteWithCallerRemaining(session, 1, 19'000);  // prefix ~= 1'000
    session.frameEnter(1, 100);
    session.frameExit(1, 0);      // child needed 100, so the site demands ~1'102
    session.frameExit(0, 1'000);  // but the parent itself burned 18'900 more afterwards

    BOOST_REQUIRE(session.executionBudget().has_value());
    BOOST_CHECK_EQUAL(*session.executionBudget(), 19'000);
}

/// A call that failed before entering a frame reports a call site but produces no child. Its
/// prefix must not be charged to the next (unrelated) child, which would under-state the
/// requirement.
BOOST_AUTO_TEST_CASE(UnconsumedCallSiteIsDiscarded)
{
    CallGasSession session;
    session.frameEnter(0, 1'000);
    siteWithCallerRemaining(session, 1, 500);  // light failure: no child frame follows
    siteWithCallerRemaining(session, 1, 400);  // prefix ~= 600, the one that must be used
    session.frameEnter(1, 300);
    session.frameExit(1, 100);  // the child needed 200
    session.frameExit(0, 300);  // the parent used 700

    BOOST_REQUIRE(session.executionBudget().has_value());
    BOOST_CHECK_EQUAL(*session.executionBudget(), 600 + minimumBudgetToForward(200));
}

/// Intrinsic gas is charged before the root frame, so the reported gas limit is the sum. It is
/// never forwarded through a CALL and therefore must not be scaled.
BOOST_AUTO_TEST_CASE(GasLimitAddsIntrinsicToTheExecutionBudget)
{
    CallGasSession session;
    session.intrinsicCost(21'000, 21'000);
    session.frameEnter(0, 50'000);
    session.frameExit(0, 10'000);

    BOOST_REQUIRE(session.gasLimit().has_value());
    BOOST_CHECK_EQUAL(*session.executionBudget(), 40'000);
    BOOST_CHECK_EQUAL(*session.gasLimit(), 21'000 + 40'000);
}

/// EIP-7623: a calldata-heavy transaction must be given max(intrinsic, 21000 + 10 * tokens)
/// before the EVM runs, and admission checks that floor. Execution cost says nothing about it,
/// so the estimate has to carry it or the answer is rejected by this node's own validator.
BOOST_AUTO_TEST_CASE(GasLimitAppliesTheEip7623Floor)
{
    // A cheap call with 10 KiB of calldata: 21000 + 10 * 320 = 24'200 tokens-gas, well above
    // the 21'000 + a few hundred execution gas a plain transfer-like call consumes.
    constexpr int64_t intrinsic = 21'000 + 320 * 16;  // 26'120
    constexpr int64_t minCost = 21'000 + 320 * 10;    // 24'200

    CallGasSession session;
    session.intrinsicCost(intrinsic, minCost);
    session.frameEnter(0, 50'000);
    session.frameExit(0, 49'900);  // 100 gas of execution

    BOOST_REQUIRE(session.gasLimit().has_value());
    BOOST_CHECK_EQUAL(*session.executionBudget(), 100);
    BOOST_CHECK_EQUAL(*session.gasLimit(), std::max(intrinsic + 100, minCost));
}

/// The floor binds on its own when execution is cheaper than the calldata: a call that returns
/// immediately still needs min_cost, and reporting less is the regression this pins.
BOOST_AUTO_TEST_CASE(Eip7623FloorBindsAboveTheExecutionSum)
{
    constexpr int64_t intrinsic = 100'000;
    constexpr int64_t minCost = 120'000;

    CallGasSession session;
    session.intrinsicCost(intrinsic, minCost);
    session.frameEnter(0, 1'000);
    session.frameExit(0, 900);

    BOOST_REQUIRE(session.gasLimit().has_value());
    BOOST_CHECK_EQUAL(*session.gasLimit(), 120'000);
}

/// A session that never saw a frame reports nothing rather than 0: eth_estimateGas must be able
/// to tell "no requirement computed" from "the requirement is zero gas", and only the former may
/// fall back to the measured gasUsed.
BOOST_AUTO_TEST_CASE(SessionWithoutExecutionReportsNothing)
{
    CallGasSession session;
    BOOST_CHECK(!session.gasLimit().has_value());
    BOOST_CHECK(!session.executionBudget().has_value());

    // An intrinsic split alone is not a requirement either: the floor without a budget says
    // nothing about what execution needs.
    session.intrinsicCost(21'000, 24'200);
    BOOST_CHECK(!session.gasLimit().has_value());
}

/// Two runs never share state, because each owns its session. This is the property that used to
/// need a generation counter: the accounting lives on the transaction's coroutine frame, so a
/// second estimate on the same thread -- or on another thread entirely -- cannot read or clear
/// the first one's frames.
BOOST_AUTO_TEST_CASE(SessionsAreIndependent)
{
    CallGasSession first;
    first.frameEnter(0, 10'000);
    first.frameExit(0, 1'000);
    BOOST_REQUIRE_EQUAL(*first.executionBudget(), 9'000);

    CallGasSession second;
    second.frameEnter(0, 5'000);
    second.frameExit(0, 1'000);

    BOOST_CHECK_EQUAL(*first.executionBudget(), 9'000);  // untouched by the second run
    BOOST_CHECK_EQUAL(*second.executionBudget(), 4'000);

    // A later run on the first session's object would overwrite its root, not accumulate.
    first.frameEnter(0, 500);
    first.frameExit(0, 200);
    BOOST_CHECK_EQUAL(*first.executionBudget(), 300);
}

/// EIP-2200's sentry is a remaining-gas floor, not a cost: an SSTORE fails when the frame has
/// <= 2300 gas left *before* anything is charged. A frame that wrote storage therefore needs
/// 2301 more than it consumed, and that surplus compounds up the call chain like any other
/// requirement.
BOOST_AUTO_TEST_CASE(StorageWriteAddsTheSentryHeadroom)
{
    CallGasSession withoutWrite;
    withoutWrite.frameEnter(0, 50'000);
    withoutWrite.frameExit(0, 10'000);  // used 40'000
    BOOST_CHECK_EQUAL(*withoutWrite.executionBudget(), 40'000);

    CallGasSession withWrite;
    withWrite.frameEnter(0, 50'000);
    withWrite.storageWrite();
    withWrite.frameExit(0, 10'000);
    BOOST_CHECK_EQUAL(*withWrite.executionBudget(), 40'000 + c_sstoreSentryGas + 1);
}

/// The sentry surplus is attached to the frame that wrote, so a parent must be able to forward
/// it -- i.e. it goes through h() on the way up, not around it.
BOOST_AUTO_TEST_CASE(StorageWriteSurplusPropagatesThroughTheInverseRule)
{
    CallGasSession session;
    session.frameEnter(0, 100'000);
    siteWithCallerRemaining(session, 1, 4'000);  // prefix ~= 96'000
    session.frameEnter(1, 3'000);
    session.storageWrite();
    session.frameExit(1, 1'000);  // child used 2'000, plus the sentry
    session.frameExit(0, 99'000);

    auto const childRequirement = 2'000 + c_sstoreSentryGas + 1;
    BOOST_REQUIRE(session.executionBudget().has_value());
    BOOST_CHECK_EQUAL(*session.executionBudget(),
        std::max<int64_t>(1'000, 96'000 + minimumBudgetToForward(childRequirement)));
}

BOOST_AUTO_TEST_SUITE_END()
