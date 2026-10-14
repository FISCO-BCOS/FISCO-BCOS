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
 * @file NodeConfigBlockDriverTest.cpp
 * @brief lane x config.ini matrix for NodeConfig::blockDriver() and the Engine API listener.
 */

// Each case loads a config.genesis for one lane and a config.ini, runs the startup hook
// (validateELModeInvariants, which derives blockDriver()), and asserts the outcome: the
// driver and Engine API listener the node runs, or the refusal it starts with.

#include "EthLaneGenesisFixture.h"
#include "ExceptionCheck.h"
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-tool/NodeConfig.h>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <string>

using namespace bcos;
using namespace bcos::tool;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(NodeConfigBlockDriverTest)

namespace
{
std::string genesisFor(int version)
{
    std::string genesis =
        "[version]\ncompatibility_version=3.18.0\n"
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
    if (version == 2)
    {
        genesis += "evm_revision=prague\n" + ethLaneGenesisSections();
    }
    if (version >= 3)
    {
        genesis += "[op_fork_timestamps]\njovian_time=0\nkarst_time=0\n" + ethLaneGenesisSections();
    }
    return genesis;
}

std::unique_ptr<NodeConfig> loadBoth(int version, std::string const& ini)
{
    auto cfg = std::make_unique<NodeConfig>(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    BOOST_REQUIRE_NO_THROW(cfg->loadGenesisConfigFromString(genesisFor(version)));
    BOOST_REQUIRE_NO_THROW(cfg->loadConfigFromString(ini));
    return cfg;
}

// The node starts with this config pair; returns the started config.
std::unique_ptr<NodeConfig> starts(int version, std::string const& ini)
{
    auto cfg = loadBoth(version, ini);
    BOOST_REQUIRE_NO_THROW(cfg->validateELModeInvariants());
    return cfg;
}

// The node refuses to start with this config pair, naming @p reason.
void refuses(int version, std::string const& ini, std::string const& reason)
{
    auto cfg = loadBoth(version, ini);
    BOOST_CHECK_EXCEPTION(cfg->validateELModeInvariants(), InvalidConfig,
        [&](auto const& e) { return errinfoContains(e, reason); });
}

constexpr auto c_followDevp2p = "[debug]\nfollow_devp2p=true\n";
constexpr auto c_followOffLane = "[debug] follow_devp2p requires executor.version >= 3";
}  // namespace

// v3 with no driver key at all: op-node drives it over the Engine API, which listens on the
// [engine_rpc] defaults.
BOOST_AUTO_TEST_CASE(v3WithNothingRunsEngineOnEngineRpcDefaults)
{
    auto cfg = starts(3, "");
    BOOST_CHECK(cfg->blockDriver() == BlockDriver::Engine);
    BOOST_CHECK(cfg->servesEngineApi());
    auto const& listener = cfg->engineApiListener();
    BOOST_CHECK_EQUAL(listener.listenIP, "127.0.0.1");
    BOOST_CHECK_EQUAL(listener.listenPort, 8551);
    BOOST_CHECK_EQUAL(listener.jwtSecretFile, "conf/engine/jwt.hex");
    BOOST_CHECK(!cfg->engineApiMinerApi());
}

// v3 with only [engine_rpc]: the listener takes its values. An enable key is not read on
// this lane, so neither value stops the listener nor trips the v0..v2 "requires
// ethereum.mode=el" gate.
BOOST_AUTO_TEST_CASE(v3WithOnlyEngineRpcServesItsListener)
{
    for (auto const* enable : {"", "enable=false\n", "enable=true\n"})
    {
        auto cfg = starts(3, std::string("[engine_rpc]\n") + enable +
                                 "listen_ip=0.0.0.0\nlisten_port=9551\n"
                                 "jwt_secret_file=jwt.hex\nclock_skew_secs=30\n"
                                 "batch_request_size_limit=16\n");
        BOOST_CHECK(cfg->blockDriver() == BlockDriver::Engine);
        BOOST_CHECK(cfg->servesEngineApi());
        auto const& listener = cfg->engineApiListener();
        BOOST_CHECK_EQUAL(listener.listenIP, "0.0.0.0");
        BOOST_CHECK_EQUAL(listener.listenPort, 9551);
        BOOST_CHECK_EQUAL(listener.jwtSecretFile, "jwt.hex");
        BOOST_CHECK_EQUAL(listener.clockSkewSecs, 30);
        BOOST_CHECK_EQUAL(listener.batchRequestSizeLimit, 16);
    }
}

// [debug] follow_devp2p on v3: the devp2p follow driver, and no Engine API listener.
BOOST_AUTO_TEST_CASE(v3FollowDevp2pRunsTheFollowDriver)
{
    auto cfg = starts(3, c_followDevp2p);
    BOOST_CHECK(cfg->blockDriver() == BlockDriver::DebugFollowDevp2p);
    BOOST_CHECK(!cfg->servesEngineApi());
    BOOST_CHECK_EQUAL(cfg->ethereumChainId(), 11155420u);
}

// follow_devp2p off the OP lane is refused: there is no OP chain to follow.
BOOST_AUTO_TEST_CASE(followDevp2pOnV0V1V2Refused)
{
    for (int version : {0, 1, 2})
    {
        refuses(version, c_followDevp2p, c_followOffLane);
    }
}

// v0..v2 keep their drivers until the v2 Engine paths merge: PBFT without a driver key, and
// [engine_rpc] without enable runs no listener.
BOOST_AUTO_TEST_CASE(v1V2DriversUnchanged)
{
    for (int version : {1, 2})
    {
        auto plain = starts(version, "");
        BOOST_CHECK(plain->blockDriver() == BlockDriver::PBFT);
        BOOST_CHECK(!plain->servesEngineApi());

        auto engineRpcOnly = starts(version, "[engine_rpc]\nlisten_port=9551\n");
        BOOST_CHECK(engineRpcOnly->blockDriver() == BlockDriver::PBFT);
        BOOST_CHECK(!engineRpcOnly->servesEngineApi());

        auto selfProduce = starts(version, "[consensus]\nenable_single_node_consensus=true\n");
        BOOST_CHECK(selfProduce->blockDriver() == BlockDriver::DebugSelfProduce);
        BOOST_CHECK(!selfProduce->servesEngineApi());
    }
    // [engine_rpc] enable on v0..v2 still requires EL mode.
    refuses(2, "[engine_rpc]\nenable=true\n", "engine_rpc.enable requires ethereum.mode=el");
}

// v2 [op_engine_rpc] stays as it was in this step: the Engine driver, listening on that
// section; its enable_miner_api has no effect off the OP lane.
BOOST_AUTO_TEST_CASE(v2OpEngineRpcUnchanged)
{
    auto cfg = starts(2,
        "[op_engine_rpc]\nenable=true\nlisten_port=9552\nenable_miner_api=true\n"
        "[engine_rpc]\nlisten_port=9551\n");
    BOOST_CHECK(cfg->blockDriver() == BlockDriver::Engine);
    BOOST_CHECK(cfg->servesEngineApi());
    BOOST_CHECK_EQUAL(cfg->engineApiListener().listenPort, 9552);
    BOOST_CHECK_EQUAL(cfg->engineApiListener().jwtSecretFile, "conf/op-engine/jwt.hex");
    BOOST_CHECK(!cfg->engineApiMinerApi());
}

// The v3 driver keys of other lanes are refused rather than ignored: each would leave the
// node with a different block producer than the operator configured.
BOOST_AUTO_TEST_CASE(v3RefusesOtherDriverKeys)
{
    refuses(3, "[op_engine_rpc]\nenable=true\n",
        "[op_engine_rpc] is not read on executor.version >= 3");
    refuses(3, "[consensus]\nenable_single_node_consensus=true\n",
        "enable_single_node_consensus is not supported on executor.version >= 3");
    // follow_devp2p does not lift either refusal.
    refuses(3, std::string(c_followDevp2p) + "[op_engine_rpc]\nenable=true\n",
        "[op_engine_rpc] is not read on executor.version >= 3");
    refuses(3, std::string(c_followDevp2p) + "[consensus]\nenable_single_node_consensus=true\n",
        "enable_single_node_consensus is not supported on executor.version >= 3");
}

// [ethereum] mode=opstack-el is gone from both files; each refusal names the replacement.
BOOST_AUTO_TEST_CASE(opstackElModeRefusedWithItsReplacement)
{
    NodeConfig ini(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    BOOST_CHECK_EXCEPTION(ini.loadConfigFromString("[ethereum]\nmode=opstack-el\n"), InvalidConfig,
        [](auto const& e) {
            return errinfoContains(
                e, "ethereum.mode=opstack-el is replaced by [debug] follow_devp2p=true");
        });
    NodeConfig genesis(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    BOOST_CHECK_EXCEPTION(
        genesis.loadGenesisConfigFromString(genesisFor(3) + "[ethereum]\nmode=opstack-el\n"),
        InvalidConfig, [](auto const& e) {
            return errinfoContains(e, "set config.ini [debug] follow_devp2p=true");
        });
}

// mpt_prune_window > 0 on v3: the Engine API commit path has no pruning observer, the
// follow path does.
BOOST_AUTO_TEST_CASE(v3MptPruneWindowNeedsFollowDevp2p)
{
    refuses(3, "[storage]\nmpt_prune_window=1000\n",
        "storage.mpt_prune_window > 0 on executor.version >= 3 (the OP lane) requires [debug] "
        "follow_devp2p=true");
    auto follow = starts(3, std::string(c_followDevp2p) + "[storage]\nmpt_prune_window=1000\n");
    BOOST_CHECK(follow->blockDriver() == BlockDriver::DebugFollowDevp2p);
    // Pruning off (the default) is fine under the Engine driver.
    BOOST_CHECK(
        starts(3, "[storage]\nmpt_prune_window=-1\n")->blockDriver() == BlockDriver::Engine);
}

// enable_miner_api lives in [engine_rpc] and is read on the OP lane only; the [web3_rpc] key
// stays scoped to the web3 listener.
BOOST_AUTO_TEST_CASE(engineRpcMinerApiIsOpLaneOnly)
{
    BOOST_CHECK(starts(3, "[engine_rpc]\nenable_miner_api=true\n")->engineApiMinerApi());
    BOOST_CHECK(!starts(3, "[web3_rpc]\nenable_miner_api=true\n")->engineApiMinerApi());
    BOOST_CHECK(starts(3, "[web3_rpc]\nenable_miner_api=true\n")->enableMinerApi());
    BOOST_CHECK(!starts(3, "[engine_rpc]\nenable_miner_api=true\n")->enableMinerApi());
    BOOST_CHECK(!starts(1, "[engine_rpc]\nenable_miner_api=true\n")->engineApiMinerApi());
}

// The debug namespace on the Engine API listener follows the section that listener reads:
// [engine_rpc] on the OP lane, an enabled [op_engine_rpc] below v3. The web3 key stays on the
// web3 listener.
BOOST_AUTO_TEST_CASE(engineApiDebugApiFollowsTheListenerSection)
{
    BOOST_CHECK(starts(3, "[engine_rpc]\nenable_debug_api=true\n")->engineApiDebugApi());
    BOOST_CHECK(!starts(3, "[engine_rpc]\nenable_debug_api=true\n")->enableDebugApi());
    BOOST_CHECK(!starts(3, "[web3_rpc]\nenable_debug_api=true\n")->engineApiDebugApi());
    BOOST_CHECK(starts(3, "[web3_rpc]\nenable_debug_api=true\n")->enableDebugApi());
    BOOST_CHECK(
        starts(2, "[op_engine_rpc]\nenable=true\nenable_debug_api=true\n")->engineApiDebugApi());
    BOOST_CHECK(!starts(2, "[op_engine_rpc]\nenable_debug_api=true\n")->engineApiDebugApi());
    refuses(3,
        "[engine_rpc]\nenable_debug_api=true\n[debug]\nfollow_devp2p=true\n"
        "[storage]\nmpt_prune_window=1000\n",
        "enable_debug_api requires storage.mpt_prune_window = -1");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
