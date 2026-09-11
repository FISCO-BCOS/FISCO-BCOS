/**
 * Copyright (C) 2026 FISCO BCOS.
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @file EthConfigTest.cpp
 * @brief EIP-7910 `eth_config` builders and method registration.
 */

#include <bcos-rpc/web3jsonrpc/endpoints/EndpointsMapping.h>
#include <bcos-rpc/web3jsonrpc/utils/EthConfig.h>
#include <json/json.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::rpc;

BOOST_AUTO_TEST_SUITE(EthConfigTest)

// EIP-7910 precompiles: Cancun adds KZG_POINT_EVALUATION; Prague adds the BLS12 set.
BOOST_AUTO_TEST_CASE(precompilesByRevision)
{
    auto cancun = ethConfigPrecompiles(EVMC_CANCUN);
    BOOST_CHECK_EQUAL(cancun.size(), 10u);
    BOOST_CHECK(cancun.isMember("ECREC"));
    BOOST_CHECK(cancun.isMember("KZG_POINT_EVALUATION"));
    BOOST_CHECK(!cancun.isMember("BLS12_G1ADD"));
    BOOST_CHECK_EQUAL(cancun["ECREC"].asString(), "0x0000000000000000000000000000000000000001");
    BOOST_CHECK_EQUAL(
        cancun["KZG_POINT_EVALUATION"].asString(), "0x000000000000000000000000000000000000000a");

    auto prague = ethConfigPrecompiles(EVMC_PRAGUE);
    BOOST_CHECK_EQUAL(prague.size(), 17u);
    BOOST_CHECK(prague.isMember("BLS12_G1ADD"));
    BOOST_CHECK(prague.isMember("BLS12_MAP_FP2_TO_G2"));
}

// EIP-7910 systemContracts: Cancun -> beacon roots; Prague adds history storage + the L1-only
// request predeploys; an OP L2 reports only the L2-relevant subset.
BOOST_AUTO_TEST_CASE(systemContractsByRevisionAndL2)
{
    auto cancun = ethConfigSystemContracts(EVMC_CANCUN, false);
    BOOST_CHECK_EQUAL(cancun.size(), 1u);
    BOOST_CHECK(cancun.isMember("BEACON_ROOTS_ADDRESS"));
    BOOST_CHECK_EQUAL(
        cancun["BEACON_ROOTS_ADDRESS"].asString(), "0x000f3df6d732807ef1319fb7b8bb8522d0beac02");

    auto pragueL1 = ethConfigSystemContracts(EVMC_PRAGUE, false);
    BOOST_CHECK_EQUAL(pragueL1.size(), 5u);
    BOOST_CHECK(pragueL1.isMember("HISTORY_STORAGE_ADDRESS"));
    BOOST_CHECK(pragueL1.isMember("DEPOSIT_CONTRACT_ADDRESS"));

    auto pragueL2 = ethConfigSystemContracts(EVMC_PRAGUE, true);
    BOOST_CHECK_EQUAL(pragueL2.size(), 2u);  // beacon roots + history storage only
    BOOST_CHECK(pragueL2.isMember("BEACON_ROOTS_ADDRESS"));
    BOOST_CHECK(pragueL2.isMember("HISTORY_STORAGE_ADDRESS"));
    BOOST_CHECK(!pragueL2.isMember("DEPOSIT_CONTRACT_ADDRESS"));

    // Pre-Cancun: the field must be omitted (the builder returns an empty object).
    BOOST_CHECK(ethConfigSystemContracts(EVMC_SHANGHAI, false).empty());
}

// One EthForkConfig object: number-typed activationTime / blobSchedule, hex chainId / forkId.
BOOST_AUTO_TEST_CASE(forkConfigShape)
{
    auto cfg = buildEthForkConfig(EVMC_PRAGUE, 914901, "0x1bebcd", true);
    BOOST_CHECK_EQUAL(cfg["activationTime"].asUInt64(), 0u);
    BOOST_CHECK_EQUAL(cfg["chainId"].asString(), "0xdf5d5");
    BOOST_CHECK_EQUAL(cfg["forkId"].asString(), "0x1bebcd");
    BOOST_CHECK_EQUAL(cfg["blobSchedule"]["target"].asUInt64(), 6u);
    BOOST_CHECK_EQUAL(cfg["blobSchedule"]["max"].asUInt64(), 9u);
    BOOST_CHECK_EQUAL(cfg["blobSchedule"]["baseFeeUpdateFraction"].asUInt64(), 5007716u);
    BOOST_CHECK(cfg.isMember("precompiles"));
    BOOST_CHECK(cfg.isMember("systemContracts"));

    auto cancun = buildEthForkConfig(EVMC_CANCUN, 1, "0x00000000", false);
    BOOST_CHECK_EQUAL(cancun["chainId"].asString(), "0x1");
    BOOST_CHECK_EQUAL(cancun["blobSchedule"]["target"].asUInt64(), 3u);
    BOOST_CHECK_EQUAL(cancun["blobSchedule"]["max"].asUInt64(), 6u);
}

// The full eth_config result: current present, next/last null on FISCO.
BOOST_AUTO_TEST_CASE(ethConfigShape)
{
    auto result = buildEthConfig(EVMC_PRAGUE, 914901, "0x0929e24e", true);
    BOOST_REQUIRE(result.isMember("current"));
    BOOST_CHECK(result["next"].isNull());
    BOOST_CHECK(result["last"].isNull());
    BOOST_CHECK_EQUAL(result["current"]["chainId"].asString(), "0xdf5d5");
}

// EIP-2124 fork id: CRC32(genesis || be64(fork)) folded to 31 bits, 0x-prefixed 8 hex.
BOOST_AUTO_TEST_CASE(forkIdEip2124)
{
    std::string const zeros = "0x" + std::string(64, '0');
    BOOST_CHECK_EQUAL(ethForkIdHex(zeros, {0}), "0x69ec3db1");

    auto id = ethForkIdHex("0x" + std::string(64, '1'), {0});
    BOOST_CHECK_EQUAL(id.size(), 10u);
    BOOST_CHECK_EQUAL(id.substr(0, 2), "0x");
    BOOST_CHECK_EQUAL(id, "0x020735ed");  // deterministic golden

    // No genesis hash -> the zero fork id, never throws.
    BOOST_CHECK_EQUAL(ethForkIdHex("", {0}), "0x00000000");
}

// The method must be dispatched (an unregistered method answers -32601 to the CL).
BOOST_AUTO_TEST_CASE(ethConfigIsRegistered)
{
    EndpointsMapping mapping;
    BOOST_CHECK_MESSAGE(mapping.findHandler("eth_config").has_value(), "eth_config not dispatched");
}

BOOST_AUTO_TEST_SUITE_END()
