/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

// The OP-Stack devp2p follow driver ([debug] follow_devp2p, formerly [ethereum]
// mode=opstack-el) on its genesis side: the OP lane (executor.version >= 3) with its fork
// schedule ([op_fork_timestamps]), the Ethereum-lane genesis shape ([alloc.*] +
// [eth_genesis_header]) and a non-zero [chain] chain_id, none of which carries a
// driver declaration any more; plus the [ethereum] knobs only that driver reads. The
// driver's lane x config.ini matrix is NodeConfigBlockDriverTest.

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
/// [chain] chain_id, optional Ethereum-lane sections and optional extra sections.
std::string opGenesis(std::string const& chainId, bool l2, std::string const& ethSection = "")
{
    return "[version]\ncompatibility_version=3.18.0\n"
           "[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=" +
           chainId +
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

/// The accepted OP genesis shape: OP lane + L2 sections.
std::string opStackELGenesis(std::string const& chainId = "11155420")
{
    return opGenesis(chainId, true);
}

NodeConfig loadGenesis(std::string const& genesis)
{
    NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(genesis));
    return cfg;
}
}  // namespace

// The accepted shape parses and carries the chain id the follow driver's fork-id handshake
// and EIP-155 checks key on; the genesis data carries no driver declaration, so a
// follow_devp2p node and an op-node-driven node share the same genesis pin.
BOOST_AUTO_TEST_CASE(opGenesisLoadsWithoutADriverDeclaration)
{
    auto cfg = loadGenesis(opStackELGenesis());
    BOOST_CHECK_EQUAL(cfg.ethereumChainId(), 11155420u);

    BOOST_REQUIRE(cfg.ledgerConfig());
    auto data = bcos::tool::generateGenesisData(cfg.genesisConfig(), *cfg.ledgerConfig());
    BOOST_CHECK(data.find("[ethereum]") == std::string::npos);
    BOOST_CHECK(
        data.find("[opForkTimestamps]\njovian_time:0\nkarst_time:0\n") != std::string::npos);
}

// Without [op_fork_timestamps] the OP lane binding fires (an OP chain cannot place its
// forks at all).
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
        l2Sections();
    NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    BOOST_CHECK_EXCEPTION(
        cfg.loadGenesisConfigFromString(genesis), InvalidConfig, [](auto const& e) {
            return errinfoContains(
                e, "executor.version >= 3 (OP lane) requires an [op_fork_timestamps] section");
        });
}

// The follow driver replays an Ethereum-shaped OP chain: without the Ethereum-lane
// genesis shape there is no genesis anchor to sync from. executor.version >= 3 implies
// the lane, so the generic lane binding rejects the missing [alloc.*] section.
BOOST_AUTO_TEST_CASE(opStackELRequiresEthLaneGenesisShape)
{
    NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(opGenesis("11155420", false)),
        InvalidConfig,
        [](auto const& e) { return errinfoContains(e, "requires a non-empty [alloc.*] section"); });
}

// Same rule as the L1 EL lane: no silent mainnet fallback for the chain id the fork-id
// handshake and EIP-1559 validation key on.
BOOST_AUTO_TEST_CASE(opStackELRequiresChainId)
{
    // The OP lane's [chain] chain_id parse (ethLaneChainId) refuses each bad spelling.
    {
        NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
        BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(opStackELGenesis("")), InvalidConfig,
            [](auto const& e) { return errinfoContains(e, "[chain].chain_id must be decimal"); });
    }
    for (const auto* chainId : {"0", "00"})
    {
        NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
        BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(opStackELGenesis(chainId)),
            InvalidConfig,
            [](auto const& e) { return errinfoContains(e, "[chain].chain_id must be non-zero"); });
    }
    NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    BOOST_CHECK_EXCEPTION(
        cfg.loadGenesisConfigFromString(opStackELGenesis("99999999999999999999999")), InvalidConfig,
        [](auto const& e) { return errinfoContains(e, "[chain].chain_id must fit uint64"); });
}

// [ethereum] mode is not read: config.ini mode=el on an OP genesis leaves the node on the OP
// Engine driver, not the L1 EL mode.
BOOST_AUTO_TEST_CASE(elModeIniOnOpGenesisNotRead)
{
    auto cfg = loadGenesis(opStackELGenesis());
    BOOST_REQUIRE_NO_THROW(cfg.loadConfigFromString("[ethereum]\nmode=el\n"));
    BOOST_REQUIRE_NO_THROW(cfg.validateELModeInvariants());
    BOOST_CHECK(cfg.blockDriver() == BlockDriver::Engine);
    BOOST_CHECK(!cfg.ethereumELModeEnabled());
}

// [ethereum] op_block_time_seconds: the OP chain's block cadence, default 2 (every
// superchain chain), bounded to [1, 60] like every neighbouring knob.
BOOST_AUTO_TEST_CASE(opBlockTimeSecondsBounds)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    {
        NodeConfig cfg(keyFactory);
        BOOST_REQUIRE_NO_THROW(cfg.loadConfigFromString("[debug]\nfollow_devp2p=true\n"));
        BOOST_CHECK_EQUAL(cfg.opBlockTimeSeconds(), 2u);
    }
    for (const auto* value : {"1", "60"})
    {
        NodeConfig cfg(keyFactory);
        BOOST_REQUIRE_NO_THROW(cfg.loadConfigFromString(
            std::string("[debug]\nfollow_devp2p=true\n[ethereum]\nop_block_time_seconds=") + value +
            "\n"));
        BOOST_CHECK_EQUAL(cfg.opBlockTimeSeconds(), std::stoull(value));
    }
    for (const auto* value : {"0", "61"})
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(
            cfg.loadConfigFromString(
                std::string("[debug]\nfollow_devp2p=true\n[ethereum]\nop_block_time_seconds=") +
                value + "\n"),
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
        BOOST_REQUIRE_NO_THROW(cfg.loadConfigFromString("[debug]\nfollow_devp2p=true\n"));
        BOOST_CHECK_EQUAL(cfg.opSyncLagBlocks(), 64u);
    }
    for (const auto* value : {"0", "1", "10000"})
    {
        NodeConfig cfg(keyFactory);
        BOOST_REQUIRE_NO_THROW(cfg.loadConfigFromString(
            std::string("[debug]\nfollow_devp2p=true\n[ethereum]\nop_sync_lag_blocks=") + value +
            "\n"));
        BOOST_CHECK_EQUAL(cfg.opSyncLagBlocks(), std::stoull(value));
    }
    for (const auto* value : {"10001", "999999"})
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(
            cfg.loadConfigFromString(
                std::string("[debug]\nfollow_devp2p=true\n[ethereum]\nop_sync_lag_blocks=") +
                value + "\n"),
            InvalidConfig, [](auto const& e) {
                return errinfoContains(e, "op_sync_lag_blocks must be in [0, 10000]");
            });
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
