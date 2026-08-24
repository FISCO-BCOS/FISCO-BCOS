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
 * @brief Unit tests for Prague EVM features: EIP-7623 calldata floor cost,
 *        EIP-7702 placeholder, BLS12-381 precompile gas costs, p256verify gas cost.
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

// ===== EIP-7623: calldata floor cost =========================================
// Arithmetic test — verifies the floor formula without a full executor stack.
// Spec: calldataGas = max(normalDataCost, numTokens * TOTAL_COST_FLOOR_PER_TOKEN)
//   where each zero byte counts as 1 token (cost 4) and each non-zero byte
//   counts as TOKENS_PER_NONZERO_BYTE tokens (cost 16).

BOOST_AUTO_TEST_CASE(eip7623_calldata_cost)
{
    using namespace bcos::executor;

    auto calcCalldataGas = [](const std::vector<uint8_t>& data) -> int64_t {
        return calcEip7623CalldataGas(ref(data));
    };

    // Empty calldata: normalDataCost=0, numTokens=0, floor=0 → gas=0
    BOOST_CHECK_EQUAL(calcCalldataGas({}), 0);

    // 100 zero bytes: normalDataCost=400, numTokens=100, floor=1000 → gas=1000
    BOOST_CHECK_EQUAL(calcCalldataGas(std::vector<uint8_t>(100, 0x00)), 1000);

    // 100 non-zero bytes: normalDataCost=1600, numTokens=400, floor=4000 → gas=4000
    BOOST_CHECK_EQUAL(calcCalldataGas(std::vector<uint8_t>(100, 0xff)), 4000);

    // Mixed: 50 zero + 50 nonzero
    //   normalDataCost = 50*4 + 50*16 = 200 + 800 = 1000
    //   numTokens      = 50*1 + 50*4  = 50 + 200  = 250
    //   floor          = 250 * 10     = 2500
    //   calldataGas    = max(1000, 2500) = 2500
    std::vector<uint8_t> mixed(100);
    for (int i = 0; i < 50; ++i)
        mixed[i] = 0x00;
    for (int i = 50; i < 100; ++i)
        mixed[i] = 0x42;
    BOOST_CHECK_EQUAL(calcCalldataGas(mixed), 2500);

    // Single non-zero byte: normalDataCost=16, numTokens=4, floor=40 → gas=40
    BOOST_CHECK_EQUAL(calcCalldataGas({0x01}), 40);

    // Single zero byte: normalDataCost=4, numTokens=1, floor=10 → gas=10
    BOOST_CHECK_EQUAL(calcCalldataGas({0x00}), 10);
}

// ===== EIP-7702: authorization list ==========================================
// EIP-7702 type-4 transactions introduce an authorization_list field.
// BLOCKED: bcos-framework/bcos-protocol has no authorization_list in its
// transaction types.  Implementation is deferred until the protocol layer
// adds support.  This placeholder ensures the suite compiles and that the
// gap is visible in CI output.

BOOST_AUTO_TEST_CASE(eip7702_authorization_list_skipped)
{
    // No authorization_list field available in bcos-protocol transaction types.
    // Full EIP-7702 testing is deferred until protocol support lands.
    BOOST_TEST_MESSAGE("SKIP: EIP-7702 deferred — no authorization_list in bcos-protocol");
    BOOST_CHECK(true);
}

// ===== BLS12-381 precompile gas costs ========================================
// EIP-2537 (Prague) adds BLS12-381 precompiles.  Gas costs are flat for add/map
// operations and per-pair for pairing.  These tests call the registered pricers
// directly — no EVM execution needed.

BOOST_AUTO_TEST_CASE(bls_precompile_gas)
{
    bytes empty;

    // bls12_g1add: flat 375 gas (EIP-2537)
    BOOST_CHECK_EQUAL(executor::PrecompiledRegistrar::pricer("bls12_g1add")(ref(empty)), 375);

    // bls12_g2add: flat 600 gas (EIP-2537)
    BOOST_CHECK_EQUAL(executor::PrecompiledRegistrar::pricer("bls12_g2add")(ref(empty)), 600);

    // bls12_map_fp_to_g1: flat 5500 gas (EIP-2537)
    BOOST_CHECK_EQUAL(
        executor::PrecompiledRegistrar::pricer("bls12_map_fp_to_g1")(ref(empty)), 5500);

    // bls12_map_fp2_to_g2: flat 23800 gas (EIP-2537)
    BOOST_CHECK_EQUAL(
        executor::PrecompiledRegistrar::pricer("bls12_map_fp2_to_g2")(ref(empty)), 23800);

    // bls12_pairing_check: 37700 base + 32600 per pair (pair = 384 bytes: G1_SIZE=128 +
    // G2_SIZE=256)
    // 1 pair = 384 bytes
    bytes one_pair(384, 0);
    BOOST_CHECK_EQUAL(executor::PrecompiledRegistrar::pricer("bls12_pairing_check")(ref(one_pair)),
        37700 + 32600);

    // 2 pairs = 768 bytes
    bytes two_pairs(768, 0);
    BOOST_CHECK_EQUAL(executor::PrecompiledRegistrar::pricer("bls12_pairing_check")(ref(two_pairs)),
        37700 + 2 * 32600);
}

// ===== p256verify gas cost ===================================================
// EIP-7212 specifies a flat gas cost of 6900 for the secp256r1 verify precompile.

BOOST_AUTO_TEST_CASE(p256verify_precompile_gas)
{
    bytes empty;
    BOOST_CHECK_EQUAL(executor::PrecompiledRegistrar::pricer("p256verify")(ref(empty)), 6900);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
