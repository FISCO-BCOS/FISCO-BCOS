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
 * @file NodeConfigDeadKeysTest.cpp
 * @brief keys that no longer have an effect: the node starts and NodeConfig::deadKeys() lists
 * each present key once; config.ini keys a lane merely does not use are not listed.
 */

#include "EthLaneGenesisFixture.h"
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-tool/NodeConfig.h>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <string>
#include <vector>

using namespace bcos;
using namespace bcos::tool;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(NodeConfigDeadKeysTest)

namespace
{
using Keys = std::vector<std::string>;

std::string genesisFor(int version, std::string const& compat = "3.18.0")
{
    // [version] is a v0/v1 key; v2/v3 list it as dead
    std::string genesis =
        (version < 2 ? "[version]\ncompatibility_version=" + compat + "\n" : std::string()) +
        "[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=" +
        std::string(version >= 2 ? "11155420" : "chain0") +
        "\n"
        "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\nleader_period=1\n"
        "node.0=" +
        std::string(128, '1') +
        ":1:1\n"
        "[tx]\ngas_limit=3000000000\n"
        "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
        "auth_admin_account=0x0000000000000000000000000000000000000001\n"
        "version=" +
        std::to_string(version) + "\n";
    return genesis;
}

// The lane's own sections, appended after any extra [executor] keys the case adds.
std::string laneSections(int version)
{
    if (version == 2)
    {
        return "[fork_timestamps]\nlondon_time=0\nparis_time=0\nshanghai_time=0\ncancun_time=0\n"
               "prague_time=0\nmerge_block=0\n" +
               ethLaneGenesisSections();
    }
    if (version >= 3)
    {
        return "[op_fork_timestamps]\njovian_time=0\nkarst_time=0\n" + ethLaneGenesisSections();
    }
    return "";
}

// Loads the pair the way a node does (genesis first), runs the startup check, and returns the
// started config.
std::unique_ptr<NodeConfig> starts(std::string const& genesis, std::string const& ini)
{
    auto cfg = std::make_unique<NodeConfig>(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    BOOST_REQUIRE_NO_THROW(cfg->loadGenesisConfigFromString(genesis));
    BOOST_REQUIRE_NO_THROW(cfg->loadConfigFromString(ini));
    BOOST_REQUIRE_NO_THROW(cfg->validateELModeInvariants());
    return cfg;
}

// Every config.ini key of the table, in table order.
constexpr auto c_deadIni =
    "[web3_rpc]\nhttp_body_size_limit=10240000\n"
    "[executor]\nbaseline_scheduler=false\nbaseline_scheduler_parallel=false\n"
    "[storage]\npd_ssl_ca_path=\npd_ssl_cert_path=\npd_ssl_key_path=\n"
    "[redis]\nserver_ip=127.0.0.1\n"
    "[flow_control]\nenable_distributed_ratelimit=false\n"
    "enable_distributed_ratelimit_cache=true\ndistributed_ratelimit_cache_percent=20\n"
    "[security]\nhsm_public_key_file_path=\n"
    "[experimental]\ncheck_parallel_conflict=true\n"
    "[consensus]\nenable_single_node_consensus=true\nblock_interval=250\n"
    "produce_empty_blocks=false\nfee_recipient=0x0000000000000000000000000000000000000001\n"
    "prev_randao=\nfixed_timestamp=1\n"
    "[op_engine_rpc]\nenable=true\n"
    "[engine_rpc]\nenable=true\n"
    "[ethereum]\nmode=el\n"
    "[chain]\nchain_id=chain0\n"
    "[web3]\nchain_id=20200\n";

Keys const c_deadIniKeys{"config.ini [web3_rpc] http_body_size_limit",
    "config.ini [executor] baseline_scheduler", "config.ini [storage] pd_ssl_ca_path",
    "config.ini [storage] pd_ssl_cert_path", "config.ini [storage] pd_ssl_key_path",
    "config.ini [redis]", "config.ini [flow_control] enable_distributed_ratelimit",
    "config.ini [flow_control] enable_distributed_ratelimit_cache",
    "config.ini [flow_control] distributed_ratelimit_cache_percent",
    "config.ini [security] hsm_public_key_file_path",
    "config.ini [experimental] check_parallel_conflict",
    "config.ini [consensus] enable_single_node_consensus", "config.ini [consensus] block_interval",
    "config.ini [consensus] produce_empty_blocks", "config.ini [consensus] fee_recipient",
    "config.ini [consensus] prev_randao", "config.ini [consensus] fixed_timestamp",
    "config.ini [op_engine_rpc]", "config.ini [engine_rpc] enable", "config.ini [ethereum] mode",
    "config.ini [chain]", "config.ini [web3]"};
}  // namespace

// Every config.ini class on every lane: the node starts and lists each key once, in table
// order; a second load of the same files lists them once again, not twice.
BOOST_AUTO_TEST_CASE(everyConfigIniDeadKeyStartsAndIsListedOnce)
{
    for (int version : {1, 2, 3})
    {
        auto cfg = starts(genesisFor(version) + laneSections(version), c_deadIni);
        BOOST_CHECK(cfg->deadKeys() == c_deadIniKeys);
        BOOST_REQUIRE_NO_THROW(cfg->loadConfigFromString(c_deadIni));
        BOOST_CHECK(cfg->deadKeys() == c_deadIniKeys);
    }
}

// Below compatibility_version 3.1 config.ini [chain] is where the chain identity lives: live,
// not listed.
BOOST_AUTO_TEST_CASE(configIniChainBelowCompat31IsLive)
{
    auto cfg = starts(
        genesisFor(1, "3.0.0"), "[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=chain0\n");
    BOOST_CHECK(cfg->deadKeys().empty());
    BOOST_CHECK_EQUAL(cfg->genesisConfig().m_chainID, "chain0");
}

// config.genesis: consensus_timeout on every lane; the removed 3.18 keys on v2/v3.
BOOST_AUTO_TEST_CASE(genesisDeadKeysAreListedOnTheirLanes)
{
    constexpr auto c_evmRevision = "evm_revision=prague\nevm_revision_forks=0:prague\n";
    constexpr auto c_removed =
        "[ethereum]\nmode=opstack-el\n[op_fork_schedule]\ncanonical=0:jovian\n";
    auto timeout = [](std::string genesis) {
        auto const at = genesis.find("[tx]");
        return genesis.insert(at, "consensus_timeout=3000\n");
    };
    for (int version : {2, 3})
    {
        auto cfg = starts(
            timeout(genesisFor(version)) + c_evmRevision + laneSections(version) + c_removed, "");
        BOOST_CHECK(cfg->deadKeys() ==
                    (Keys{"config.genesis [consensus] consensus_timeout",
                        "config.genesis [ethereum] mode", "config.genesis [executor] evm_revision",
                        "config.genesis [executor] evm_revision_forks",
                        "config.genesis [op_fork_schedule]"}));
    }
    // v0/v1 no longer read evm_revision: listed. [ethereum] mode and [op_fork_schedule] stay
    // silent.
    auto v1 = starts(timeout(genesisFor(1)) + c_evmRevision + c_removed, "");
    BOOST_CHECK(v1->deadKeys() == (Keys{"config.genesis [consensus] consensus_timeout",
                                      "config.genesis [executor] evm_revision",
                                      "config.genesis [executor] evm_revision_forks"}));
}

// v2/v3 genesis: the v0/v1 sections a migrated config.genesis still carries are listed.
BOOST_AUTO_TEST_CASE(v01GenesisSectionsAreListedOnEthLanes)
{
    for (int version : {2, 3})
    {
        auto cfg = starts("[version]\ncompatibility_version=3.18.0\n" + genesisFor(version) +
                              laneSections(version) +
                              "[web3]\nchain_id=20200\n[features]\nfeature_evm_cancun=1\n",
            "");
        BOOST_CHECK(cfg->deadKeys() == (Keys{"config.genesis [web3]", "config.genesis [version]",
                                           "config.genesis [features]"}));
    }
}

// [debug] block parameters without [debug] enable=true are listed; with it they are live.
BOOST_AUTO_TEST_CASE(debugSelfProduceKeysNeedEnable)
{
    constexpr auto c_params =
        "block_interval=500\nproduce_empty_blocks=false\n"
        "fee_recipient=0x0000000000000000000000000000000000000001\nprev_randao=\n"
        "fixed_timestamp=1\n";
    BOOST_CHECK(starts(genesisFor(1), std::string("[debug]\n") + c_params)->deadKeys() ==
                (Keys{"config.ini [debug] block_interval",
                    "config.ini [debug] produce_empty_blocks", "config.ini [debug] fee_recipient",
                    "config.ini [debug] prev_randao", "config.ini [debug] fixed_timestamp"}));
    for (int version : {1, 2})
    {
        auto cfg = starts(genesisFor(version) + laneSections(version),
            std::string("[debug]\nenable=true\n") + c_params);
        BOOST_CHECK(cfg->deadKeys().empty());
        BOOST_CHECK_EQUAL(cfg->debugBlockInterval(), 500U);
    }
}

// config.ini keys another lane reads are silently ignored on a lane without a use for them.
BOOST_AUTO_TEST_CASE(laneMismatchKeysAreNotListed)
{
    // v1: the Engine API listener and the v2/v3 devp2p network keys
    BOOST_CHECK(starts(genesisFor(1),
        "[engine_rpc]\nlisten_port=9551\nenable_miner_api=true\n"
        "[ethereum]\nmax_batch_size=64\nop_block_time_seconds=2\n")
            ->deadKeys()
            .empty());
    // v2: the OP-only keys; v3: the L1-only key
    BOOST_CHECK(starts(genesisFor(2) + laneSections(2),
        "[engine_rpc]\nenable_miner_api=true\n[ethereum]\nop_block_time_seconds=2\n"
        "op_sync_lag_blocks=64\n")
            ->deadKeys()
            .empty());
    BOOST_CHECK(starts(genesisFor(3) + laneSections(3),
        "[ethereum]\ndeposit_contract_address=0x7f02c3e3c98b133055b8b348b2ac625669182295\n")
            ->deadKeys()
            .empty());
}

// [web3_rpc] enable_miner_api is not dead: op-batcher reaches miner_setMaxDASize through the
// web3 listener (its --l2-eth-rpc carries no JWT), so the key keeps its effect there.
BOOST_AUTO_TEST_CASE(web3RpcMinerApiStaysLive)
{
    auto cfg = starts(genesisFor(3) + laneSections(3), "[web3_rpc]\nenable_miner_api=true\n");
    BOOST_CHECK(cfg->deadKeys().empty());
    BOOST_CHECK(cfg->enableMinerApi());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
