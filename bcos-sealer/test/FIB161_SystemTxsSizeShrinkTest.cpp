/*
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
 * @file FIB161_SystemTxsSizeShrinkTest.cpp
 * @brief Regression test for FIB-161. The bug was that
 *        SealingManager::generateProposal computed systemTxsSize before the
 *        block-hook ran and only decremented txsSize after the hook, leaving
 *        the invariant systemTxsSize <= txsSize broken in the
 *        "maxTxsPerBlock <= maxSysTxsPerBlock && sys queue full && hook
 *        injects 1" case — the drain loops then over-appended.
 *
 *        The fix extracts the bound computation into a pure function
 *        bcos::sealer::detail::computeAssemblyPlan, so this regression is
 *        verified by direct arithmetic checks without standing up a
 *        SealingManager.
 */

#include "bcos-sealer/SealingManager.h"
#include <boost/test/unit_test.hpp>

using bcos::sealer::detail::c_maxSysTxsPerBlock;
using bcos::sealer::detail::computeAssemblyPlan;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(FIB161SystemTxsSizeShrinkTest)

// T1 — pure bug trigger: max=5, sysQueue full, hook inserts 1.
// Pre-fix would have left systemTxsSize at 5, producing 1+5=6 > max.
BOOST_AUTO_TEST_CASE(t1_hook_injects_when_sys_queue_at_max)
{
    auto [txs, sys] = computeAssemblyPlan(
        /*max=*/5, /*normal=*/0, /*sys=*/5, /*hookInsertedTx=*/true);
    BOOST_CHECK_EQUAL(txs, 4U);
    BOOST_CHECK_EQUAL(sys, 4U);
    BOOST_CHECK_LE(sys, txs);      // invariant
    BOOST_CHECK_LE(1U + txs, 5U);  // hook tx + queue draws <= max
}

// T2 — mixed pending: sys=3, normal=5, max=5, hook adds 1.
BOOST_AUTO_TEST_CASE(t2_hook_with_mixed_pending)
{
    auto [txs, sys] = computeAssemblyPlan(5, 5, 3, true);
    BOOST_CHECK_EQUAL(txs, 4U);  // 5 -> 4 after hook collapse
    BOOST_CHECK_EQUAL(sys, 3U);  // capped at pendingSysSize, fits under txs
    BOOST_CHECK_LE(1U + txs, 5U);
}

// T3 — max equals the sys cap (10): hook injects 1 while sysQueue is at cap.
// Pre-fix would have driven systemTxsSize=10, blockSize=11.
BOOST_AUTO_TEST_CASE(t3_max_equals_sys_cap_with_hook)
{
    auto [txs, sys] = computeAssemblyPlan(10, 0, 10, true);
    BOOST_CHECK_EQUAL(txs, 9U);
    BOOST_CHECK_EQUAL(sys, 9U);
    BOOST_CHECK_LE(1U + txs, 10U);
}

// T4 — no hook: bounds untouched.
BOOST_AUTO_TEST_CASE(t4_no_hook_at_cap)
{
    auto [txs, sys] = computeAssemblyPlan(5, 0, 5, false);
    BOOST_CHECK_EQUAL(txs, 5U);
    BOOST_CHECK_EQUAL(sys, 5U);
}

// T5 — hook fires but pending sizes are well below max: no collapse needed.
// max=10, only 3 normal + 2 sys queued; txsSize won't equal max, so the
// shrink branch must not fire — verifies the guard `txsSize == maxTxsPerBlock`.
BOOST_AUTO_TEST_CASE(t5_hook_with_room_below_cap)
{
    auto [txs, sys] = computeAssemblyPlan(10, 3, 2, true);
    BOOST_CHECK_EQUAL(txs, 5U);  // unchanged: not at cap
    BOOST_CHECK_EQUAL(sys, 2U);
}

// T6 — hook fires with empty sys queue: sys bound stays 0, normal drain
// gets txsSize slots even after the hook's deduction.
BOOST_AUTO_TEST_CASE(t6_hook_with_normal_only)
{
    auto [txs, sys] = computeAssemblyPlan(5, 5, 0, true);
    BOOST_CHECK_EQUAL(txs, 4U);
    BOOST_CHECK_EQUAL(sys, 0U);
}

// T7 — large max, no hook: sys cap (10) limits sys draw, normal fills rest.
BOOST_AUTO_TEST_CASE(t7_large_max_no_hook_caps_sys)
{
    auto [txs, sys] = computeAssemblyPlan(100, 200, 20, false);
    BOOST_CHECK_EQUAL(txs, 100U);
    BOOST_CHECK_EQUAL(sys, c_maxSysTxsPerBlock);  // 10
    // The drain leaves (100 - 10) = 90 normal slots.
}

// T8 — c_maxSysTxsPerBlock constant is honoured: sys queue much larger than
// the cap, no hook, normal pending suffices.
BOOST_AUTO_TEST_CASE(t8_sys_cap_honoured)
{
    auto [txs, sys] = computeAssemblyPlan(50, 100, 100, false);
    BOOST_CHECK_EQUAL(txs, 50U);
    BOOST_CHECK_EQUAL(sys, c_maxSysTxsPerBlock);
}

// T9 — empty queues + hook: bounds are zero, no underflow on size_t arithmetic.
BOOST_AUTO_TEST_CASE(t9_empty_queues_hook_inserts)
{
    auto [txs, sys] = computeAssemblyPlan(5, 0, 0, true);
    BOOST_CHECK_EQUAL(txs, 0U);
    BOOST_CHECK_EQUAL(sys, 0U);
}

// T10 — custom maxSysTxsPerBlock override (parameter-level isolation test):
// the function does not implicitly read a global, the cap comes from the arg.
BOOST_AUTO_TEST_CASE(t10_custom_sys_cap_parameter)
{
    auto [txs, sys] = computeAssemblyPlan(100, 50, 50, false, /*maxSys=*/3);
    BOOST_CHECK_EQUAL(txs, 100U);
    BOOST_CHECK_EQUAL(sys, 3U);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
