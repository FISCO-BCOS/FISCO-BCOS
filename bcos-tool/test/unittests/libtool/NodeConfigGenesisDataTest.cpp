/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "EthLaneGenesisFixture.h"
#include "ExceptionCheck.h"
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-tool/NodeConfig.h>
#include <boost/test/unit_test.hpp>
#include <limits>
#include <string>

using namespace bcos;
using namespace bcos::tool;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(NodeConfigGenesisDataTest)

// generateGenesisData has two shapes selected by compatibilityVersion: a
// sectioned INI-like form for >= V3.1, and a dash-joined legacy form below it.

// >= V3.1 (here V3.6, rpbft, with a feature and a consensus node) takes the
// sectioned branch, including the epoch (>= V3.5) and features sub-blocks and
// the consensus-node loop.
BOOST_AUTO_TEST_CASE(modernSectionedFormat)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    const std::string node =
        "1234567890123456789012345678901234567890123456789012345678901234"
        "1234567890123456789012345678901234567890123456789012345678901234";
    std::string genesis =
        "[version]\ncompatibility_version=3.6.0\n"
        "[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=1\n"
        "[web3]\nchain_id=1\n"
        "[features]\nfeature_balance=1\n"
        "[consensus]\nconsensus_type=rpbft\nblock_tx_count_limit=1000\nleader_period=1\n"
        "epoch_sealer_num=4\nepoch_block_num=1000\nnode.0=" +
        node +
        ":1:1\n"
        "[tx]\ngas_limit=3000000000\n"
        "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
        "auth_admin_account=0x0000000000000000000000000000000000000001\n";

    NodeConfig cfg(keyFactory);
    BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(genesis));
    BOOST_REQUIRE(cfg.ledgerConfig());

    auto data = bcos::tool::generateGenesisData(cfg.genesisConfig(), *cfg.ledgerConfig());
    BOOST_CHECK(!data.empty());
    BOOST_CHECK(data.find("[chain]") != std::string::npos);
    BOOST_CHECK(data.find("consensus_type: rpbft") != std::string::npos);
    BOOST_CHECK(data.find("epochSealerNum") != std::string::npos);  // >= V3.5 block
    BOOST_CHECK(data.find("[features]") != std::string::npos);      // feature present
    BOOST_CHECK(data.find("node.0") != std::string::npos);          // node loop ran
}

// executor.evm_revision / evm_revision_forks are no longer read on any lane: a v2 chain's EVM
// revision comes from [fork_timestamps] alone, and no released binary (3.16.0-3.17.1) read
// them on v0/v1. Present -- even with a value the old parser refused -- they change neither
// the load result nor the genesis pin.
BOOST_AUTO_TEST_CASE(evmRevisionKeysAreIgnored)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    const std::string node =
        "1234567890123456789012345678901234567890123456789012345678901234"
        "1234567890123456789012345678901234567890123456789012345678901234";
    auto genesis = [&](std::string const& version, std::string const& executorExtra,
                       std::string const& sections) {
        return "[version]\ncompatibility_version=3.18.0\n"
               "[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=1\n"
               "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\nleader_period=1\n"
               "node.0=" +
               node +
               ":1:1\n"
               "[tx]\ngas_limit=3000000000\n"
               "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
               "auth_admin_account=0x0000000000000000000000000000000000000001\n"
               "version=" +
               version + "\n" + executorExtra + sections;
    };
    const std::string schedule =
        "[fork_timestamps]\nlondon_time=0\nparis_time=0\nshanghai_time=0\ncancun_time=0\n"
        "prague_time=1746612311\nmerge_block=0\n";
    auto pinOf = [&](std::string const& text) {
        NodeConfig cfg(keyFactory);
        cfg.loadGenesisConfigFromString(text);
        return bcos::tool::generateGenesisData(cfg.genesisConfig(), *cfg.ledgerConfig());
    };

    for (auto const* keys : {"evm_revision=cancun\nevm_revision_forks=0:cancun,100000:osaka\n",
             "evm_revision=experimental\n", "evm_revision=notafork\nevm_revision_forks=-5:x\n"})
    {
        // v2: the [fork_timestamps] schedule decides; the keys leave no trace in the pin.
        auto const v2Sections = schedule + ethLaneGenesisSections();
        std::string v2Pin;
        BOOST_REQUIRE_NO_THROW(v2Pin = pinOf(genesis("2", keys, v2Sections)));
        BOOST_CHECK_EQUAL(v2Pin, pinOf(genesis("2", "", v2Sections)));
        BOOST_CHECK(v2Pin.find("evmRevision") == std::string::npos);

        // v1: same, so a v1 genesis carrying the keys pins like one without them.
        std::string v1Pin;
        BOOST_REQUIRE_NO_THROW(v1Pin = pinOf(genesis("1", keys, "")));
        BOOST_CHECK_EQUAL(v1Pin, pinOf(genesis("1", "", "")));
        BOOST_CHECK(v1Pin.find("evmRevision") == std::string::npos);
    }

    // evm_revision is no substitute for the schedule: a v2 genesis without [fork_timestamps]
    // still loads (the genesis loader has no v2 revision guard any more), and the node
    // refuses it in validateELModeInvariants.
    NodeConfig cfg(keyFactory);
    BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(
        genesis("2", "evm_revision=cancun\n", ethLaneGenesisSections())));
    BOOST_CHECK_EXCEPTION(cfg.validateELModeInvariants(), InvalidConfig, [](auto const& e) {
        return errinfoContains(e, "requires a [fork_timestamps] section in config.genesis");
    });
}

// A compatibilityVersion below V3.1 takes the legacy dash-joined branch.
BOOST_AUTO_TEST_CASE(legacyDashJoinedFormat)
{
    ledger::GenesisConfig genesisConfig;
    genesisConfig.m_compatibilityVersion =
        static_cast<uint32_t>(protocol::BlockVersion::V3_0_VERSION);
    genesisConfig.m_isWasm = false;
    genesisConfig.m_isAuthCheck = false;
    genesisConfig.m_isSerialExecute = true;
    genesisConfig.m_authAdminAccount = "0x0000000000000000000000000000000000000009";
    genesisConfig.m_txGasLimit = 3000000000;

    ledger::LedgerConfig ledgerConfig;
    ledgerConfig.setBlockTxCountLimit(1000);
    ledgerConfig.setLeaderSwitchPeriod(1);

    auto data = bcos::tool::generateGenesisData(genesisConfig, ledgerConfig);
    BOOST_CHECK(!data.empty());
    BOOST_CHECK(data.find("[chain]") == std::string::npos);  // not the sectioned form
    BOOST_CHECK(data.find(genesisConfig.m_authAdminAccount) != std::string::npos);
    BOOST_CHECK(data.find("1000-1-3000000000") != std::string::npos);  // txCount-period-gas
}

// The EL-mode fork schedule's REQUIRED ladder ([fork_timestamps] london..prague) drives
// the EVM revision in EL mode, so it is part of the genesis pin: loadForkTimestamps stores
// it on the GenesisConfig and generateGenesisData emits a [forkTimestamps] section — two
// nodes holding different required schedules produce different genesis data and fail the
// genesis comparison instead of silently running different EVM rules. The post-Prague tail
// (osaka/bpo1/bpo2) is deliberately NOT pinned: those forks activate after genesis, so
// configuring one later must not trip the byte-compared pin (EIP-2124 fork-id handshake
// catches divergence). Chains without the section are byte-unaffected.
BOOST_AUTO_TEST_CASE(forkTimestampsGenesisPin)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    const std::string node =
        "1234567890123456789012345678901234567890123456789012345678901234"
        "1234567890123456789012345678901234567890123456789012345678901234";
    const std::string base =
        "[version]\ncompatibility_version=3.18.0\n"
        "[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=1\n"
        "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\nleader_period=1\n"
        "node.0=" +
        node +
        ":1:1\n"
        "[tx]\ngas_limit=3000000000\n"
        "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
        "auth_admin_account=0x0000000000000000000000000000000000000001\n"
        "version=2\n";
    const std::string schedule =
        "[fork_timestamps]\nlondon_time=0\nparis_time=0\nshanghai_time=1681338455\n"
        "cancun_time=1710338135\nprague_time=1746612311\n"
        "merge_block=0\n";

    // Parses into the GenesisConfig and lands in the genesis pin. (Every config here is
    // executor.version=2, so each carries the lane-mandatory [alloc.0] +
    // [eth_genesis_header] sections; allocs stay out of the pin, the header is pinned.)
    NodeConfig cfg(keyFactory);
    BOOST_REQUIRE_NO_THROW(
        cfg.loadGenesisConfigFromString(base + schedule + ethLaneGenesisSections()));
    auto const& gc = cfg.genesisConfig();
    BOOST_REQUIRE(gc.m_ethereumForkSchedule.has_value());
    BOOST_CHECK_EQUAL(gc.m_ethereumForkSchedule->m_londonTime, 0u);
    BOOST_CHECK_EQUAL(gc.m_ethereumForkSchedule->m_parisTime, 0u);  // explicit: PoS from genesis
    BOOST_CHECK_EQUAL(gc.m_ethereumForkSchedule->m_shanghaiTime, 1681338455u);
    BOOST_CHECK_EQUAL(gc.m_ethereumForkSchedule->m_osakaTime,
        std::numeric_limits<uint64_t>::max());  // omitted: not yet active

    auto data = bcos::tool::generateGenesisData(gc, *cfg.ledgerConfig());
    BOOST_CHECK(data.find("[ethereum]") != std::string::npos);
    BOOST_CHECK(data.find("mode:el") != std::string::npos);
    BOOST_CHECK(data.find("[forkTimestamps]") != std::string::npos);
    BOOST_CHECK(data.find("london_time:0") != std::string::npos);
    BOOST_CHECK(data.find("paris_time:0") != std::string::npos);
    BOOST_CHECK(data.find("shanghai_time:1681338455") != std::string::npos);
    BOOST_CHECK(data.find("cancun_time:1710338135") != std::string::npos);
    BOOST_CHECK(data.find("prague_time:1746612311") != std::string::npos);
    // The post-Prague tail must NOT be pinned: a later osaka/bpo activation changes the
    // config but must not trip the byte-compared restart comparison.
    BOOST_CHECK(data.find("osaka_time:") == std::string::npos);
    BOOST_CHECK(data.find("bpo1_time:") == std::string::npos);
    BOOST_CHECK(data.find("bpo2_time:") == std::string::npos);
    // The EL chain id is part of the pin too.
    BOOST_CHECK(data.find("[web3]") != std::string::npos);
    BOOST_CHECK(data.find("chain_id:1") != std::string::npos);
    // Section layout: the EL-gated [ethereum]/[web3]/[forkTimestamps] blocks are
    // emitted AFTER the [executor]-owned keys (epochSealerNum/epochBlockNum carry no
    // section header), so those keys keep following [executor] on EL chains too
    // instead of being split off under [forkTimestamps].
    BOOST_CHECK(data.find("epochSealerNum:") != std::string::npos);
    BOOST_CHECK(data.find("epochSealerNum:") < data.find("[forkTimestamps]"));
    BOOST_CHECK(data.find("[forkTimestamps]") < data.find("node.0"));

    // A node with a different schedule produces different genesis data.
    NodeConfig cfg2(keyFactory);
    BOOST_REQUIRE_NO_THROW(cfg2.loadGenesisConfigFromString(
        base +
        "[fork_timestamps]\nlondon_time=0\nparis_time=0\nshanghai_time=1681338455\n"
        "cancun_time=1710338135\nprague_time=1746612312\n"
        "merge_block=0\n" +
        ethLaneGenesisSections()));
    BOOST_CHECK(
        data != bcos::tool::generateGenesisData(cfg2.genesisConfig(), *cfg2.ledgerConfig()));

    // A node that differs ONLY in the post-Prague tail (osaka scheduled vs omitted)
    // produces the SAME genesis data — the tail is not pinned, so activating Osaka
    // later does not break the restart comparison (EIP-2124 handshake covers it).
    NodeConfig cfgTail(keyFactory);
    BOOST_REQUIRE_NO_THROW(cfgTail.loadGenesisConfigFromString(
        base +
        "[fork_timestamps]\nlondon_time=0\nparis_time=0\nshanghai_time=1681338455\n"
        "cancun_time=1710338135\nprague_time=1746612311\n"
        "merge_block=0\n"
        "osaka_time=1767225548\n" +
        ethLaneGenesisSections()));
    BOOST_CHECK(
        data == bcos::tool::generateGenesisData(cfgTail.genesisConfig(), *cfgTail.ledgerConfig()));

    // A node with a different [chain] chain_id (same schedule) also produces
    // different genesis data — the id is pinned, not just validated.
    std::string baseOtherChain = base;
    baseOtherChain.replace(baseOtherChain.find("group_id=group0\nchain_id=1\n"),
        std::string("group_id=group0\nchain_id=1\n").size(),
        "group_id=group0\nchain_id=11155111\n");
    NodeConfig cfg4(keyFactory);
    BOOST_REQUIRE_NO_THROW(
        cfg4.loadGenesisConfigFromString(baseOtherChain + schedule + ethLaneGenesisSections()));
    auto const data4 = bcos::tool::generateGenesisData(cfg4.genesisConfig(), *cfg4.ledgerConfig());
    BOOST_CHECK(data != data4);
    // The EL chain-id line follows [chain] chain_id, not just the chainID line.
    BOOST_CHECK(data4.find("[web3]\nchain_id:11155111\n") != std::string::npos);

    // No [fork_timestamps] section -> no [forkTimestamps] emission (the genesis loads; the
    // node refuses it in validateELModeInvariants). The lane-gated chain-id block is still
    // pinned.
    NodeConfig cfg3(keyFactory);
    BOOST_REQUIRE_NO_THROW(cfg3.loadGenesisConfigFromString(base + ethLaneGenesisSections()));
    BOOST_CHECK(!cfg3.genesisConfig().m_ethereumForkSchedule.has_value());
    auto data3 = bcos::tool::generateGenesisData(cfg3.genesisConfig(), *cfg3.ledgerConfig());
    BOOST_CHECK(data3.find("[forkTimestamps]") == std::string::npos);
    BOOST_CHECK(data3.find("[ethereum]\nmode:el\n[web3]\nchain_id:1\n") != std::string::npos);
}

// A fork the chain never schedules (UINT64_MAX, what eth-sync-check --genesis2ini emits for
// an absent *Time field) must NOT be frozen into the byte-compared genesis pin: an operator
// who later schedules Cancun on a Shanghai-era chain regenerates config.genesis, and a pinned
// sentinel would then abort the restart on a genesis mismatch. Sentinel entries are omitted
// from the pin — the same rule the post-Prague tail follows. Divergence is still caught: a
// node holding the sentinel emits no key while a node holding a real activation time emits
// one, so the pins differ.
BOOST_AUTO_TEST_CASE(forkTimestampsPinSkipsNotScheduledSentinel)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    const std::string node =
        "1234567890123456789012345678901234567890123456789012345678901234"
        "1234567890123456789012345678901234567890123456789012345678901234";
    const std::string base =
        "[version]\ncompatibility_version=3.18.0\n"
        "[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=1\n"
        "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\nleader_period=1\n"
        "node.0=" +
        node +
        ":1:1\n"
        "[tx]\ngas_limit=3000000000\n"
        "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
        "auth_admin_account=0x0000000000000000000000000000000000000001\n"
        "version=2\n";

    // Shanghai-era chain: cancun/prague unscheduled -> omitted from the pin.
    NodeConfig shanghaiEra(keyFactory);
    BOOST_REQUIRE_NO_THROW(shanghaiEra.loadGenesisConfigFromString(
        base + "[fork_timestamps]\nlondon_time=0\nparis_time=0\nshanghai_time=1681338455\n"
               "cancun_time=18446744073709551615\nprague_time=18446744073709551615\n"
               "merge_block=0\n" +
        ethLaneGenesisSections()));
    auto sentinelData = bcos::tool::generateGenesisData(
        shanghaiEra.genesisConfig(), *shanghaiEra.ledgerConfig());
    BOOST_CHECK(sentinelData.find("[forkTimestamps]") != std::string::npos);
    BOOST_CHECK(sentinelData.find("london_time:0") != std::string::npos);
    BOOST_CHECK(sentinelData.find("paris_time:0") != std::string::npos);
    BOOST_CHECK(sentinelData.find("shanghai_time:1681338455") != std::string::npos);
    BOOST_CHECK(sentinelData.find("cancun_time:") == std::string::npos);
    BOOST_CHECK(sentinelData.find("prague_time:") == std::string::npos);

    // Fully-scheduled chain: all five keys pinned, in ladder order, byte-identical to the
    // pre-change emission.
    NodeConfig full(keyFactory);
    BOOST_REQUIRE_NO_THROW(full.loadGenesisConfigFromString(
        base + "[fork_timestamps]\nlondon_time=0\nparis_time=0\nshanghai_time=1681338455\n"
               "cancun_time=1710338135\nprague_time=1746612311\n"
               "merge_block=0\n" +
        ethLaneGenesisSections()));
    auto fullData = bcos::tool::generateGenesisData(full.genesisConfig(), *full.ledgerConfig());
    BOOST_CHECK(fullData.find("[forkTimestamps]\nlondon_time:0\nparis_time:0\n"
                              "shanghai_time:1681338455\ncancun_time:1710338135\n"
                              "prague_time:1746612311\n") != std::string::npos);
}

// [fork_timestamps] is the L1 fork schedule and belongs to executor.version=2 only, where the
// node requires it (validateELModeInvariants). [ethereum] mode in the genesis is not read.
BOOST_AUTO_TEST_CASE(forkTimestampsBelongToTheL1ElLane)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    const std::string node =
        "1234567890123456789012345678901234567890123456789012345678901234"
        "1234567890123456789012345678901234567890123456789012345678901234";
    auto genesis = [&](std::string const& version, std::string const& extra) {
        return "[version]\ncompatibility_version=3.18.0\n"
               "[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=1\n"
               "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\nleader_period=1\n"
               "node.0=" +
               node +
               ":1:1\n"
               "[tx]\ngas_limit=3000000000\n"
               "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
               "auth_admin_account=0x0000000000000000000000000000000000000001\n"
               "version=" +
               version + "\n" + extra;
    };
    const std::string schedule =
        "[fork_timestamps]\nlondon_time=0\nparis_time=0\nshanghai_time=1681338455\n"
        "cancun_time=1710338135\nprague_time=1746612311\n"
        "merge_block=0\n";

    // v2 with the schedule alone (no evm_revision, no mode declaration): accepted.
    {
        NodeConfig cfg(keyFactory);
        BOOST_REQUIRE_NO_THROW(
            cfg.loadGenesisConfigFromString(genesis("2", schedule + ethLaneGenesisSections())));
        BOOST_CHECK(cfg.genesisConfig().m_ethereumForkSchedule.has_value());
    }
    // v2 without the schedule: the genesis loads, the node refuses it.
    {
        NodeConfig cfg(keyFactory);
        BOOST_REQUIRE_NO_THROW(
            cfg.loadGenesisConfigFromString(genesis("2", ethLaneGenesisSections())));
        BOOST_CHECK_EXCEPTION(cfg.validateELModeInvariants(), InvalidConfig, [](auto const& e) {
            return errinfoContains(
                e, "executor.version=2 (the L1 EL lane) requires a [fork_timestamps] section");
        });
    }
    // [fork_timestamps] on a FISCO lane: rejected by validateL2Invariants.
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(genesis("1", schedule)),
            InvalidConfig, [](auto const& e) {
                return errinfoContains(e, "[fork_timestamps] section requires executor.version=2");
            });
    }
    // [ethereum] mode=none / an unknown value next to the schedule: not read.
    for (auto const* mode : {"none", "invalid"})
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_NO_THROW(cfg.loadGenesisConfigFromString(genesis("2",
            std::string("[ethereum]\nmode=") + mode + "\n" + schedule + ethLaneGenesisSections())));
    }
}

// The L1 EL lane's chain id must be explicit: no silent fallback to Ethereum mainnet. It is
// checked in validateL2Invariants (genesis load time, order-independent), so tools that load
// config.ini before config.genesis are unaffected.
BOOST_AUTO_TEST_CASE(elModeRequiresChainId)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    const std::string node =
        "1234567890123456789012345678901234567890123456789012345678901234"
        "1234567890123456789012345678901234567890123456789012345678901234";
    const std::string head =
        "[version]\ncompatibility_version=3.18.0\n"
        "[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=";
    const std::string tail =
        "\n"
        "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\nleader_period=1\n"
        "node.0=" +
        node +
        ":1:1\n"
        "[tx]\ngas_limit=3000000000\n"
        "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
        "auth_admin_account=0x0000000000000000000000000000000000000001\n"
        "version=2\n"
        "[fork_timestamps]\nlondon_time=0\nparis_time=0\nshanghai_time=1681338455\n"
        "cancun_time=1710338135\nprague_time=1746612311\n"
        "merge_block=0\n" +
        // executor.version=2 binds the [alloc.*] + [eth_genesis_header] sections both ways.
        ethLaneGenesisSections();
    const std::string ini = "[ethereum]\n";

    // Explicit valid chain id -> parsed and pinned.
    {
        NodeConfig cfg(keyFactory);
        BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(head + "11155111" + tail));
        BOOST_REQUIRE_NO_THROW(cfg.loadConfigFromString(ini));
        BOOST_REQUIRE_NO_THROW(cfg.validateELModeInvariants());
        BOOST_CHECK(cfg.ethereumELModeEnabled());
        BOOST_CHECK_EQUAL(cfg.ethereumChainId(), 11155111u);
    }
    // [chain] chain_id zero -> rejected at genesis load, no mainnet fallback.
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(head + "0" + tail), InvalidConfig,
            [](auto const& e) { return errinfoContains(e, "[chain].chain_id must be non-zero"); });
    }
    // [chain] chain_id overflowing uint64 -> rejected at genesis load.
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(
            cfg.loadGenesisConfigFromString(head + "99999999999999999999999" + tail), InvalidConfig,
            [](auto const& e) { return errinfoContains(e, "[chain].chain_id must fit uint64"); });
    }
    // v2 genesis without the schedule: the getter still carries the
    // Ethereum lane's [chain] chain_id — it is copied on every lane >= 2 at genesis load.
    // Both v2 drivers refuse this genesis at startup (NodeConfigBlockDriverTest).
    {
        NodeConfig cfg(keyFactory);
        const std::string plainGenesis =
            "[version]\ncompatibility_version=3.18.0\n"
            "[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=7\n"
            "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\nleader_period=1\n"
            "node.0=" +
            node +
            ":1:1\n"
            "[tx]\ngas_limit=3000000000\n"
            "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
            "auth_admin_account=0x0000000000000000000000000000000000000001\n"
            "version=2\n" +
            ethLaneGenesisSections();
        BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(plainGenesis));
        BOOST_CHECK_EQUAL(cfg.ethereumChainId(), 7u);
    }
    // Tools order (config.ini BEFORE config.genesis): pure parsing, no genesis
    // reads in loadEthereumConfig — both loads pass, and the post-load hook
    // validates afterwards.
    {
        NodeConfig cfg(keyFactory);
        BOOST_REQUIRE_NO_THROW(cfg.loadConfigFromString(ini));
        BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(head + "11155111" + tail));
        BOOST_REQUIRE_NO_THROW(cfg.validateELModeInvariants());
        BOOST_CHECK_EQUAL(cfg.ethereumChainId(), 11155111u);
    }
}

// ethereum.max_batch_size sizes the RLPx header/body requests the EL
// downloader will issue; bound it while the config surface is unreleased
// (geth caps one request at MaxHeaderFetch=192 / MaxBodyFetch=128).
BOOST_AUTO_TEST_CASE(ethereumMaxBatchSizeBounds)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    // Default when the key is absent.
    {
        NodeConfig cfg(keyFactory);
        BOOST_REQUIRE_NO_THROW(cfg.loadConfigFromString("[ethereum]\n"));
        BOOST_CHECK_EQUAL(cfg.ethereumMaxBatchSize(), 192u);
    }
    // Boundary values pass.
    {
        NodeConfig cfg(keyFactory);
        BOOST_REQUIRE_NO_THROW(cfg.loadConfigFromString("[ethereum]\nmax_batch_size=1024\n"));
        BOOST_CHECK_EQUAL(cfg.ethereumMaxBatchSize(), 1024u);
    }
    // Zero and anything above 1024 are rejected.
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(cfg.loadConfigFromString("[ethereum]\nmax_batch_size=0\n"),
            InvalidConfig, [](auto const& e) {
                return errinfoContains(e, "max_batch_size must be in [1, 1024]");
            });
    }
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(cfg.loadConfigFromString("[ethereum]\nmax_batch_size=1025\n"),
            InvalidConfig, [](auto const& e) {
                return errinfoContains(e, "max_batch_size must be in [1, 1024]");
            });
    }
}

// [ethereum] reorg_window bounds the EL shallow-reorg depth (EthereumChainRollback.h);
// pairing with storage.mpt_prune_window is enforced by validateELModeInvariants: a
// rollback target's trie nodes must still be on disk, so pruning (when enabled) must
// retain at least as many blocks as a reorg may rewind.
BOOST_AUTO_TEST_CASE(elReorgWindowVsMptPruneWindow)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    const std::string node =
        "1234567890123456789012345678901234567890123456789012345678901234"
        "1234567890123456789012345678901234567890123456789012345678901234";
    const std::string genesis =
        "[version]\ncompatibility_version=3.18.0\n"
        "[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=1\n"
        "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\nleader_period=1\n"
        "node.0=" +
        node +
        ":1:1\n"
        "[tx]\ngas_limit=3000000000\n"
        "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
        "auth_admin_account=0x0000000000000000000000000000000000000001\n"
        "version=2\n"
        "[fork_timestamps]\nlondon_time=0\nparis_time=0\nshanghai_time=1681338455\n"
        "cancun_time=1710338135\nprague_time=1746612311\n"
        "merge_block=0\n" +
        // executor.version=2 binds the [alloc.*] + [eth_genesis_header] sections both ways.
        ethLaneGenesisSections();

    auto loadBoth = [&](NodeConfig& cfg, std::string const& ini) {
        BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(genesis));
        BOOST_REQUIRE_NO_THROW(cfg.loadConfigFromString(ini));
    };

    // Default: reorg_window=256, pruning disabled (-1) — always safe.
    {
        NodeConfig cfg(keyFactory);
        loadBoth(cfg, "[ethereum]\n");
        BOOST_REQUIRE_NO_THROW(cfg.validateELModeInvariants());
        BOOST_CHECK_EQUAL(cfg.ethereumReorgWindow(), 256);
    }
    // Pruning window larger than / equal to the reorg window: accepted.
    for (auto const* ini : {"[ethereum]\n[storage]\nmpt_prune_window=300\n",
             "[ethereum]\nreorg_window=100\n[storage]\nmpt_prune_window=100\n"})
    {
        NodeConfig cfg(keyFactory);
        loadBoth(cfg, ini);
        BOOST_REQUIRE_NO_THROW(cfg.validateELModeInvariants());
    }
    // Pruning enabled below the reorg window: rejected — an in-window rollback target
    // would have its trie nodes pruned already.
    {
        NodeConfig cfg(keyFactory);
        loadBoth(cfg, "[ethereum]\n[storage]\nmpt_prune_window=100\n");
        BOOST_CHECK_EXCEPTION(cfg.validateELModeInvariants(), InvalidConfig, [](auto const& e) {
            return errinfoContains(e, "must be -1 (disabled) or >= ethereum.reorg_window");
        });
    }
    // Rollback disabled (reorg_window=0): no rewind can happen, so a small pruning
    // window is fine.
    {
        NodeConfig cfg(keyFactory);
        loadBoth(cfg, "[ethereum]\nreorg_window=0\n[storage]\nmpt_prune_window=100\n");
        BOOST_REQUIRE_NO_THROW(cfg.validateELModeInvariants());
    }
    // reorg_window itself: negative or absurdly large is a load-time error.
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(cfg.loadConfigFromString("[ethereum]\nreorg_window=-1\n"),
            InvalidConfig,
            [](auto const& e) { return errinfoContains(e, "ethereum.reorg_window must be"); });
    }
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(cfg.loadConfigFromString("[ethereum]\nreorg_window=100001\n"),
            InvalidConfig,
            [](auto const& e) { return errinfoContains(e, "ethereum.reorg_window must be"); });
    }
}

// The challenger data plane (debug_dbGet, gated by enable_debug_api on EITHER listener) reads
// MPT preimages straight out of the node store with no retention guarantee: a block still
// inside a dispute game's challenge window can have its trie nodes pruned, and kona-host then
// receives an indistinguishable -32000 "not found". The dispute window is a chain/contract
// parameter rather than a node config, so the enforceable invariant is the archive one — a node
// serving preimages must not bound MPT disk growth. validateELModeInvariants refuses to boot
// one that does.
BOOST_AUTO_TEST_CASE(enableDebugApiRequiresArchivePruneWindow)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    const std::string node =
        "1234567890123456789012345678901234567890123456789012345678901234"
        "1234567890123456789012345678901234567890123456789012345678901234";
    const std::string genesis =
        "[version]\ncompatibility_version=3.18.0\n"
        "[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=1\n"
        "[web3]\nchain_id=11155111\n"
        "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\nleader_period=1\n"
        "node.0=" +
        node +
        ":1:1\n"
        "[tx]\ngas_limit=3000000000\n"
        "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
        "auth_admin_account=0x0000000000000000000000000000000000000001\n"
        "version=2\n"
        "[fork_timestamps]\nlondon_time=0\nparis_time=0\nshanghai_time=1681338455\n"
        "cancun_time=1710338135\nprague_time=1746612311\n"
        "merge_block=0\n" +
        // executor.version=2 binds the [alloc.*] + [eth_genesis_header] sections both ways.
        ethLaneGenesisSections();

    auto loadBoth = [&](NodeConfig& cfg, std::string const& ini) {
        BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(genesis));
        BOOST_REQUIRE_NO_THROW(cfg.loadConfigFromString(ini));
    };

    // The web3 listener's enable_debug_api with a finite prune window (valid for the reorg rule,
    // but not archive): rejected.
    {
        NodeConfig cfg(keyFactory);
        loadBoth(cfg,
            "[web3_rpc]\nenable_debug_api=true\n"
            "[storage]\nmpt_prune_window=300\n");
        BOOST_CHECK_EXCEPTION(cfg.validateELModeInvariants(), InvalidConfig, [](auto const& e) {
            return errinfoContains(
                e, "enable_debug_api requires storage.mpt_prune_window = -1");
        });
    }
    // The Engine API (8551) listener's own key is gated identically — "either listener" is the
    // invariant, not "the web3 listener".
    {
        NodeConfig cfg(keyFactory);
        loadBoth(cfg,
            "[engine_rpc]\nenable_debug_api=true\n"
            "[storage]\nmpt_prune_window=300\n");
        BOOST_CHECK_EXCEPTION(cfg.validateELModeInvariants(), InvalidConfig, [](auto const& e) {
            return errinfoContains(
                e, "enable_debug_api requires storage.mpt_prune_window = -1");
        });
    }
    // Archive (the -1 default) with the namespace on: accepted.
    {
        NodeConfig cfg(keyFactory);
        loadBoth(cfg, "[web3_rpc]\nenable_debug_api=true\n");
        BOOST_REQUIRE_NO_THROW(cfg.validateELModeInvariants());
    }
    // Pruning on with the namespace off: unaffected — the invariant is scoped to the gate.
    {
        NodeConfig cfg(keyFactory);
        loadBoth(cfg, "[storage]\nmpt_prune_window=300\n");
        BOOST_REQUIRE_NO_THROW(cfg.validateELModeInvariants());
    }
}

// Reload is a supported shape: a second loadGenesisConfig without the fork schedule must
// clear the previous one (a stale schedule would satisfy the v2 [fork_timestamps] rule).
BOOST_AUTO_TEST_CASE(loadForkTimestampsReloadClears)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    const std::string node =
        "1234567890123456789012345678901234567890123456789012345678901234"
        "1234567890123456789012345678901234567890123456789012345678901234";
    const std::string base =
        "[version]\ncompatibility_version=3.18.0\n"
        "[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=1\n"
        "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\nleader_period=1\n"
        "node.0=" +
        node +
        ":1:1\n"
        "[tx]\ngas_limit=3000000000\n"
        "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
        "auth_admin_account=0x0000000000000000000000000000000000000001\n"
        "version=2\n";

    NodeConfig cfg(keyFactory);
    // First load: fork schedule (+ the lane-mandatory sections: every config in this test is
    // executor.version=2).
    BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(
        base +
        "[fork_timestamps]\nlondon_time=0\nparis_time=0\nshanghai_time=1681338455\n"
        "cancun_time=1710338135\nprague_time=1746612311\n"
        "merge_block=0\n" +
        ethLaneGenesisSections()));
    BOOST_CHECK(cfg.genesisConfig().m_ethereumForkSchedule.has_value());

    // Reload without the section: it must be cleared, not retained.
    BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(base + ethLaneGenesisSections()));
    BOOST_CHECK(!cfg.genesisConfig().m_ethereumForkSchedule.has_value());
}

// Fork timestamp parsing must fail fast: std::stoull accepted a leading '-' (wrapping to
// "never activates") and silently truncated trailing garbage; the from_chars parser rejects
// both, like every neighbouring parse.
BOOST_AUTO_TEST_CASE(forkTimestampsRejectMalformed)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    const std::string node =
        "1234567890123456789012345678901234567890123456789012345678901234"
        "1234567890123456789012345678901234567890123456789012345678901234";
    const std::string base =
        "[version]\ncompatibility_version=3.18.0\n"
        "[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=1\n"
        "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\nleader_period=1\n"
        "node.0=" +
        node +
        ":1:1\n"
        "[tx]\ngas_limit=3000000000\n"
        "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
        "auth_admin_account=0x0000000000000000000000000000000000000001\n"
        "version=2\n"
        "[fork_timestamps]\nlondon_time=0\nparis_time=0\nshanghai_time=1681338455\n"
        "cancun_time=1710338135\nprague_time=";

    // A leading '-' must not wrap to 2^64-1 ("fork never activates").
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(base + "-1\n"), InvalidConfig,
            [](auto const& e) { return errinfoContains(e, "prague_time invalid timestamp"); });
    }
    // Trailing garbage must not be silently truncated to a wrong schedule.
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(base + "1746612311abc\n"),
            InvalidConfig,
            [](auto const& e) { return errinfoContains(e, "prague_time invalid timestamp"); });
    }
    // 0x-prefixed hex is still accepted. (executor.version=2 also binds the
    // [alloc.*] + [eth_genesis_header] sections; the rejected cases above throw in
    // loadForkTimestamps, before that binding runs.)
    {
        NodeConfig cfg(keyFactory);
        BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(
            base + "0x67f9f25b\nmerge_block=0\n" + ethLaneGenesisSections()));
        BOOST_CHECK_EQUAL(cfg.genesisConfig().m_ethereumForkSchedule->m_pragueTime, 0x67f9f25bu);
    }
    // paris_time is REQUIRED: an omitted key must not silently default to 0 — a
    // mainnet-shaped schedule (london_time > 0) would then be rejected by the
    // fork-order ladder, and an implicit 0 is exactly the silent "active from
    // genesis" default the rest of this loader refuses.
    {
        NodeConfig cfg(keyFactory);
        std::string noParis = base;
        noParis.replace(noParis.find("paris_time=0\n"), std::string("paris_time=0\n").size(), "");
        BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(noParis + "1746612311\n"),
            InvalidConfig,
            [](auto const& e) { return errinfoContains(e, "paris_time is required"); });
    }
}

// The L1 ladder accepts the UINT64_MAX not-scheduled sentinel for forks the chain never
// schedules: eth-sync-check --genesis2ini emits exactly this shape for geth-format geneses
// whose config stops at an earlier fork (e.g. shanghaiTime only), and the ladder's
// non-decreasing check treats UINT64_MAX as the terminal "never activates" value. Only the
// OP [op_fork_timestamps] lane refuses the literal sentinel (fail-closed; covered by
// NodeConfigOpForkTimestampsTest.sentinelAsDeclaredTimestampRejected).
BOOST_AUTO_TEST_CASE(forkTimestampsAcceptNotScheduledSentinel)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    const std::string node =
        "1234567890123456789012345678901234567890123456789012345678901234"
        "1234567890123456789012345678901234567890123456789012345678901234";
    const std::string base =
        "[version]\ncompatibility_version=3.18.0\n"
        "[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=1\n"
        "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\nleader_period=1\n"
        "node.0=" +
        node +
        ":1:1\n"
        "[tx]\ngas_limit=3000000000\n"
        "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
        "auth_admin_account=0x0000000000000000000000000000000000000001\n"
        "version=2\n"
        "[fork_timestamps]\nlondon_time=0\nparis_time=0\nshanghai_time=1681338455\n";

    // Unscheduled tail: cancun/prague declared as the sentinel — eth-sync-check's emission
    // for a Shanghai-at-genesis chain. Loads, and the schedule carries the sentinel.
    {
        NodeConfig cfg(keyFactory);
        BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(
            base + "cancun_time=18446744073709551615\nprague_time=18446744073709551615\n"
                   "merge_block=0\n" +
            ethLaneGenesisSections()));
        auto const& schedule = cfg.genesisConfig().m_ethereumForkSchedule;
        BOOST_REQUIRE(schedule.has_value());
        BOOST_CHECK_EQUAL(schedule->m_shanghaiTime, 1681338455u);
        BOOST_CHECK_EQUAL(schedule->m_cancunTime, std::numeric_limits<uint64_t>::max());
        BOOST_CHECK_EQUAL(schedule->m_pragueTime, std::numeric_limits<uint64_t>::max());
    }
    // The 0x-hex spelling is the same value.
    {
        NodeConfig cfg(keyFactory);
        BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(
            base + "cancun_time=0xffffffffffffffff\nprague_time=0xffffffffffffffff\n"
                   "merge_block=0\n" +
            ethLaneGenesisSections()));
        BOOST_CHECK_EQUAL(cfg.genesisConfig().m_ethereumForkSchedule->m_cancunTime,
            std::numeric_limits<uint64_t>::max());
    }
}

// Activation times must be non-decreasing down the fork ladder (geth rejects an
// out-of-order schedule via ChainConfig.CheckConfigForkOrder); UINT64_MAX
// ("not yet active") is terminal, so a scheduled time after it is a decrease.
BOOST_AUTO_TEST_CASE(forkTimestampsRejectOutOfOrder)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    const std::string node =
        "1234567890123456789012345678901234567890123456789012345678901234"
        "1234567890123456789012345678901234567890123456789012345678901234";
    const std::string head =
        "[version]\ncompatibility_version=3.18.0\n"
        "[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=1\n"
        "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\nleader_period=1\n"
        "node.0=" +
        node +
        ":1:1\n"
        "[tx]\ngas_limit=3000000000\n"
        "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
        "auth_admin_account=0x0000000000000000000000000000000000000001\n"
        "version=2\n"
        "[fork_timestamps]\nlondon_time=0\nparis_time=0\n";

    // Cancun earlier than Shanghai -> rejected.
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(
                                  head + "shanghai_time=1710338135\n"
                                         "cancun_time=1681338455\nprague_time=1746612311\n"
                                         "merge_block=0\n"),
            InvalidConfig, [](auto const& e) {
                return errinfoContains(e, "cancun_time (1681338455) is earlier than");
            });
    }
    // A scheduled bpo1 while osaka is unscheduled (UINT64_MAX, terminal) -> decreasing.
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(
                                  head + "shanghai_time=1681338455\n"
                                         "cancun_time=1710338135\nprague_time=1746612311\n"
                                         "merge_block=0\n"
                                         "bpo1_time=1750000000\n"),
            InvalidConfig, [](auto const& e) {
                return errinfoContains(e, "bpo1_time (1750000000) is earlier than");
            });
    }
    // A scheduled osaka with unscheduled bpos is fine (MAX is non-decreasing). The v2 lane
    // binding needs the [alloc.*] + [eth_genesis_header] sections; the rejected cases above
    // throw in loadForkTimestamps, before that binding runs.
    {
        NodeConfig cfg(keyFactory);
        BOOST_REQUIRE_NO_THROW(
            cfg.loadGenesisConfigFromString(head +
                                            "shanghai_time=1681338455\n"
                                            "cancun_time=1710338135\nprague_time=1746612311\n"
                                            "merge_block=0\n"
                                            "osaka_time=1767225548\n" +
                                            ethLaneGenesisSections()));
        BOOST_CHECK_EQUAL(cfg.genesisConfig().m_ethereumForkSchedule->m_osakaTime, 1767225548u);
    }
}

// merge_block ([fork_timestamps], config.genesis): the chain's only block-based
// fork. REQUIRED whenever the section is present (no chain-agnostic default — a
// non-Sepolia chain must declare its own); 0 = PoS from genesis. Parsed with the
// same strict decimal/0x-hex rules as the timestamps.
BOOST_AUTO_TEST_CASE(ethereumMergeBlockParsing)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    const std::string node =
        "1234567890123456789012345678901234567890123456789012345678901234"
        "1234567890123456789012345678901234567890123456789012345678901234";
    const std::string base =
        "[version]\ncompatibility_version=3.18.0\n"
        "[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=1\n"
        "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\nleader_period=1\n"
        "node.0=" +
        node +
        ":1:1\n"
        "[tx]\ngas_limit=3000000000\n"
        "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
        "auth_admin_account=0x0000000000000000000000000000000000000001\n"
        "version=2\n"
        "[fork_timestamps]\nlondon_time=0\nparis_time=0\nshanghai_time=1681338455\n"
        "cancun_time=1710338135\nprague_time=1746612311\n";

    // Absent key -> rejected like the other non-tail ladder keys: there is no
    // chain-agnostic default, and silently falling back to Sepolia's 1735371
    // would route a non-Sepolia chain's early blocks through the (deliberately
    // permissive) PoW validation branch.
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(base), InvalidConfig,
            [](auto const& e) { return errinfoContains(e, "merge_block is required"); });
    }
    // Explicit 0 -> PoS from genesis (pure-PoS chains). (executor.version=2 also binds the
    // [alloc.*] + [eth_genesis_header] sections; the rejected cases throw in
    // loadForkTimestamps, before that binding runs.)
    {
        NodeConfig cfg(keyFactory);
        BOOST_REQUIRE_NO_THROW(
            cfg.loadGenesisConfigFromString(base + "merge_block=0\n" + ethLaneGenesisSections()));
        BOOST_CHECK_EQUAL(cfg.ethereumMergeBlock(), 0u);
    }
    // 0x-prefixed hex is accepted, like the timestamps.
    {
        NodeConfig cfg(keyFactory);
        BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(
            base + "merge_block=0x1a7acb\n" + ethLaneGenesisSections()));
        BOOST_CHECK_EQUAL(cfg.ethereumMergeBlock(), 1735371u);
    }
    // Malformed values fail fast like every neighbouring parse.
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(base + "merge_block=-1\n"),
            InvalidConfig,
            [](auto const& e) { return errinfoContains(e, "merge_block invalid timestamp"); });
    }
    // The not-scheduled sentinel is a TIMESTAMP concept; on a block number it would
    // silently read as "pre-merge forever" (every block validated under PoW rules and
    // rewarded as such), so merge_block refuses it even though the timestamp keys of the
    // same section accept it.
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(
            cfg.loadGenesisConfigFromString(base + "merge_block=18446744073709551615\n"),
            InvalidConfig,
            [](auto const& e) { return errinfoContains(e, "not-scheduled sentinel"); });
    }
    // Reload without the section resets to the unset value (0), not a stale one.
    {
        NodeConfig cfg(keyFactory);
        BOOST_REQUIRE_NO_THROW(
            cfg.loadGenesisConfigFromString(base + "merge_block=42\n" + ethLaneGenesisSections()));
        BOOST_CHECK_EQUAL(cfg.ethereumMergeBlock(), 42u);
        const std::string plain =
            "[version]\ncompatibility_version=3.18.0\n"
            "[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=1\n"
            "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\nleader_period=1\n"
            "node.0=" +
            node +
            ":1:1\n"
            "[tx]\ngas_limit=3000000000\n"
            "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
            "auth_admin_account=0x0000000000000000000000000000000000000001\n"
            "version=2\n" +
            ethLaneGenesisSections();
        BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(plain));
        BOOST_CHECK_EQUAL(cfg.ethereumMergeBlock(), 0u);
    }
}

// ethereum.finalized_checkpoint (config.ini): optional operator-pinned
// "<number>:<0xHASH>" trust anchor. Validated eagerly at config load.
BOOST_AUTO_TEST_CASE(ethereumFinalizedCheckpointParsing)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    const std::string hash64 = "0x1111111111111111111111111111111111111111111111111111111111111111";
    // Absent / empty -> no checkpoint.
    {
        NodeConfig cfg(keyFactory);
        BOOST_REQUIRE_NO_THROW(cfg.loadConfigFromString("[ethereum]\n"));
        BOOST_CHECK(!cfg.ethereumFinalizedCheckpoint().has_value());
    }
    {
        NodeConfig cfg(keyFactory);
        BOOST_REQUIRE_NO_THROW(cfg.loadConfigFromString("[ethereum]\nfinalized_checkpoint=\n"));
        BOOST_CHECK(!cfg.ethereumFinalizedCheckpoint().has_value());
    }
    // Valid "<number>:<0xHASH>" -> parsed.
    {
        NodeConfig cfg(keyFactory);
        BOOST_REQUIRE_NO_THROW(
            cfg.loadConfigFromString("[ethereum]\nfinalized_checkpoint=9200000:" + hash64 + "\n"));
        auto const& checkpoint = cfg.ethereumFinalizedCheckpoint();
        BOOST_REQUIRE(checkpoint.has_value());
        BOOST_CHECK_EQUAL(checkpoint->number, 9200000u);
        BOOST_CHECK(checkpoint->hash == crypto::HashType(hash64));
    }
    // Malformed values are rejected at load time.
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(
            cfg.loadConfigFromString("[ethereum]\nfinalized_checkpoint=9200000\n"), InvalidConfig,
            [](auto const& e) { return errinfoContains(e, "finalized_checkpoint must be"); });
    }
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(
            cfg.loadConfigFromString("[ethereum]\nfinalized_checkpoint=abc:" + hash64 + "\n"),
            InvalidConfig, [](auto const& e) {
                return errinfoContains(e, "finalized_checkpoint(number) must be decimal digits");
            });
    }
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(
            cfg.loadConfigFromString("[ethereum]\nfinalized_checkpoint=1:0x1234\n"), InvalidConfig,
            [](auto const& e) {
                return errinfoContains(e, "finalized_checkpoint(hash) must be 64 hex chars");
            });
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
