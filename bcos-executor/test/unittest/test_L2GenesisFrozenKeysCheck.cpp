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
 * @file test_L2GenesisFrozenKeysCheck.cpp
 * @brief checkL2GenesisFrozenKeys: the three genesis-frozen SystemConfig keys (chain_id,
 *        gas_limit, compatibility_version) must equal the node's config.genesis, and a
 *        mismatch message must name both places so the operator knows which file to fix.
 */
#include <bcos-framework/ledger/L2ConfigLoader.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <boost/test/unit_test.hpp>
#include <cstdint>
#include <string>

using namespace bcos;
using namespace bcos::ledger;

namespace l2_genesis_frozen_keys_check_test
{
namespace
{
evmc_uint256be chainIdWord(uint64_t chainId)
{
    evmc_uint256be word{};
    for (int shift = 56, i = 24; shift >= 0; shift -= 8, ++i)
    {
        word.bytes[i] = static_cast<uint8_t>((chainId >> shift) & 0xFFU);
    }
    return word;
}

LedgerConfig loadedConfig()
{
    LedgerConfig config;
    config.setChainId(chainIdWord(20200));
    config.setGasLimit({30'000'000, 0});
    config.setBlockTxCountLimit(1000);
    config.setCompatibilityVersion(0x03120000);
    return config;
}

L2GenesisFrozenNodeConfig matchingNode()
{
    return {
        .web3ChainId = u256(20200), .txGasLimit = 30'000'000, .compatibilityVersion = 0x03120000};
}

bool mentions(std::string const& message, std::string const& needle)
{
    return message.find(needle) != std::string::npos;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(L2GenesisFrozenKeysCheckTest)

BOOST_AUTO_TEST_CASE(AllThreeAgreeIsAccepted)
{
    BOOST_CHECK(!checkL2GenesisFrozenKeys(loadedConfig(), matchingNode()).has_value());
}

BOOST_AUTO_TEST_CASE(BlockTxCountLimitIsNotCompared)
{
    // Runtime-writable on the contract: governance may move it away from whatever the node's
    // consensus.block_tx_count_limit says, and the check must not turn that into a refusal.
    auto config = loadedConfig();
    config.setBlockTxCountLimit(3);
    BOOST_CHECK(!checkL2GenesisFrozenKeys(config, matchingNode()).has_value());
}

BOOST_AUTO_TEST_CASE(ChainIdMismatchNamesBothPlaces)
{
    auto node = matchingNode();
    node.web3ChainId = u256(31337);
    auto result = checkL2GenesisFrozenKeys(loadedConfig(), node);
    BOOST_REQUIRE(result.has_value());
    BOOST_CHECK_MESSAGE(mentions(*result, "SystemConfig chain_id"), *result);
    BOOST_CHECK_MESSAGE(
        mentions(*result, "0x43000000000000000000000000000000000000c0 slot chain_id"), *result);
    BOOST_CHECK_MESSAGE(mentions(*result, "= 20200"), *result);
    BOOST_CHECK_MESSAGE(mentions(*result, "config.genesis [web3] chain_id = 31337"), *result);
}

/// Defensive branch: unreachable after a successful load (a missing slot throws in the loader),
/// pinned so a caller that skipped the load cannot get a false "all agree".
BOOST_AUTO_TEST_CASE(MissingChainIdIsAMismatch)
{
    LedgerConfig config;
    config.setGasLimit({30'000'000, 0});
    config.setCompatibilityVersion(0x03120000);
    auto result = checkL2GenesisFrozenKeys(config, matchingNode());
    BOOST_REQUIRE(result.has_value());
    BOOST_CHECK_MESSAGE(mentions(*result, "chain_id"), *result);
    BOOST_CHECK_MESSAGE(mentions(*result, "[web3] chain_id = 20200"), *result);
}

BOOST_AUTO_TEST_CASE(GasLimitMismatchNamesBothPlaces)
{
    auto node = matchingNode();
    node.txGasLimit = 3'000'000'000;
    auto result = checkL2GenesisFrozenKeys(loadedConfig(), node);
    BOOST_REQUIRE(result.has_value());
    BOOST_CHECK_MESSAGE(mentions(*result, "SystemConfig gas_limit"), *result);
    BOOST_CHECK_MESSAGE(mentions(*result, "= 30000000"), *result);
    BOOST_CHECK_MESSAGE(mentions(*result, "config.genesis [tx] gas_limit = 3000000000"), *result);
}

BOOST_AUTO_TEST_CASE(CompatibilityVersionMismatchNamesBothPlaces)
{
    auto node = matchingNode();
    node.compatibilityVersion = 0x03110000;
    auto result = checkL2GenesisFrozenKeys(loadedConfig(), node);
    BOOST_REQUIRE(result.has_value());
    BOOST_CHECK_MESSAGE(mentions(*result, "SystemConfig compatibility_version"), *result);
    BOOST_CHECK_MESSAGE(mentions(*result, "= 0x3120000"), *result);
    BOOST_CHECK_MESSAGE(
        mentions(*result, "config.genesis [version] compatibility_version = 0x3110000"), *result);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace l2_genesis_frozen_keys_check_test
