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
 * @brief Unit tests for Prague EVM features: EIP-7623 calldata floor,
 *        BLS12-381 and p256verify precompile gas costs.
 * @file PragueTest.cpp
 */

#include "bcos-utilities/Common.h"
#include "vm/Precompiled.h"
#include <Common.h>
#include <boost/test/unit_test.hpp>
#include <vector>

namespace bcos::test
{

BOOST_AUTO_TEST_SUITE(PragueTest)

BOOST_AUTO_TEST_CASE(eip7623_calldata_cost)
{
    using namespace bcos::executor;

    auto calcCalldataGas = [](const std::vector<uint8_t>& data) -> int64_t {
        return calcEip7623CalldataGas(ref(data));
    };

    BOOST_CHECK_EQUAL(calcCalldataGas({}), 0);
    BOOST_CHECK_EQUAL(calcCalldataGas(std::vector<uint8_t>(100, 0x00)), 1000);
    BOOST_CHECK_EQUAL(calcCalldataGas(std::vector<uint8_t>(100, 0xff)), 4000);

    std::vector<uint8_t> mixed(100);
    for (int i = 0; i < 50; ++i)
        mixed[i] = 0x00;
    for (int i = 50; i < 100; ++i)
        mixed[i] = 0x42;
    BOOST_CHECK_EQUAL(calcCalldataGas(mixed), 2500);
    BOOST_CHECK_EQUAL(calcCalldataGas({0x01}), 40);
    BOOST_CHECK_EQUAL(calcCalldataGas({0x00}), 10);
}

BOOST_AUTO_TEST_CASE(bls_precompile_gas)
{
    bytes empty;
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
    BOOST_CHECK_EQUAL(executor::PrecompiledRegistrar::pricer("bls12_pairing_check")(ref(two_pairs)),
        37700 + 2 * 32600);
}

BOOST_AUTO_TEST_CASE(p256verify_precompile_gas)
{
    bytes empty;
    BOOST_CHECK_EQUAL(executor::PrecompiledRegistrar::pricer("p256verify")(ref(empty)), 6900);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
