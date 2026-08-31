/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

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

// EVMC revision config (ethereum-executor, executor_version=2): parses the
// explicit revision and the block-height fork transitions from the genesis
// config, and the serialized form shows up in the generated genesis data.
BOOST_AUTO_TEST_CASE(evmcRevisionConfig)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    const std::string node =
        "1234567890123456789012345678901234567890123456789012345678901234"
        "1234567890123456789012345678901234567890123456789012345678901234";
    std::string genesis =
        "[version]\ncompatibility_version=3.18.0\n"
        "[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=1\n"
        "[web3]\nchain_id=1\n"
        "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\nleader_period=1\n"
        "node.0=" +
        node +
        ":1:1\n"
        "[tx]\ngas_limit=3000000000\n"
        "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
        "auth_admin_account=0x0000000000000000000000000000000000000001\n"
        "evm_revision=cancun\n"
        "evm_revision_forks=0:cancun, 100000:osaka\n";

    NodeConfig cfg(keyFactory);
    BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(genesis));
    BOOST_REQUIRE(cfg.ledgerConfig());

    auto const& gc = cfg.genesisConfig();
    BOOST_REQUIRE(gc.m_evmcRevision.has_value());
    BOOST_CHECK_EQUAL(*gc.m_evmcRevision, EVMC_CANCUN);
    BOOST_REQUIRE_EQUAL(gc.m_evmcRevisionForks.size(), 2u);
    BOOST_CHECK_EQUAL(gc.m_evmcRevisionForks.at(0), EVMC_CANCUN);
    BOOST_CHECK_EQUAL(gc.m_evmcRevisionForks.at(100000), EVMC_OSAKA);

    auto data = bcos::tool::generateGenesisData(gc, *cfg.ledgerConfig());
    BOOST_CHECK(data.find("evmRevision:0:cancun,100000:osaka") != std::string::npos);
}

// A v2 chain (ethereum-executor) MUST pin its EVMC revision explicitly: the revision is
// consumed on every block, and a binary-side default would be recorded nowhere on-chain
// (an implicit hard fork on binary upgrade). loadExecutorConfig rejects executor.version=2
// without evm_revision / evm_revision_forks. v0/v1 are unaffected.
BOOST_AUTO_TEST_CASE(executorV2RequiresEvmcRevision)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    const std::string node =
        "1234567890123456789012345678901234567890123456789012345678901234"
        "1234567890123456789012345678901234567890123456789012345678901234";
    const std::string base =
        "[version]\ncompatibility_version=3.18.0\n"
        "[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=1\n"
        "[web3]\nchain_id=1\n"
        "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\nleader_period=1\n"
        "node.0=" +
        node +
        ":1:1\n"
        "[tx]\ngas_limit=3000000000\n"
        "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
        "auth_admin_account=0x0000000000000000000000000000000000000001\n";

    // version=2 with no EVMC revision -> rejected.
    {
        NodeConfig cfg(keyFactory);
        std::string genesis = base + "version=2\n";
        BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(genesis), InvalidConfig,
            [](auto const& e) { return errinfoContains(e, "requires an explicit"); });
    }

    // version=2 with an explicit single revision -> accepted.
    {
        NodeConfig cfg(keyFactory);
        std::string genesis = base + "version=2\nevm_revision=cancun\n";
        BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(genesis));
    }

    // version=2 with fork transitions (no single revision) -> accepted.
    {
        NodeConfig cfg(keyFactory);
        std::string genesis = base + "version=2\nevm_revision_forks=0:cancun,100000:osaka\n";
        BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(genesis));
    }

    // version=1 with no EVMC revision -> unaffected (v0/v1 never consume it).
    {
        NodeConfig cfg(keyFactory);
        std::string genesis = base + "version=1\n";
        BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(genesis));
    }
}

// v2 must also be able to PERSIST its revision: Ledger::buildGenesisBlock writes
// evmc_revision only for compatibility_version >= V3_18_0. Below that the operator is
// forced to write a value that is then ignored (or, below 3.15.0, no revision is
// injected at all and every transaction throws EvmcRevisionNotConfigured). Reject.
BOOST_AUTO_TEST_CASE(executorV2RequiresCompat318)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    const std::string node =
        "1234567890123456789012345678901234567890123456789012345678901234"
        "1234567890123456789012345678901234567890123456789012345678901234";
    const std::string base =
        "[version]\ncompatibility_version=";
    const std::string mid =
        "\n[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=1\n"
        "[web3]\nchain_id=1\n"
        "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\nleader_period=1\n"
        "node.0=" +
        node +
        ":1:1\n"
        "[tx]\ngas_limit=3000000000\n"
        "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
        "auth_admin_account=0x0000000000000000000000000000000000000001\n"
        "version=2\nevm_revision=cancun\n";

    // 3.15.0 <= compat < 3.18.0 -> rejected (revision cannot be persisted on-chain).
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(base + "3.17.0" + mid),
            InvalidConfig,
            [](auto const& e) {
                return errinfoContains(e, "compatibility_version >= 3.18.0");
            });
    }
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(base + "3.15.0" + mid),
            InvalidConfig,
            [](auto const& e) {
                return errinfoContains(e, "compatibility_version >= 3.18.0");
            });
    }
    // compat >= 3.18.0 -> accepted.
    {
        NodeConfig cfg(keyFactory);
        BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(base + "3.18.0" + mid));
    }
}

// evm_revision_forks edge cases (review test suggestion): out-of-order entries are
// normalized by the map (ascending), a negative block height is rejected, and an
// unknown revision name is rejected.
BOOST_AUTO_TEST_CASE(evmcRevisionForksEdgeCases)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    const std::string node =
        "1234567890123456789012345678901234567890123456789012345678901234"
        "1234567890123456789012345678901234567890123456789012345678901234";
    const std::string base =
        "[version]\ncompatibility_version=3.18.0\n"
        "[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=1\n"
        "[web3]\nchain_id=1\n"
        "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\nleader_period=1\n"
        "node.0=" +
        node +
        ":1:1\n"
        "[tx]\ngas_limit=3000000000\n"
        "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
        "auth_admin_account=0x0000000000000000000000000000000000000001\n"
        "version=2\n";

    // Out-of-order entries: accepted, normalized (map is key-ordered).
    {
        NodeConfig cfg(keyFactory);
        std::string genesis = base + "evm_revision_forks=100000:osaka, 0:cancun\n";
        BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(genesis));
        auto const& gc = cfg.genesisConfig();
        BOOST_REQUIRE_EQUAL(gc.m_evmcRevisionForks.size(), 2u);
        BOOST_CHECK_EQUAL(gc.m_evmcRevisionForks.at(0), EVMC_CANCUN);
        BOOST_CHECK_EQUAL(gc.m_evmcRevisionForks.at(100000), EVMC_OSAKA);
        auto data = bcos::tool::generateGenesisData(gc, *cfg.ledgerConfig());
        BOOST_CHECK(data.find("0:cancun,100000:osaka") != std::string::npos);
    }

    // Negative fork height -> rejected. (Without a 0: entry it would otherwise become the
    // block-0 baseline — an operator typing -5 instead of 5 would silently get a different
    // fork schedule.)
    {
        NodeConfig cfg(keyFactory);
        std::string genesis = base + "evm_revision_forks=-5:cancun,100000:osaka\n";
        BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(genesis), InvalidConfig,
            [](auto const& e) { return errinfoContains(e, "block height must be >= 0"); });
    }

    // Unknown revision name -> rejected.
    {
        NodeConfig cfg(keyFactory);
        std::string genesis = base + "evm_revision_forks=0:notafork\n";
        BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(genesis), InvalidConfig,
            [](auto const& e) { return errinfoContains(e, "invalid revision"); });
    }
}

// EVMC_EXPERIMENTAL is evmone's bucket for in-development EIPs (semantics change between
// releases), not a released fork. Pinning a v2 chain to it would tie consensus to the
// binary, so it must be rejected at config load in both the single-revision and the
// fork-transition forms (the round-trip helper still accepts it — the two name tables
// agree; this is a config-policy decision).
BOOST_AUTO_TEST_CASE(evmcExperimentalRejected)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    const std::string node =
        "1234567890123456789012345678901234567890123456789012345678901234"
        "1234567890123456789012345678901234567890123456789012345678901234";
    const std::string base =
        "[version]\ncompatibility_version=3.18.0\n"
        "[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=1\n"
        "[web3]\nchain_id=1\n"
        "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\nleader_period=1\n"
        "node.0=" +
        node +
        ":1:1\n"
        "[tx]\ngas_limit=3000000000\n"
        "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
        "auth_admin_account=0x0000000000000000000000000000000000000001\n"
        "version=2\n";

    // Single revision = experimental -> rejected.
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(
            cfg.loadGenesisConfigFromString(base + "evm_revision=experimental\n"), InvalidConfig,
            [](auto const& e) { return errinfoContains(e, "evm_revision=experimental"); });
    }
    // Fork transition to experimental -> rejected.
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(
                                  base + "evm_revision_forks=0:experimental\n"),
            InvalidConfig,
            [](auto const& e) {
                return errinfoContains(e, "evm_revision_forks revision \"experimental\"");
            });
    }
    // Control: a released revision is still accepted.
    {
        NodeConfig cfg(keyFactory);
        BOOST_REQUIRE_NO_THROW(
            cfg.loadGenesisConfigFromString(base + "evm_revision=cancun\n"));
    }
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
        "[web3]\nchain_id=1\n"
        "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\nleader_period=1\n"
        "node.0=" +
        node +
        ":1:1\n"
        "[tx]\ngas_limit=3000000000\n"
        "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
        "auth_admin_account=0x0000000000000000000000000000000000000001\n"
        "version=2\n";
    const std::string schedule =
        "[ethereum]\nmode=el\n"
        "[fork_timestamps]\nlondon_time=0\nparis_time=0\nshanghai_time=1681338455\n"
        "cancun_time=1710338135\nprague_time=1746612311\n";

    // Parses into the GenesisConfig and lands in the genesis pin.
    NodeConfig cfg(keyFactory);
    BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(base + schedule));
    auto const& gc = cfg.genesisConfig();
    BOOST_CHECK(gc.m_ethereumELMode);  // [ethereum] mode=el declaration
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
        base + "[ethereum]\nmode=el\n"
               "[fork_timestamps]\nlondon_time=0\nparis_time=0\nshanghai_time=1681338455\n"
               "cancun_time=1710338135\nprague_time=1746612312\n"));
    BOOST_CHECK(
        data != bcos::tool::generateGenesisData(cfg2.genesisConfig(), *cfg2.ledgerConfig()));

    // A node that differs ONLY in the post-Prague tail (osaka scheduled vs omitted)
    // produces the SAME genesis data — the tail is not pinned, so activating Osaka
    // later does not break the restart comparison (EIP-2124 handshake covers it).
    NodeConfig cfgTail(keyFactory);
    BOOST_REQUIRE_NO_THROW(cfgTail.loadGenesisConfigFromString(
        base + "[ethereum]\nmode=el\n"
               "[fork_timestamps]\nlondon_time=0\nparis_time=0\nshanghai_time=1681338455\n"
               "cancun_time=1710338135\nprague_time=1746612311\n"
               "osaka_time=1767225548\n"));
    BOOST_CHECK(
        data == bcos::tool::generateGenesisData(cfgTail.genesisConfig(), *cfgTail.ledgerConfig()));

    // A node with a different [web3] chain_id (same schedule) also produces
    // different genesis data — the id is pinned, not just validated.
    std::string baseOtherChain = base;
    baseOtherChain.replace(baseOtherChain.find("[web3]\nchain_id=1\n"),
        std::string("[web3]\nchain_id=1\n").size(), "[web3]\nchain_id=11155111\n");
    NodeConfig cfg4(keyFactory);
    BOOST_REQUIRE_NO_THROW(cfg4.loadGenesisConfigFromString(baseOtherChain + schedule));
    BOOST_CHECK(
        data != bcos::tool::generateGenesisData(cfg4.genesisConfig(), *cfg4.ledgerConfig()));

    // No [fork_timestamps] section -> no [forkTimestamps] emission (legacy chains stay
    // byte-identical); an explicit evm_revision satisfies the v2 guard instead.
    NodeConfig cfg3(keyFactory);
    BOOST_REQUIRE_NO_THROW(cfg3.loadGenesisConfigFromString(base + "evm_revision=cancun\n"));
    BOOST_CHECK(!cfg3.genesisConfig().m_ethereumForkSchedule.has_value());
    BOOST_CHECK(!cfg3.genesisConfig().m_ethereumELMode);
    auto data3 = bcos::tool::generateGenesisData(cfg3.genesisConfig(), *cfg3.ledgerConfig());
    BOOST_CHECK(data3.find("[forkTimestamps]") == std::string::npos);
    BOOST_CHECK(data3.find("[ethereum]") == std::string::npos);
    BOOST_CHECK(data3.find("[web3]") == std::string::npos);  // chain id emitted only with EL
}

// The EL-mode declaration ([ethereum] mode=el) and its fork schedule are bound together in
// config.genesis: a [fork_timestamps] section on a genesis that does not declare EL mode
// must be rejected (an ordinary executor-v2 chain must not be able to waive the evmc_revision
// / auth_admin_account guards by pasting in a section nothing reads), and an EL declaration
// without the schedule must be rejected too (no schedule = no way to derive the EVM revision).
BOOST_AUTO_TEST_CASE(forkTimestampsRequireELDeclaration)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    const std::string node =
        "1234567890123456789012345678901234567890123456789012345678901234"
        "1234567890123456789012345678901234567890123456789012345678901234";
    const std::string base =
        "[version]\ncompatibility_version=3.18.0\n"
        "[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=1\n"
        "[web3]\nchain_id=1\n"
        "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\nleader_period=1\n"
        "node.0=" +
        node +
        ":1:1\n"
        "[tx]\ngas_limit=3000000000\n"
        "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
        "auth_admin_account=0x0000000000000000000000000000000000000001\n"
        "version=2\n"
        // evm_revision lets loadExecutorConfig's v2 guard pass cleanly, so the "no EL
        // declaration" cases below reach validateL2Invariants — the branch under test — as
        // the ONLY guard that can fire. Without it the executor-v2 guard would throw first
        // and the suite could not distinguish the two.
        "evm_revision=cancun\n";
    const std::string schedule =
        "[fork_timestamps]\nlondon_time=0\nparis_time=0\nshanghai_time=1681338455\n"
        "cancun_time=1710338135\nprague_time=1746612311\n";

    // [fork_timestamps] without [ethereum] mode=el: rejected by validateL2Invariants.
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(base + schedule), InvalidConfig,
            [](auto const& e) {
                return errinfoContains(e, "[fork_timestamps] section requires [ethereum] mode=el");
            });
    }
    // [ethereum] mode=el without [fork_timestamps]: rejected by validateL2Invariants.
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(
            cfg.loadGenesisConfigFromString(base + "[ethereum]\nmode=el\n"), InvalidConfig,
            [](auto const& e) {
                return errinfoContains(e, "mode=el requires a [fork_timestamps] section");
            });
    }
    // [ethereum] mode=none alongside [fork_timestamps]: rejected (declaration is explicit).
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(
            cfg.loadGenesisConfigFromString(base + "[ethereum]\nmode=none\n" + schedule),
            InvalidConfig,
            [](auto const& e) {
                return errinfoContains(e, "[fork_timestamps] section requires [ethereum] mode=el");
            });
    }
    // Invalid [ethereum] mode value: rejected like every neighbouring parse.
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(base + "[ethereum]\nmode=invalid\n"),
            InvalidConfig,
            [](auto const& e) { return errinfoContains(e, "[ethereum].mode invalid"); });
    }
}

// EL mode's chain id must be explicit: no silent fallback to Ethereum mainnet.
// The requirement is keyed on the genesis [ethereum] mode=el declaration and
// checked in validateL2Invariants (genesis load time, order-independent);
// the config.ini->genesis direction is checked by validateELModeInvariants,
// which the node initializers call after BOTH files are loaded — so tools
// that load config.ini before config.genesis are unaffected.
BOOST_AUTO_TEST_CASE(elModeRequiresChainId)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    const std::string node =
        "1234567890123456789012345678901234567890123456789012345678901234"
        "1234567890123456789012345678901234567890123456789012345678901234";
    const std::string head =
        "[version]\ncompatibility_version=3.18.0\n"
        "[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=1\n"
        "[web3]\nchain_id=";
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
        "evm_revision=cancun\n"
        "[ethereum]\nmode=el\n"
        "[fork_timestamps]\nlondon_time=0\nparis_time=0\nshanghai_time=1681338455\n"
        "cancun_time=1710338135\nprague_time=1746612311\n";
    const std::string ini = "[ethereum]\nmode=el\n";

    // Explicit valid chain id -> parsed and pinned.
    {
        NodeConfig cfg(keyFactory);
        BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(head + "11155111" + tail));
        BOOST_REQUIRE_NO_THROW(cfg.loadConfigFromString(ini));
        BOOST_REQUIRE_NO_THROW(cfg.validateELModeInvariants());
        BOOST_CHECK(cfg.ethereumELModeEnabled());
        BOOST_CHECK_EQUAL(cfg.ethereumChainId(), 11155111u);
    }
    // [web3] chain_id absent (loader default "0") -> rejected at genesis load,
    // no mainnet fallback.
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(head + "0" + tail), InvalidConfig,
            [](auto const& e) { return errinfoContains(e, "non-zero [web3] chain_id"); });
    }
    // [web3] chain_id overflowing uint64 -> rejected at genesis load.
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(
            cfg.loadGenesisConfigFromString(head + "99999999999999999999999" + tail),
            InvalidConfig,
            [](auto const& e) { return errinfoContains(e, "chain_id to fit uint64"); });
    }
    // Non-EL genesis (no declaration, no schedule) never requires a chain id (normal
    // FISCO chains are unaffected); the unset getter reads 0, never mainnet 1.
    {
        NodeConfig cfg(keyFactory);
        const std::string plainGenesis =
            "[version]\ncompatibility_version=3.18.0\n"
            "[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=1\n"
            "[web3]\nchain_id=1\n"
            "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\nleader_period=1\n"
            "node.0=" +
            node +
            ":1:1\n"
            "[tx]\ngas_limit=3000000000\n"
            "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
            "auth_admin_account=0x0000000000000000000000000000000000000001\n"
            "version=2\nevm_revision=cancun\n";
        BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(plainGenesis));
        BOOST_REQUIRE_NO_THROW(cfg.loadConfigFromString("[ethereum]\nmode=none\n"));
        BOOST_CHECK(!cfg.ethereumELModeEnabled());
        BOOST_CHECK_EQUAL(cfg.ethereumChainId(), 0u);
    }
    // Tools order (config.ini BEFORE config.genesis): pure parsing, no genesis
    // reads in loadEthereumConfig — both loads pass, and the post-load hook
    // validates the pairing afterwards.
    {
        NodeConfig cfg(keyFactory);
        BOOST_REQUIRE_NO_THROW(cfg.loadConfigFromString(ini));
        BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(head + "11155111" + tail));
        BOOST_REQUIRE_NO_THROW(cfg.validateELModeInvariants());
        BOOST_CHECK_EQUAL(cfg.ethereumChainId(), 11155111u);
    }
    // config.ini mode=el on a genesis that does NOT declare EL: both loaders
    // pass (neither sees the other file), the post-load hook rejects.
    {
        NodeConfig cfg(keyFactory);
        const std::string plainGenesis =
            "[version]\ncompatibility_version=3.18.0\n"
            "[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=1\n"
            "[web3]\nchain_id=1\n"
            "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\nleader_period=1\n"
            "node.0=" +
            node +
            ":1:1\n"
            "[tx]\ngas_limit=3000000000\n"
            "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
            "auth_admin_account=0x0000000000000000000000000000000000000001\n"
            "version=2\nevm_revision=cancun\n";
        BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(plainGenesis));
        BOOST_REQUIRE_NO_THROW(cfg.loadConfigFromString(ini));
        BOOST_CHECK_EXCEPTION(cfg.validateELModeInvariants(), InvalidConfig,
            [](auto const& e) {
                return errinfoContains(e, "requires config.genesis to declare");
            });
    }
    // The symmetric direction: genesis declares EL but config.ini says mode=none —
    // the node would run executor v2 with neither an on-chain nor a
    // timestamp-derived EVM revision. Rejected by the post-load hook.
    {
        NodeConfig cfg(keyFactory);
        BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(head + "11155111" + tail));
        BOOST_REQUIRE_NO_THROW(cfg.loadConfigFromString("[ethereum]\nmode=none\n"));
        BOOST_CHECK_EXCEPTION(cfg.validateELModeInvariants(), InvalidConfig,
            [](auto const& e) {
                return errinfoContains(e, "declares [ethereum] mode=el but config.ini");
            });
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
        BOOST_REQUIRE_NO_THROW(cfg.loadConfigFromString("[ethereum]\nmode=el\n"));
        BOOST_CHECK_EQUAL(cfg.ethereumMaxBatchSize(), 192u);
    }
    // Boundary values pass.
    {
        NodeConfig cfg(keyFactory);
        BOOST_REQUIRE_NO_THROW(
            cfg.loadConfigFromString("[ethereum]\nmode=el\nmax_batch_size=1024\n"));
        BOOST_CHECK_EQUAL(cfg.ethereumMaxBatchSize(), 1024u);
    }
    // Zero and anything above 1024 are rejected.
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(cfg.loadConfigFromString("[ethereum]\nmode=el\nmax_batch_size=0\n"),
            InvalidConfig, [](auto const& e) {
                return errinfoContains(e, "max_batch_size must be in [1, 1024]");
            });
    }
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(
            cfg.loadConfigFromString("[ethereum]\nmode=el\nmax_batch_size=1025\n"), InvalidConfig,
            [](auto const& e) {
                return errinfoContains(e, "max_batch_size must be in [1, 1024]");
            });
    }
}

// Reload is a supported shape: a second loadGenesisConfig without the EL declaration /
// fork schedule must clear the previous values (a stale m_ethereumELMode would waive both
// the executor.evm_revision and the auth_admin_account guards).
BOOST_AUTO_TEST_CASE(loadForkTimestampsReloadClears)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    const std::string node =
        "1234567890123456789012345678901234567890123456789012345678901234"
        "1234567890123456789012345678901234567890123456789012345678901234";
    const std::string base =
        "[version]\ncompatibility_version=3.18.0\n"
        "[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=1\n"
        "[web3]\nchain_id=1\n"
        "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\nleader_period=1\n"
        "node.0=" +
        node +
        ":1:1\n"
        "[tx]\ngas_limit=3000000000\n"
        "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
        "auth_admin_account=0x0000000000000000000000000000000000000001\n"
        "version=2\n"
        "evm_revision=cancun\n";

    NodeConfig cfg(keyFactory);
    // First load: EL declaration + fork schedule.
    BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(
        base + "[ethereum]\nmode=el\n"
               "[fork_timestamps]\nlondon_time=0\nparis_time=0\nshanghai_time=1681338455\n"
               "cancun_time=1710338135\nprague_time=1746612311\n"));
    BOOST_CHECK(cfg.genesisConfig().m_ethereumELMode);
    BOOST_CHECK(cfg.genesisConfig().m_ethereumForkSchedule.has_value());

    // Reload without those sections: both must be cleared, not retained.
    BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(base));
    BOOST_CHECK(!cfg.genesisConfig().m_ethereumELMode);
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
        "[web3]\nchain_id=1\n"
        "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\nleader_period=1\n"
        "node.0=" +
        node +
        ":1:1\n"
        "[tx]\ngas_limit=3000000000\n"
        "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
        "auth_admin_account=0x0000000000000000000000000000000000000001\n"
        "version=2\n"
        "[ethereum]\nmode=el\n"
        "[fork_timestamps]\nlondon_time=0\nparis_time=0\nshanghai_time=1681338455\n"
        "cancun_time=1710338135\nprague_time=";

    // A leading '-' must not wrap to 2^64-1 ("fork never activates").
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(
            cfg.loadGenesisConfigFromString(base + "-1\n"), InvalidConfig,
            [](auto const& e) { return errinfoContains(e, "prague_time invalid timestamp"); });
    }
    // Trailing garbage must not be silently truncated to a wrong schedule.
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(
            cfg.loadGenesisConfigFromString(base + "1746612311abc\n"), InvalidConfig,
            [](auto const& e) { return errinfoContains(e, "prague_time invalid timestamp"); });
    }
    // 0x-prefixed hex is still accepted.
    {
        NodeConfig cfg(keyFactory);
        BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(base + "0x67f9f25b\n"));
        BOOST_CHECK_EQUAL(
            cfg.genesisConfig().m_ethereumForkSchedule->m_pragueTime, 0x67f9f25bu);
    }
    // paris_time is REQUIRED: an omitted key must not silently default to 0 — a
    // mainnet-shaped schedule (london_time > 0) would then be rejected by the
    // fork-order ladder, and an implicit 0 is exactly the silent "active from
    // genesis" default the rest of this loader refuses.
    {
        NodeConfig cfg(keyFactory);
        std::string noParis = base;
        noParis.replace(
            noParis.find("paris_time=0\n"), std::string("paris_time=0\n").size(), "");
        BOOST_CHECK_EXCEPTION(
            cfg.loadGenesisConfigFromString(noParis + "1746612311\n"), InvalidConfig,
            [](auto const& e) { return errinfoContains(e, "paris_time is required"); });
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
        "[web3]\nchain_id=1\n"
        "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\nleader_period=1\n"
        "node.0=" +
        node +
        ":1:1\n"
        "[tx]\ngas_limit=3000000000\n"
        "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
        "auth_admin_account=0x0000000000000000000000000000000000000001\n"
        "version=2\n"
        "[ethereum]\nmode=el\n"
        "[fork_timestamps]\nlondon_time=0\nparis_time=0\n";

    // Cancun earlier than Shanghai -> rejected.
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(
            cfg.loadGenesisConfigFromString(
                head + "shanghai_time=1710338135\n"
                       "cancun_time=1681338455\nprague_time=1746612311\n"),
            InvalidConfig,
            [](auto const& e) {
                return errinfoContains(e, "cancun_time (1681338455) is earlier than");
            });
    }
    // A scheduled bpo1 while osaka is unscheduled (UINT64_MAX, terminal) -> decreasing.
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(
            cfg.loadGenesisConfigFromString(
                head + "shanghai_time=1681338455\n"
                       "cancun_time=1710338135\nprague_time=1746612311\n"
                       "bpo1_time=1750000000\n"),
            InvalidConfig,
            [](auto const& e) {
                return errinfoContains(e, "bpo1_time (1750000000) is earlier than");
            });
    }
    // A scheduled osaka with unscheduled bpos is fine (MAX is non-decreasing).
    {
        NodeConfig cfg(keyFactory);
        BOOST_REQUIRE_NO_THROW(
            cfg.loadGenesisConfigFromString(
                head + "shanghai_time=1681338455\n"
                       "cancun_time=1710338135\nprague_time=1746612311\n"
                       "osaka_time=1767225548\n"));
        BOOST_CHECK_EQUAL(
            cfg.genesisConfig().m_ethereumForkSchedule->m_osakaTime, 1767225548u);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
