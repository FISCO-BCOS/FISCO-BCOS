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
 * @brief Prague end-to-end test skeleton: schedule flag verification, precompile
 *        gas budget checks (EIP-2537 BLS, EIP-7212 p256verify), and stubs for
 *        tests that require a live executor stack.
 * @file PragueE2ETest.cpp
 */

#include "bcos-utilities/Common.h"
#include "vm/Precompiled.h"
#include <Common.h>
#include <boost/test/unit_test.hpp>
#include <vector>

namespace bcos::test
{

BOOST_AUTO_TEST_SUITE(PragueE2ETest)

// ===== full_block_execution stub =============================================
// Requires a live BlockContext + TransactionExecutive stack — deferred until
// the integration test harness is available.

BOOST_AUTO_TEST_CASE(full_block_execution_stub)
{
    BOOST_TEST_MESSAGE("SKIP: full_block_execution — requires live executor stack");
    BOOST_CHECK(true);
}

// ===== Prague schedule flags =================================================
// Verify that FiscoBcosSchedulePrague has exactly the expected feature flags.

BOOST_AUTO_TEST_CASE(prague_schedule_flags)
{
    using namespace bcos::executor;

    BOOST_CHECK(FiscoBcosSchedulePrague.enableCanCun);
    BOOST_CHECK(FiscoBcosSchedulePrague.enablePrague);
    BOOST_CHECK(FiscoBcosSchedulePrague.enablePairs);
    BOOST_CHECK(!FiscoBcosSchedulePrague.enableOsaka);
    BOOST_CHECK_EQUAL(FiscoBcosSchedulePrague.maxEvmCodeSize, 0x100000u);
    BOOST_CHECK_EQUAL(FiscoBcosSchedulePrague.maxWasmCodeSize, 0xF00000u);
}

// ===== Osaka schedule flags ==================================================
// Verify that FiscoBcosScheduleOsaka inherits all Prague flags and enables Osaka.

BOOST_AUTO_TEST_CASE(osaka_schedule_flags)
{
    using namespace bcos::executor;

    BOOST_CHECK(FiscoBcosScheduleOsaka.enableCanCun);
    BOOST_CHECK(FiscoBcosScheduleOsaka.enablePrague);
    BOOST_CHECK(FiscoBcosScheduleOsaka.enableOsaka);
    BOOST_CHECK(FiscoBcosScheduleOsaka.enablePairs);
    BOOST_CHECK_EQUAL(FiscoBcosScheduleOsaka.maxEvmCodeSize, 0x100000u);
    BOOST_CHECK_EQUAL(FiscoBcosScheduleOsaka.maxWasmCodeSize, 0xF00000u);
}

// ===== Prague vs Cancun schedule differences =================================
// Verify that Prague enables features absent in the Cancun schedule.

BOOST_AUTO_TEST_CASE(prague_vs_cancun_gas)
{
    using namespace bcos::executor;

    // Cancun does not enable Prague features
    BOOST_CHECK(!FiscoBcosScheduleCancun.enablePrague);
    BOOST_CHECK(!FiscoBcosScheduleCancun.enableOsaka);
    // Cancun: enableCanCun=true, enablePairs=false (not a FISCO custom flag until v3.2)
    BOOST_CHECK(FiscoBcosScheduleCancun.enableCanCun);
    BOOST_CHECK(!FiscoBcosScheduleCancun.enablePairs);

    // Prague adds enablePrague on top of Cancun
    BOOST_CHECK(FiscoBcosSchedulePrague.enablePrague);
    BOOST_CHECK(!FiscoBcosSchedulePrague.enableOsaka);
    // Prague adds enablePairs=true on top of Cancun
    BOOST_CHECK(FiscoBcosSchedulePrague.enablePairs);  // BLS gating flag

    // Both share the same code-size limits introduced with Cancun / V320
    BOOST_CHECK_EQUAL(
        FiscoBcosScheduleCancun.maxEvmCodeSize, FiscoBcosSchedulePrague.maxEvmCodeSize);
    BOOST_CHECK_EQUAL(
        FiscoBcosScheduleCancun.maxWasmCodeSize, FiscoBcosSchedulePrague.maxWasmCodeSize);
}

// ===== Precompile gas budget =================================================
// Verify that the pricers registered for Prague precompiles return the gas costs
// mandated by the respective EIPs.  No EVM execution is required — the pricers
// are called directly.

BOOST_AUTO_TEST_CASE(prague_precompile_gas_budget)
{
    bytes empty;

    // p256verify: 6900 flat (EIP-7212)
    BOOST_CHECK_EQUAL(executor::PrecompiledRegistrar::pricer("p256verify")(ref(empty)), 6900);

    // BLS G1 add: 375 flat (EIP-2537)
    BOOST_CHECK_EQUAL(executor::PrecompiledRegistrar::pricer("bls12_g1add")(ref(empty)), 375);

    // BLS G2 add: 600 flat (EIP-2537)
    BOOST_CHECK_EQUAL(executor::PrecompiledRegistrar::pricer("bls12_g2add")(ref(empty)), 600);

    // BLS map-fp-to-G1: 5500 flat (EIP-2537)
    BOOST_CHECK_EQUAL(
        executor::PrecompiledRegistrar::pricer("bls12_map_fp_to_g1")(ref(empty)), 5500);

    // BLS map-fp2-to-G2: 23800 flat (EIP-2537)
    BOOST_CHECK_EQUAL(
        executor::PrecompiledRegistrar::pricer("bls12_map_fp2_to_g2")(ref(empty)), 23800);

    // BLS pairing check: 37700 base + 32600 per pair; 1 pair = 384 bytes (EIP-2537)
    bytes one_pair(384, 0);
    BOOST_CHECK_EQUAL(executor::PrecompiledRegistrar::pricer("bls12_pairing_check")(ref(one_pair)),
        37700 + 32600);

    // 2 pairs = 768 bytes
    bytes two_pairs(768, 0);
    BOOST_CHECK_EQUAL(
        executor::PrecompiledRegistrar::pricer("bls12_pairing_check")(bytesConstRef(&two_pairs)),
        u256(37700 + 2 * 32600));
}

// ===== Modexp precompile stub ================================================
// modexp gas computation is input-dependent (base, exp, mod sizes).
// Full arithmetic test is in EvmPrecompiledTest::modexpCompatibility.
// Verify the precompile is registered by checking the pricer doesn't crash on
// empty input.

BOOST_AUTO_TEST_CASE(modexp_precompile_stub)
{
    BOOST_TEST_MESSAGE("INFO: modexp gas is tested in EvmPrecompiledTest::modexpCompatibility");
    // Just verify the pricer doesn't crash on empty input
    bytes empty;
    auto gas = executor::PrecompiledRegistrar::pricer("modexp")(bytesConstRef(&empty));
    BOOST_CHECK_GE(gas, 0);  // empty input yields 0 gas; minimum 200 applies to non-empty input
}

// ===== Blob transaction stub =================================================
// EIP-4844 / EIP-7691 blob transactions are conditional on FISCO-BCOS blob
// support.  FISCO-BCOS uses PBFT (not PoS), so blob transactions are not
// yet supported.

BOOST_AUTO_TEST_CASE(prague_blob_transactions_stub)
{
    BOOST_TEST_MESSAGE("SKIP: blob_transactions — FISCO-BCOS PBFT does not support EIP-4844 blobs");
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
