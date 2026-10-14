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
#include <vector>

using namespace bcos;
using namespace bcos::tool;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(NodeConfigBlockDriverTest)

namespace
{
constexpr auto c_forkTimestamps =
    "[fork_timestamps]\nlondon_time=0\nparis_time=0\nshanghai_time=0\ncancun_time=0\n"
    "prague_time=0\nmerge_block=0\n";

// v2 genesis variants: the L1 fork schedule (the shape every v2 driver accepts), or only an
// executor.evm_revision, which nothing reads (accepted by the genesis loader, refused by both v2
// drivers for the missing schedule).
enum class V2Fork
{
    Timestamps,
    EvmRevisionOnly,
};

std::string genesisFor(int version, V2Fork v2Fork = V2Fork::Timestamps)
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
        genesis += (v2Fork == V2Fork::Timestamps ? std::string(c_forkTimestamps) :
                                                   std::string("evm_revision=prague\n")) +
                   ethLaneGenesisSections();
    }
    if (version >= 3)
    {
        genesis += "[op_fork_timestamps]\njovian_time=0\nkarst_time=0\n" + ethLaneGenesisSections();
    }
    return genesis;
}

std::unique_ptr<NodeConfig> loadBoth(
    int version, std::string const& ini, V2Fork v2Fork = V2Fork::Timestamps)
{
    auto cfg = std::make_unique<NodeConfig>(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    BOOST_REQUIRE_NO_THROW(cfg->loadGenesisConfigFromString(genesisFor(version, v2Fork)));
    BOOST_REQUIRE_NO_THROW(cfg->loadConfigFromString(ini));
    return cfg;
}

// The node starts with this config pair; returns the started config.
std::unique_ptr<NodeConfig> starts(
    int version, std::string const& ini, V2Fork v2Fork = V2Fork::Timestamps)
{
    auto cfg = loadBoth(version, ini, v2Fork);
    BOOST_REQUIRE_NO_THROW(cfg->validateELModeInvariants());
    return cfg;
}

// The node refuses to start with this config pair, naming @p reason.
void refuses(int version, std::string const& ini, std::string const& reason,
    V2Fork v2Fork = V2Fork::Timestamps)
{
    auto cfg = loadBoth(version, ini, v2Fork);
    BOOST_CHECK_EXCEPTION(cfg->validateELModeInvariants(), InvalidConfig,
        [&](auto const& e) { return errinfoContains(e, reason); });
}

constexpr auto c_followDevp2p = "[debug]\nfollow_devp2p=true\n";
constexpr auto c_followOffLane = "[debug] follow_devp2p requires executor.version >= 3";
constexpr auto c_selfProduce = "[debug]\nenable=true\n";
constexpr auto c_selfProduceOnV3 =
    "[debug] enable: executor.version >= 3 (the OP lane) does not support self-production";
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

// v2 and v3 with only [engine_rpc]: the listener takes its values. [engine_rpc] has no
// enable key, so neither value stops the listener.
BOOST_AUTO_TEST_CASE(v2V3WithOnlyEngineRpcServeItsListener)
{
    for (int version : {2, 3})
    {
        for (auto const* enable : {"", "enable=false\n", "enable=true\n"})
        {
            auto cfg = starts(version, std::string("[engine_rpc]\n") + enable +
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
}

// v2 with no mode declaration in either file is the L1 EL: the Engine driver, the Engine
// API on the [engine_rpc] defaults, and EL mode (devp2p sync next to it).
BOOST_AUTO_TEST_CASE(v2WithNothingIsTheL1El)
{
    auto cfg = starts(2, "");
    BOOST_CHECK(cfg->blockDriver() == BlockDriver::Engine);
    BOOST_CHECK(cfg->servesEngineApi());
    BOOST_CHECK(cfg->ethereumELModeEnabled());
    BOOST_CHECK_EQUAL(cfg->engineApiListener().listenPort, 8551);
    BOOST_CHECK_EQUAL(cfg->engineApiListener().jwtSecretFile, "conf/engine/jwt.hex");
    BOOST_CHECK_EQUAL(cfg->ethereumChainId(), 11155420u);
}

// Both v2 drivers derive the EVM revision from [fork_timestamps] alone; a v2 genesis that carries
// executor.evm_revision instead has no schedule, so the node refuses to start under either.
BOOST_AUTO_TEST_CASE(v2RequiresForkTimestampsUnderEitherDriver)
{
    for (auto const* ini : {"", c_selfProduce})
    {
        refuses(2, ini,
            "executor.version=2 (the L1 EL lane) requires a [fork_timestamps] section in "
            "config.genesis",
            V2Fork::EvmRevisionOnly);
    }
}

// [debug] enable on v2 replaces the Engine driver: no Engine API, no EL mode.
BOOST_AUTO_TEST_CASE(v2SelfProduceServesNoEngineApi)
{
    auto cfg = starts(2, c_selfProduce);
    BOOST_CHECK(cfg->blockDriver() == BlockDriver::DebugSelfProduce);
    BOOST_CHECK(!cfg->servesEngineApi());
    BOOST_CHECK(!cfg->ethereumELModeEnabled());
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

// v0/v1 offer no Engine API: PBFT without a driver key, and [engine_rpc] (any enable value)
// is not read.
BOOST_AUTO_TEST_CASE(v0V1ServeNoEngineApi)
{
    for (int version : {0, 1})
    {
        auto plain = starts(version, "");
        BOOST_CHECK(plain->blockDriver() == BlockDriver::PBFT);
        BOOST_CHECK(!plain->servesEngineApi());
        BOOST_CHECK(!plain->ethereumELModeEnabled());

        auto engineRpc = starts(version, "[engine_rpc]\nenable=true\nlisten_port=9551\n");
        BOOST_CHECK(engineRpc->blockDriver() == BlockDriver::PBFT);
        BOOST_CHECK(!engineRpc->servesEngineApi());
    }
}

// [debug] enable x lane: the DebugSelfProduce driver on v1 and v2 (no Engine API on either),
// refused on v0 and on v3, each refusal naming why.
BOOST_AUTO_TEST_CASE(debugEnableLaneMatrix)
{
    refuses(0, c_selfProduce,
        "[debug] enable requires executor.version 1 or 2: executor.version=0 does not support "
        "self-production");
    for (int version : {1, 2})
    {
        auto cfg = starts(version, c_selfProduce);
        BOOST_CHECK(cfg->blockDriver() == BlockDriver::DebugSelfProduce);
        BOOST_CHECK(!cfg->servesEngineApi());
        BOOST_CHECK(!cfg->ethereumELModeEnabled());
    }
    refuses(3, c_selfProduce, c_selfProduceOnV3);
}

// [debug] enable and follow_devp2p together are refused on every lane: each is a block driver.
BOOST_AUTO_TEST_CASE(debugEnableWithFollowDevp2pRefused)
{
    for (int version : {0, 1, 2, 3})
    {
        refuses(version, "[debug]\nenable=true\nfollow_devp2p=true\n",
            "[debug] enable and [debug] follow_devp2p are mutually exclusive");
    }
}

// The DebugSelfProduce parameters are read from [debug], with their defaults when absent.
BOOST_AUTO_TEST_CASE(debugSelfProduceParameters)
{
    auto defaults = starts(1, c_selfProduce);
    BOOST_CHECK_EQUAL(defaults->debugBlockInterval(), 1000u);
    BOOST_CHECK(defaults->debugProduceEmptyBlocks());
    BOOST_CHECK_EQUAL(defaults->debugFeeRecipient(), "0x0000000000000000000000000000000000000000");
    BOOST_CHECK(defaults->debugPrevRandao().empty());
    BOOST_CHECK_EQUAL(defaults->debugFixedTimestamp(), 0u);

    auto const randao = "0x" + std::string(64, 'a');
    auto set = starts(2, std::string(c_selfProduce) +
                             "block_interval=250\nproduce_empty_blocks=false\n"
                             "fee_recipient=0x2adc25665018aa1fe0e6bc666dac8fc2697ff9ba\n"
                             "prev_randao=" +
                             randao + "\nfixed_timestamp=1700000000\n");
    BOOST_CHECK(set->blockDriver() == BlockDriver::DebugSelfProduce);
    BOOST_CHECK_EQUAL(set->debugBlockInterval(), 250u);
    BOOST_CHECK(!set->debugProduceEmptyBlocks());
    BOOST_CHECK_EQUAL(set->debugFeeRecipient(), "0x2adc25665018aa1fe0e6bc666dac8fc2697ff9ba");
    BOOST_CHECK_EQUAL(set->debugPrevRandao(), randao);
    BOOST_CHECK_EQUAL(set->debugFixedTimestamp(), 1700000000u);
}

// The self-production keys under [consensus] are no longer read: enable_single_node_consensus
// selects no driver, so v1 stays PBFT and v2 stays Engine, and the block parameters there do
// not reach the [debug] getters.
BOOST_AUTO_TEST_CASE(consensusSelfProduceKeysAreNotRead)
{
    constexpr auto c_old =
        "[consensus]\nenable_single_node_consensus=true\nblock_interval=250\n"
        "produce_empty_blocks=false\nfixed_timestamp=1700000000\n";
    auto v1 = starts(1, c_old);
    BOOST_CHECK(v1->blockDriver() == BlockDriver::PBFT);
    BOOST_CHECK_EQUAL(v1->debugBlockInterval(), 1000u);
    BOOST_CHECK(v1->debugProduceEmptyBlocks());
    BOOST_CHECK_EQUAL(v1->debugFixedTimestamp(), 0u);
    BOOST_CHECK(starts(2, c_old)->blockDriver() == BlockDriver::Engine);
    BOOST_CHECK(starts(3, c_old)->blockDriver() == BlockDriver::Engine);
}

// [ethereum] mode is not read in either file: config.ini mode=el does not make a v1 node an
// EL, and a v2 genesis that still carries mode=el loads as any v2 genesis does.
BOOST_AUTO_TEST_CASE(ethereumModeElIsNotRead)
{
    auto v1 = starts(1, "[ethereum]\nmode=el\n");
    BOOST_CHECK(v1->blockDriver() == BlockDriver::PBFT);
    BOOST_CHECK(!v1->ethereumELModeEnabled());

    NodeConfig v2(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    BOOST_REQUIRE_NO_THROW(v2.loadGenesisConfigFromString(genesisFor(2) + "[ethereum]\nmode=el\n"));
    BOOST_REQUIRE_NO_THROW(v2.loadConfigFromString("[ethereum]\nmode=none\n"));
    BOOST_REQUIRE_NO_THROW(v2.validateELModeInvariants());
    BOOST_CHECK(v2.ethereumELModeEnabled());
}

// [op_engine_rpc] is removed: on every lane the node starts with the section recorded as a dead
// key, and nothing in it reaches the Engine API listener.
BOOST_AUTO_TEST_CASE(opEngineRpcIsADeadKey)
{
    constexpr auto c_opEngineRpc = "[op_engine_rpc]\nenable=true\nlisten_port=9552\n";
    BOOST_CHECK(starts(1, c_opEngineRpc)->blockDriver() == BlockDriver::PBFT);
    for (int version : {1, 2, 3})
    {
        auto cfg = starts(version, c_opEngineRpc);
        BOOST_CHECK(cfg->deadKeys() == std::vector<std::string>{"config.ini [op_engine_rpc]"});
    }
    auto v3 = starts(3, c_opEngineRpc);
    BOOST_CHECK(v3->blockDriver() == BlockDriver::Engine);
    BOOST_CHECK_EQUAL(v3->engineApiListener().listenPort, 8551);
}

// executor.eest_replay_mode drops balance and nonce-window admission checks; the fixtures are
// Ethereum state tests, so it is refused on every lane but v2 (under either v2 driver).
BOOST_AUTO_TEST_CASE(eestReplayModeRequiresV2)
{
    constexpr auto c_eest = "[executor]\neest_replay_mode=true\n";
    for (int version : {0, 1, 3})
    {
        refuses(version, c_eest, "executor.eest_replay_mode requires executor.version=2");
    }
    BOOST_CHECK(starts(2, c_eest)->eestReplayMode());
    BOOST_CHECK(starts(2, std::string(c_eest) + c_selfProduce)->eestReplayMode());
}


// [ethereum] mode=opstack-el is gone from both files: the node starts on the Engine driver
// (follow_devp2p is [debug]'s now) and records the key as dead in the file that carries it.
BOOST_AUTO_TEST_CASE(opstackElModeIsADeadKey)
{
    auto ini = starts(3, "[ethereum]\nmode=opstack-el\n");
    BOOST_CHECK(ini->blockDriver() == BlockDriver::Engine);
    BOOST_CHECK(ini->deadKeys() == std::vector<std::string>{"config.ini [ethereum] mode"});

    NodeConfig genesis(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    BOOST_REQUIRE_NO_THROW(
        genesis.loadGenesisConfigFromString(genesisFor(3) + "[ethereum]\nmode=opstack-el\n"));
    BOOST_REQUIRE_NO_THROW(genesis.loadConfigFromString(""));
    BOOST_REQUIRE_NO_THROW(genesis.validateELModeInvariants());
    BOOST_CHECK(genesis.blockDriver() == BlockDriver::Engine);
    BOOST_CHECK(genesis.deadKeys() == std::vector<std::string>{"config.genesis [ethereum] mode"});
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
    BOOST_CHECK(!starts(2, "[engine_rpc]\nenable_miner_api=true\n")->engineApiMinerApi());
}

// The debug namespace on the Engine API listener is [engine_rpc] enable_debug_api on every lane
// that serves the Engine API (v2 and v3); the web3 key stays on the web3 listener.
BOOST_AUTO_TEST_CASE(engineApiDebugApiFollowsTheListenerSection)
{
    BOOST_CHECK(starts(3, "[engine_rpc]\nenable_debug_api=true\n")->engineApiDebugApi());
    BOOST_CHECK(!starts(3, "[engine_rpc]\nenable_debug_api=true\n")->enableDebugApi());
    BOOST_CHECK(!starts(3, "[web3_rpc]\nenable_debug_api=true\n")->engineApiDebugApi());
    BOOST_CHECK(starts(3, "[web3_rpc]\nenable_debug_api=true\n")->enableDebugApi());
    BOOST_CHECK(starts(2, "[engine_rpc]\nenable_debug_api=true\n")->engineApiDebugApi());
    BOOST_CHECK(!starts(2, "[web3_rpc]\nenable_debug_api=true\n")->engineApiDebugApi());
    refuses(3,
        "[engine_rpc]\nenable_debug_api=true\n[debug]\nfollow_devp2p=true\n"
        "[storage]\nmpt_prune_window=1000\n",
        "enable_debug_api requires storage.mpt_prune_window = -1");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
