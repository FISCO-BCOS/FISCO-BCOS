/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-tool/NodeConfig.h>
#include <boost/test/unit_test.hpp>
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
        BOOST_CHECK_THROW(cfg.loadGenesisConfigFromString(genesis), InvalidConfig);
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
        BOOST_CHECK_THROW(
            cfg.loadGenesisConfigFromString(base + "3.17.0" + mid), InvalidConfig);
    }
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_THROW(
            cfg.loadGenesisConfigFromString(base + "3.15.0" + mid), InvalidConfig);
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
        BOOST_CHECK_THROW(cfg.loadGenesisConfigFromString(genesis), InvalidConfig);
    }

    // Unknown revision name -> rejected.
    {
        NodeConfig cfg(keyFactory);
        std::string genesis = base + "evm_revision_forks=0:notafork\n";
        BOOST_CHECK_THROW(cfg.loadGenesisConfigFromString(genesis), InvalidConfig);
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
        BOOST_CHECK_THROW(
            cfg.loadGenesisConfigFromString(base + "evm_revision=experimental\n"), InvalidConfig);
    }
    // Fork transition to experimental -> rejected.
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_THROW(cfg.loadGenesisConfigFromString(
                              base + "evm_revision_forks=0:experimental\n"),
            InvalidConfig);
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

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
