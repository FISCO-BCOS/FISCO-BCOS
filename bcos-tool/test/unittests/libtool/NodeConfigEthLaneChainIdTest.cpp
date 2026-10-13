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
 * @file NodeConfigEthLaneChainIdTest.cpp
 * @brief v2/v3 take their chain id from [chain] chain_id only (a non-zero decimal uint64) and
 *        ignore [web3]; v0/v1 keep the string [chain] chain_id and the [web3] chain_id.
 */

#include "EthLaneGenesisFixture.h"
#include "ExceptionCheck.h"
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-tool/NodeConfig.h>
#include <boost/test/unit_test.hpp>
#include <optional>
#include <string>

using namespace bcos;
using namespace bcos::tool;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(NodeConfigEthLaneChainIdTest)

namespace
{
// @p chainId / @p web3ChainId: the value of [chain] chain_id / [web3] chain_id, or nullopt to
// leave the key out.
std::string genesisWith(int executorVersion, std::optional<std::string> const& chainId,
    std::optional<std::string> const& web3ChainId)
{
    const std::string node =
        "1234567890123456789012345678901234567890123456789012345678901234"
        "1234567890123456789012345678901234567890123456789012345678901234";
    std::string genesis = "[version]\ncompatibility_version=3.18.0\n";
    genesis += "[chain]\nsm_crypto=false\ngroup_id=group0\n";
    if (chainId)
    {
        genesis += "chain_id=" + *chainId + "\n";
    }
    if (web3ChainId)
    {
        genesis += "[web3]\nchain_id=" + *web3ChainId + "\n";
    }
    genesis +=
        "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\nleader_period=1\n"
        "node.0=" +
        node +
        ":1:1\n"
        "[tx]\ngas_limit=3000000000\n"
        "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
        "auth_admin_account=0x0000000000000000000000000000000000000001\n";
    if (executorVersion == ledger::ETHEREUM_EXECUTOR_VERSION)
    {
        return genesis + "version=2\nevm_revision=cancun\n" + ethLaneGenesisSections();
    }
    if (executorVersion == ledger::OPSTACK_EXECUTOR_VERSION)
    {
        return genesis + "version=3\n[op_fork_timestamps]\njovian_time=0\n" +
               ethLaneGenesisSections();
    }
    return genesis + "version=" + std::to_string(executorVersion) + "\n";
}

constexpr int c_ethLanes[] = {ledger::ETHEREUM_EXECUTOR_VERSION, ledger::OPSTACK_EXECUTOR_VERSION};
}  // namespace

BOOST_AUTO_TEST_CASE(ethLaneTakesDecimalChainIdFromChainSection)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    for (int executorVersion : c_ethLanes)
    {
        BOOST_TEST_CONTEXT("executor.version=" << executorVersion)
        {
            // [web3] chain_id is ignored, even when it disagrees.
            for (std::optional<std::string> const& web3 :
                {std::optional<std::string>{}, std::optional<std::string>{"5"}})
            {
                BOOST_TEST_CONTEXT("[web3] chain_id=" << web3.value_or("<absent>"))
                {
                    NodeConfig cfg(keyFactory);
                    cfg.loadGenesisConfigFromString(genesisWith(executorVersion, "901", web3));
                    BOOST_CHECK_EQUAL(cfg.genesisConfig().m_chainID, "901");
                    BOOST_CHECK_EQUAL(cfg.genesisConfig().m_web3ChainID, "901");
                }
            }

            NodeConfig max(keyFactory);
            max.loadGenesisConfigFromString(
                genesisWith(executorVersion, "18446744073709551615", std::nullopt));
            BOOST_CHECK_EQUAL(max.genesisConfig().m_web3ChainID, "18446744073709551615");
        }
    }
}

BOOST_AUTO_TEST_CASE(ethLaneRefusesBadChainId)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    struct Case
    {
        std::optional<std::string> chainId;
        std::optional<std::string> web3ChainId;
        std::string reason;
    };
    const Case cases[] = {
        {"chain0", std::nullopt, "[chain].chain_id must be decimal"},
        {"0x385", std::nullopt, "[chain].chain_id must be decimal"},
        {"0", std::nullopt, "[chain].chain_id must be non-zero"},
        {"000", std::nullopt, "[chain].chain_id must be non-zero"},
        {"007", std::nullopt, "[chain].chain_id must not have leading zeros"},
        {"18446744073709551616", std::nullopt, "[chain].chain_id must fit uint64"},
        {std::nullopt, std::nullopt, "requires [chain] chain_id"},
        // Only [web3]: the lane no longer reads it.
        {std::nullopt, "901", "requires [chain] chain_id"},
    };
    for (int executorVersion : c_ethLanes)
    {
        for (auto const& c : cases)
        {
            BOOST_TEST_CONTEXT(
                "executor.version=" << executorVersion
                                    << " [chain] chain_id=" << c.chainId.value_or("<absent>")
                                    << " [web3] chain_id=" << c.web3ChainId.value_or("<absent>"))
            {
                NodeConfig cfg(keyFactory);
                BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(
                                          genesisWith(executorVersion, c.chainId, c.web3ChainId)),
                    InvalidConfig, [&c](auto const& e) { return errinfoContains(e, c.reason); });
            }
        }
    }
}

// v0/v1 are unchanged: [chain] chain_id is a free-form name and [web3] chain_id is the EVM
// chain id.
BOOST_AUTO_TEST_CASE(v1KeepsWeb3ChainId)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    NodeConfig cfg(keyFactory);
    cfg.loadGenesisConfigFromString(genesisWith(1, "chain0", "42"));
    BOOST_CHECK_EQUAL(cfg.genesisConfig().m_chainID, "chain0");
    BOOST_CHECK_EQUAL(cfg.genesisConfig().m_web3ChainID, "42");

    NodeConfig absent(keyFactory);
    absent.loadGenesisConfigFromString(genesisWith(1, "chain0", std::nullopt));
    BOOST_CHECK_EQUAL(absent.genesisConfig().m_web3ChainID, "0");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
