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
 * @file NodeConfigEthLaneVersionFeaturesTest.cpp
 * @brief v2/v3 genesis ignores [version] and [features]: the compat number is the binary's
 *        ETHEREUM_LANE_COMPATIBILITY_VERSION and no Features reach the genesis pin.
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
BOOST_AUTO_TEST_SUITE(NodeConfigEthLaneVersionFeaturesTest)

namespace
{
std::string laneGenesis(
    int executorVersion, std::optional<std::string> const& compat, std::string const& features)
{
    const std::string node =
        "1234567890123456789012345678901234567890123456789012345678901234"
        "1234567890123456789012345678901234567890123456789012345678901234";
    std::string genesis;
    if (compat)
    {
        genesis += "[version]\ncompatibility_version=" + *compat + "\n";
    }
    genesis += features;
    genesis +=
        "[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=1\n"
        "[web3]\nchain_id=1\n"
        "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\nleader_period=1\n"
        "node.0=" +
        node +
        ":1:1\n"
        "[tx]\ngas_limit=3000000000\n"
        "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
        "auth_admin_account=0x0000000000000000000000000000000000000001\n";
    if (executorVersion == ledger::ETHEREUM_EXECUTOR_VERSION)
    {
        genesis += "version=2\n";
    }
    else
    {
        genesis += "version=3\n[op_fork_timestamps]\njovian_time=0\n";
    }
    return genesis + ethLaneGenesisSections();
}

std::string pinOf(NodeConfig& cfg)
{
    return generateGenesisData(cfg.genesisConfig(), *cfg.ledgerConfig());
}
}  // namespace

BOOST_AUTO_TEST_CASE(versionSectionIsIgnored)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    for (int executorVersion :
        {ledger::ETHEREUM_EXECUTOR_VERSION, ledger::OPSTACK_EXECUTOR_VERSION})
    {
        BOOST_TEST_CONTEXT("executor.version=" << executorVersion)
        {
            NodeConfig reference(keyFactory);
            reference.loadGenesisConfigFromString(laneGenesis(executorVersion, "3.18.0", ""));
            BOOST_CHECK_EQUAL(
                reference.compatibilityVersion(), ledger::ETHEREUM_LANE_COMPATIBILITY_VERSION);

            // Absent, older, and a value below the old v2 floor of 3.18.0 all load the same
            // chain: the pin is byte-identical.
            for (std::optional<std::string> const& compat : {std::optional<std::string>{},
                     std::optional<std::string>{"3.0.0"}, std::optional<std::string>{"3.17.0"}})
            {
                BOOST_TEST_CONTEXT("compatibility_version=" << compat.value_or("<absent>"))
                {
                    NodeConfig cfg(keyFactory);
                    cfg.loadGenesisConfigFromString(laneGenesis(executorVersion, compat, ""));
                    BOOST_CHECK_EQUAL(
                        cfg.compatibilityVersion(), ledger::ETHEREUM_LANE_COMPATIBILITY_VERSION);
                    BOOST_CHECK_EQUAL(pinOf(cfg), pinOf(reference));
                }
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(featuresSectionIsIgnored)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    // An unknown name and a dependency violation: both are refused on v1, both ignored here.
    const std::string features = "[features]\nno_such_flag=1\nfeature_balance_precompiled=1\n";
    for (int executorVersion :
        {ledger::ETHEREUM_EXECUTOR_VERSION, ledger::OPSTACK_EXECUTOR_VERSION})
    {
        BOOST_TEST_CONTEXT("executor.version=" << executorVersion)
        {
            NodeConfig reference(keyFactory);
            reference.loadGenesisConfigFromString(laneGenesis(executorVersion, "3.18.0", ""));

            NodeConfig cfg(keyFactory);
            cfg.loadGenesisConfigFromString(laneGenesis(executorVersion, "3.18.0", features));
            BOOST_CHECK(cfg.genesisConfig().m_features.empty());
            BOOST_CHECK_EQUAL(pinOf(cfg), pinOf(reference));
        }
    }
}

// RPBFT needs the feature_rpbft / epoch rows an Ethereum-lane genesis does not write.
BOOST_AUTO_TEST_CASE(rpbftIsRejected)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    for (int executorVersion :
        {ledger::ETHEREUM_EXECUTOR_VERSION, ledger::OPSTACK_EXECUTOR_VERSION})
    {
        BOOST_TEST_CONTEXT("executor.version=" << executorVersion)
        {
            auto genesis = laneGenesis(executorVersion, "3.18.0", "");
            auto const pos = genesis.find("consensus_type=pbft");
            BOOST_REQUIRE(pos != std::string::npos);
            genesis.replace(pos, std::string("consensus_type=pbft").size(), "consensus_type=rpbft");

            NodeConfig cfg(keyFactory);
            BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(genesis), InvalidConfig,
                [](auto const& e) { return errinfoContains(e, "consensus_type=rpbft"); });
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
