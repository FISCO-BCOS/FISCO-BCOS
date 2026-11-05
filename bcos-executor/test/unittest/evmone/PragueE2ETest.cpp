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
 * @brief Prague end-to-end tests for schedule flags and precompile gas budgets.
 * @file PragueE2ETest.cpp
 */

#include "bcos-utilities/Common.h"
#include "vm/Precompiled.h"
#include <Common.h>
#include <boost/test/unit_test.hpp>

namespace bcos::test
{

BOOST_AUTO_TEST_SUITE(PragueE2ETest)

BOOST_AUTO_TEST_CASE(prague_schedule_flags)
{
    using namespace bcos::executor;
    BOOST_CHECK(FiscoBcosSchedulePrague.enableCanCun);
    BOOST_CHECK(FiscoBcosSchedulePrague.enablePrague);
    BOOST_CHECK(FiscoBcosSchedulePrague.enablePairs);
    BOOST_CHECK(!FiscoBcosSchedulePrague.enableOsaka);
    BOOST_CHECK_EQUAL(FiscoBcosSchedulePrague.maxEvmCodeSize, 0x100000u);
}

BOOST_AUTO_TEST_CASE(osaka_schedule_flags)
{
    using namespace bcos::executor;
    BOOST_CHECK(FiscoBcosScheduleOsaka.enableCanCun);
    BOOST_CHECK(FiscoBcosScheduleOsaka.enablePrague);
    BOOST_CHECK(FiscoBcosScheduleOsaka.enableOsaka);
    BOOST_CHECK(FiscoBcosScheduleOsaka.enablePairs);
    BOOST_CHECK_EQUAL(FiscoBcosScheduleOsaka.maxEvmCodeSize, 0x100000u);
}

BOOST_AUTO_TEST_CASE(prague_vs_cancun_gas)
{
    using namespace bcos::executor;
    BOOST_CHECK(!FiscoBcosScheduleCancun.enablePrague);
    BOOST_CHECK(!FiscoBcosScheduleCancun.enableOsaka);
    BOOST_CHECK(FiscoBcosScheduleCancun.enableCanCun);
    BOOST_CHECK(!FiscoBcosScheduleCancun.enablePairs);
    BOOST_CHECK(FiscoBcosSchedulePrague.enablePrague);
    BOOST_CHECK(!FiscoBcosSchedulePrague.enableOsaka);
    BOOST_CHECK(FiscoBcosSchedulePrague.enablePairs);
    BOOST_CHECK_EQUAL(
        FiscoBcosScheduleCancun.maxEvmCodeSize, FiscoBcosSchedulePrague.maxEvmCodeSize);
}

BOOST_AUTO_TEST_CASE(prague_precompile_gas_budget)
{
    bytes empty;
    BOOST_CHECK_EQUAL(executor::PrecompiledRegistrar::pricer("p256verify")(ref(empty)), 6900);
    BOOST_CHECK_EQUAL(executor::PrecompiledRegistrar::pricer("bls12_g1add")(ref(empty)), 375);
    BOOST_CHECK_EQUAL(executor::PrecompiledRegistrar::pricer("bls12_g2add")(ref(empty)), 600);
    BOOST_CHECK_EQUAL(
        executor::PrecompiledRegistrar::pricer("bls12_map_fp_to_g1")(ref(empty)), 5500);
    BOOST_CHECK_EQUAL(
        executor::PrecompiledRegistrar::pricer("bls12_map_fp2_to_g2")(ref(empty)), 23800);

    bytes one_pair(384, 0);
    BOOST_CHECK_EQUAL(executor::PrecompiledRegistrar::pricer("bls12_pairing_check")(ref(one_pair)),
        37700 + 32600);
    bytes two_pairs(768, 0);
    BOOST_CHECK_EQUAL(
        executor::PrecompiledRegistrar::pricer("bls12_pairing_check")(bytesConstRef(&two_pairs)),
        u256(37700 + 2 * 32600));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
