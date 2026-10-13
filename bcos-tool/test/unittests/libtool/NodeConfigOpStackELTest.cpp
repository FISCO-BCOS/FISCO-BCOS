/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

// [ethereum] mode=opstack-el: the OP-Stack EL self-sync declaration. The genesis side is
// bound to the OP lane (executor.version >= 3), the OP fork schedule
// ([op_fork_timestamps]), the Ethereum-lane genesis shape ([alloc.*] +
// [eth_genesis_header], both implied by executor.version >= 3) and a non-zero [web3]
// chain_id; the config.ini side is bound to
// the genesis declaration both ways and is mutually exclusive with the engine-driven
// block producers ([op_engine_rpc], enable_single_node_consensus).

#include "ExceptionCheck.h"
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-tool/NodeConfig.h>
#include <boost/test/unit_test.hpp>
#include <string>

using namespace bcos;
using namespace bcos::tool;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(NodeConfigOpStackELTest)

namespace
{
const std::string kNode =
    "1234567890123456789012345678901234567890123456789012345678901234"
    "1234567890123456789012345678901234567890123456789012345678901234";

/// The Ethereum-lane genesis shape ([alloc.0] + [eth_genesis_header]); the OP lane's
/// executor.version >= 3 implies both, so no [features] section is needed. The header
/// values are the fixture from test_NodeConfigEthGenesisHeader.cpp (the hash matches
/// the 21 fields, though only Ledger::buildGenesisBlock checks that).
std::string l2Sections()
{
    return "[alloc.0]\naddress=0x43000000000000000000000000000000000000C0\n"
           "balance=0\nnonce=0\ncode=0x6080604052\n"
           "[eth_genesis_header]\n"
           "parent_hash=0x0000000000000000000000000000000000000000000000000000000000000000\n"
           "sha3_uncles=0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347\n"
           "miner=0x4200000000000000000000000000000000000011\n"
           "state_root=0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421\n"
           "transactions_root=0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421\n"
           "receipts_root=0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421\n"
           "logs_bloom=0x" +
           std::string(512, '0') +
           "\n"
           "difficulty=0x0\nnumber=0x0\ngas_limit=0x1c9c380\ngas_used=0x0\n"
           "timestamp=0x689d5c00\n"
           "extra_data=0x01000000fa000000060000000000000000\n"
           "mix_hash=0x0000000000000000000000000000000000000000000000000000000000000000\n"
           "nonce=0x0000000000000000\nbase_fee_per_gas=0x3b9aca00\n"
           "withdrawals_root=0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421\n"
           "blob_gas_used=0x0\nexcess_blob_gas=0x0\n"
           "parent_beacon_block_root="
           "0x0000000000000000000000000000000000000000000000000000000000000000\n"
           "requests_hash=0xe3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\n"
           "hash=0x8634eabcf9e6df6b91b63cecab2d7af50a0a4fb8e0cc0aaca07cd8d0da32c069\n";
}

/// OP-lane genesis (executor.version=3 + [op_fork_timestamps]) with a configurable
/// [web3] chain_id, optional Ethereum-lane sections and an optional [ethereum] mode
/// declaration.
std::string opGenesis(std::string const& web3ChainId, bool l2, std::string const& ethSection)
{
    return "[version]\ncompatibility_version=3.18.0\n"
           "[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=1\n"
           "[web3]\nchain_id=" +
           web3ChainId +
           "\n"
           "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\nleader_period=1\n"
           "node.0=" +
           kNode +
           ":1:1\n"
           "[tx]\ngas_limit=3000000000\n"
           "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
           "auth_admin_account=0x0000000000000000000000000000000000000001\n"
           "version=3\n"
           "[op_fork_timestamps]\njovian_time=0\nkarst_time=0\n" +
           (l2 ? l2Sections() : "") + ethSection;
}

/// The accepted opstack-el shape: OP lane + L2 sections + the declaration.
std::string opStackELGenesis(std::string const& web3ChainId = "11155420")
{
    return opGenesis(web3ChainId, true, "[ethereum]\nmode=opstack-el\n");
}

NodeConfig loadGenesis(std::string const& genesis)
{
    NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(genesis));
    return cfg;
}
}  // namespace

// The accepted shape: the declaration parses, pins the chain id, and lands in the
// genesis data alongside it.
BOOST_AUTO_TEST_CASE(opStackELGenesisLoadsAndPins)
{
    auto cfg = loadGenesis(opStackELGenesis());
    BOOST_CHECK(cfg.genesisConfig().m_opStackELMode);
    BOOST_CHECK(!cfg.genesisConfig().m_ethereumELMode);
    BOOST_CHECK_EQUAL(cfg.ethereumChainId(), 11155420u);

    BOOST_REQUIRE(cfg.ledgerConfig());
    auto data = bcos::tool::generateGenesisData(cfg.genesisConfig(), *cfg.ledgerConfig());
    BOOST_CHECK(data.find("[ethereum]\nmode:opstack-el\n") != std::string::npos);
    BOOST_CHECK(data.find("[web3]\nchain_id:11155420\n") != std::string::npos);
    // The OP fork pin keeps its own section and its own emit rules.
    BOOST_CHECK(data.find("[opForkTimestamps]\njovian_time:0\nkarst_time:0\n") !=
                std::string::npos);

    // A different chain id produces different genesis data (the id is pinned, not
    // just validated).
    auto other = loadGenesis(opStackELGenesis("10"));
    BOOST_CHECK(data != bcos::tool::generateGenesisData(other.genesisConfig(), *other.ledgerConfig()));
}

// The declaration is bound to the OP lane: on executor.version < 3 the sync client's
// verifier (OpBlockVerifier) could not resolve the fork schedule. (With version=2 and
// no [op_fork_timestamps] the opstack-el block's own guard is the one that fires.)
BOOST_AUTO_TEST_CASE(opStackELRequiresOpLane)
{
    const std::string genesis =
        "[version]\ncompatibility_version=3.18.0\n"
        "[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=1\n"
        "[web3]\nchain_id=11155420\n"
        "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\nleader_period=1\n"
        "node.0=" +
        kNode +
        ":1:1\n"
        "[tx]\ngas_limit=3000000000\n"
        "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
        "auth_admin_account=0x0000000000000000000000000000000000000001\n"
        "version=2\nevm_revision=cancun\n" +
        l2Sections() + "[ethereum]\nmode=opstack-el\n";
    NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(genesis), InvalidConfig,
        [](auto const& e) {
            return errinfoContains(e, "mode=opstack-el requires executor.version >= 3");
        });
}

// Without [op_fork_timestamps] the OP lane binding fires first (an OP chain cannot
// place its forks at all), which is also the guard an opstack-el genesis trips.
BOOST_AUTO_TEST_CASE(opStackELRequiresOpForkSchedule)
{
    const std::string genesis =
        "[version]\ncompatibility_version=3.18.0\n"
        "[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=1\n"
        "[web3]\nchain_id=11155420\n"
        "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\nleader_period=1\n"
        "node.0=" +
        kNode +
        ":1:1\n"
        "[tx]\ngas_limit=3000000000\n"
        "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
        "auth_admin_account=0x0000000000000000000000000000000000000001\n"
        "version=3\n" +
        l2Sections() + "[ethereum]\nmode=opstack-el\n";
    NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(genesis), InvalidConfig,
        [](auto const& e) {
            return errinfoContains(
                e, "executor.version >= 3 (OP lane) requires an [op_fork_timestamps] section");
        });
}

// The sync client replays an Ethereum-shaped OP chain: without the Ethereum-lane
// genesis shape there is no genesis anchor to sync from. There is no feature-flag
// check anymore — executor.version >= 3 already implies the lane, so the generic
// lane binding fires instead: validateL2Invariants rejects the missing [alloc.*]
// section before the opstack-el block is even reached.
BOOST_AUTO_TEST_CASE(opStackELRequiresEthLaneGenesisShape)
{
    NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    BOOST_CHECK_EXCEPTION(
        cfg.loadGenesisConfigFromString(
            opGenesis("11155420", false, "[ethereum]\nmode=opstack-el\n")),
        InvalidConfig, [](auto const& e) {
            return errinfoContains(e, "requires a non-empty [alloc.*] section");
        });
}

// Same rule as mode=el: no silent mainnet fallback for the chain id the fork-id
// handshake and EIP-1559 validation key on.
BOOST_AUTO_TEST_CASE(opStackELRequiresChainId)
{
    // An empty chain_id fails even earlier, in loadWeb3ChainConfig's own parse.
    {
        NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
        BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(opStackELGenesis("")),
            InvalidConfig,
            [](auto const& e) { return errinfoContains(e, "web3ChainId must be a number"); });
    }
    // Any zero spelling lands on the reserved "unset" value and is rejected by the
    // EL-sync chain-id guard.
    for (const auto* chainId : {"0", "00"})
    {
        NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
        BOOST_CHECK_EXCEPTION(
            cfg.loadGenesisConfigFromString(opStackELGenesis(chainId)), InvalidConfig,
            [](auto const& e) { return errinfoContains(e, "non-zero [web3] chain_id"); });
    }
    NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    BOOST_CHECK_EXCEPTION(
        cfg.loadGenesisConfigFromString(opStackELGenesis("99999999999999999999999")),
        InvalidConfig, [](auto const& e) { return errinfoContains(e, "chain_id to fit uint64"); });
}

// Invalid mode values fail fast like every neighbouring parse.
BOOST_AUTO_TEST_CASE(opStackELInvalidModeRejected)
{
    NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    BOOST_CHECK_EXCEPTION(
        cfg.loadGenesisConfigFromString(opStackELGenesis().replace(
            opStackELGenesis().find("mode=opstack-el"), 15, "mode=opstack")),
        InvalidConfig, [](auto const& e) { return errinfoContains(e, "[ethereum].mode invalid"); });
    NodeConfig iniCfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    BOOST_CHECK_EXCEPTION(iniCfg.loadConfigFromString("[ethereum]\nmode=opstack\n"), InvalidConfig,
        [](auto const& e) { return errinfoContains(e, "ethereum.mode invalid"); });
}

// The config.ini <-> config.genesis binding, both directions, checked by
// validateELModeInvariants after BOTH files are loaded.
BOOST_AUTO_TEST_CASE(opStackELIniBinding)
{
    // Both sides declare opstack-el -> accepted.
    {
        auto cfg = loadGenesis(opStackELGenesis());
        BOOST_REQUIRE_NO_THROW(cfg.loadConfigFromString("[ethereum]\nmode=opstack-el\n"));
        BOOST_REQUIRE_NO_THROW(cfg.validateELModeInvariants());
        BOOST_CHECK(cfg.opStackELModeEnabled());
        BOOST_CHECK(!cfg.ethereumELModeEnabled());
    }
    // Genesis declares opstack-el but config.ini says none -> the opstack-el direction
    // of the hook rejects it.
    {
        auto cfg = loadGenesis(opStackELGenesis());
        BOOST_REQUIRE_NO_THROW(cfg.loadConfigFromString("[ethereum]\nmode=none\n"));
        BOOST_CHECK_EXCEPTION(cfg.validateELModeInvariants(), InvalidConfig,
            [](auto const& e) {
                return errinfoContains(e, "declares [ethereum] mode=opstack-el but config.ini");
            });
    }
    // config.ini says el on an opstack-el genesis -> the EL direction of the hook fires
    // first (an el declaration needs an el genesis).
    {
        auto cfg = loadGenesis(opStackELGenesis());
        BOOST_REQUIRE_NO_THROW(cfg.loadConfigFromString("[ethereum]\nmode=el\n"));
        BOOST_CHECK_EXCEPTION(cfg.validateELModeInvariants(), InvalidConfig,
            [](auto const& e) {
                return errinfoContains(
                    e, "ethereum.mode=el requires config.genesis to declare [ethereum] mode=el");
            });
    }
    // config.ini says opstack-el on a genesis that does not declare it -> rejected.
    {
        auto cfg = loadGenesis(opGenesis("11155420", true, ""));
        BOOST_REQUIRE_NO_THROW(cfg.loadConfigFromString("[ethereum]\nmode=opstack-el\n"));
        BOOST_CHECK_EXCEPTION(cfg.validateELModeInvariants(), InvalidConfig,
            [](auto const& e) {
                return errinfoContains(
                    e, "ethereum.mode=opstack-el requires config.genesis to declare");
            });
    }
}

// Both self-sync modes replace the engine-API driver; combining them with it (or with
// the built-in single-node driver) would leave two block movers reachable.
BOOST_AUTO_TEST_CASE(opStackELMutualExclusion)
{
    {
        NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
        BOOST_CHECK_EXCEPTION(
            cfg.loadConfigFromString(
                "[op_engine_rpc]\nenable=true\n[ethereum]\nmode=opstack-el\n"),
            InvalidConfig, [](auto const& e) {
                return errinfoContains(
                    e, "ethereum.mode=opstack-el and op_engine_rpc.enable are mutually exclusive");
            });
    }
    {
        NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
        BOOST_CHECK_EXCEPTION(
            cfg.loadConfigFromString(
                "[consensus]\nenable_single_node_consensus=true\n[ethereum]\nmode=opstack-el\n"),
            InvalidConfig, [](auto const& e) {
                return errinfoContains(e,
                    "ethereum.mode=opstack-el and consensus.enable_single_node_consensus are "
                    "mutually exclusive");
            });
    }
}

// [ethereum] op_block_time_seconds: the OP chain's block cadence, default 2 (every
// superchain chain), bounded to [1, 60] like every neighbouring knob.
BOOST_AUTO_TEST_CASE(opBlockTimeSecondsBounds)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    {
        NodeConfig cfg(keyFactory);
        BOOST_REQUIRE_NO_THROW(cfg.loadConfigFromString("[ethereum]\nmode=opstack-el\n"));
        BOOST_CHECK_EQUAL(cfg.opBlockTimeSeconds(), 2u);
    }
    for (const auto* value : {"1", "60"})
    {
        NodeConfig cfg(keyFactory);
        BOOST_REQUIRE_NO_THROW(cfg.loadConfigFromString(
            std::string("[ethereum]\nmode=opstack-el\nop_block_time_seconds=") + value + "\n"));
        BOOST_CHECK_EQUAL(cfg.opBlockTimeSeconds(), std::stoull(value));
    }
    for (const auto* value : {"0", "61"})
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(
            cfg.loadConfigFromString(
                std::string("[ethereum]\nmode=opstack-el\nop_block_time_seconds=") + value + "\n"),
            InvalidConfig, [](auto const& e) {
                return errinfoContains(e, "op_block_time_seconds must be in [1, 60]");
            });
    }
}

// [ethereum] op_sync_lag_blocks: the download lag behind the peer's UNSAFE head
// (a tip-reorg heuristic, not a finality boundary), default 64, bounded to
// [0, 10000] like every neighbouring knob; 0 = download right up to the tip.
BOOST_AUTO_TEST_CASE(opSyncLagBlocksBounds)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    {
        NodeConfig cfg(keyFactory);
        BOOST_REQUIRE_NO_THROW(cfg.loadConfigFromString("[ethereum]\nmode=opstack-el\n"));
        BOOST_CHECK_EQUAL(cfg.opSyncLagBlocks(), 64u);
    }
    for (const auto* value : {"0", "1", "10000"})
    {
        NodeConfig cfg(keyFactory);
        BOOST_REQUIRE_NO_THROW(cfg.loadConfigFromString(
            std::string("[ethereum]\nmode=opstack-el\nop_sync_lag_blocks=") + value + "\n"));
        BOOST_CHECK_EQUAL(cfg.opSyncLagBlocks(), std::stoull(value));
    }
    for (const auto* value : {"10001", "999999"})
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(
            cfg.loadConfigFromString(
                std::string("[ethereum]\nmode=opstack-el\nop_sync_lag_blocks=") + value + "\n"),
            InvalidConfig, [](auto const& e) {
                return errinfoContains(e, "op_sync_lag_blocks must be in [0, 10000]");
            });
    }
}

// Reload is a supported shape: a second genesis without the declaration must clear the
// flag, or a stale opstack-el declaration would leak into the next chain's genesis pin.
BOOST_AUTO_TEST_CASE(reloadWithoutDeclarationClears)
{
    auto cfg = loadGenesis(opStackELGenesis());
    BOOST_CHECK(cfg.genesisConfig().m_opStackELMode);

    BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(opGenesis("11155420", true, "")));
    BOOST_CHECK(!cfg.genesisConfig().m_opStackELMode);
    auto data = bcos::tool::generateGenesisData(cfg.genesisConfig(), *cfg.ledgerConfig());
    BOOST_CHECK(data.find("mode:opstack-el") == std::string::npos);
    // The OP schedule pin stays — it is not part of the EL declaration.
    BOOST_CHECK(data.find("[opForkTimestamps]") != std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
