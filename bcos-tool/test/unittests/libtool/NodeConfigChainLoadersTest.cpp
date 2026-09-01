/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "NodeConfigLoaderProbe.h"
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include <fstream>
#include <string>

using namespace bcos;
using namespace bcos::tool;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(NodeConfigChainLoadersTest)


// loadTxPoolConfig rejects a zero limit / worker count (the runtime-wedging
// values the `<= 0` guards are meant to catch).
BOOST_AUTO_TEST_CASE(txPoolConfigRejectsZero)
{
    LoaderProbe p;
    BOOST_CHECK_THROW(p.loadTxPoolConfig(fromIni("[txpool]\nlimit=0\n")), std::exception);
    // txpool.notify_worker_num / verify_worker_num were removed along with the per-module worker
    // pools, so there is no longer a `<= 0` guard to exercise for them -- the keys are simply
    // ignored. txpool.limit is the one that still has to reject a wedging value.
    // A positive configuration is accepted.
    LoaderProbe ok;
    BOOST_CHECK_NO_THROW(ok.loadTxPoolConfig(fromIni("[txpool]\nlimit=100\n")));
}


// The genesis loader validates consensus.block_tx_count_limit and
// consensus.leader_period as strictly positive. These are read as signed
// int64_t, so both zero and negative values are rejected (unlike the size_t
// txpool fields above, where negatives wrap past the guard).
BOOST_AUTO_TEST_CASE(ledgerConfigRejectsNonPositiveConsensusCounts)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    std::string node =
        "1234567890123456789012345678901234567890123456789012345678901234"
        "1234567890123456789012345678901234567890123456789012345678901234";
    auto cfg = [&](const std::string& txCount, const std::string& leaderPeriod) {
        return fromIni("[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=" + txCount +
                       "\nleader_period=" + leaderPeriod + "\nnode.0=" + node +
                       ":1:0\n[tx]\ngas_limit=3000000000\n");
    };
    LoaderProbe a(keyFactory);
    BOOST_CHECK_THROW(a.loadLedgerConfig(cfg("0", "1")), bcos::tool::InvalidConfig);
    LoaderProbe b(keyFactory);
    BOOST_CHECK_THROW(b.loadLedgerConfig(cfg("-5", "1")), bcos::tool::InvalidConfig);
    LoaderProbe c(keyFactory);
    BOOST_CHECK_THROW(c.loadLedgerConfig(cfg("1000", "0")), bcos::tool::InvalidConfig);
    LoaderProbe d(keyFactory);
    BOOST_CHECK_THROW(d.loadLedgerConfig(cfg("1000", "-1")), bcos::tool::InvalidConfig);
}


BOOST_AUTO_TEST_CASE(txPoolConfigValidAndInvalid)
{
    LoaderProbe a;
    a.loadTxPoolConfig(
        fromIni("[txpool]\nlimit=15000\nnotify_worker_num=2\nverify_worker_num=2\n"));
    BOOST_CHECK_EQUAL(a.txpoolLimit(), 15000);

    LoaderProbe b;
    BOOST_CHECK_THROW(
        b.loadTxPoolConfig(fromIni("[txpool]\nlimit=0\n")), bcos::tool::InvalidConfig);

    LoaderProbe c;  // non-numeric → checkAndGetValue lexical_cast throws
    BOOST_CHECK_THROW(
        c.loadTxPoolConfig(fromIni("[txpool]\nlimit=abc\n")), bcos::tool::InvalidConfig);
}


BOOST_AUTO_TEST_CASE(chainConfigValidAndInvalid)
{
    LoaderProbe a;
    a.loadChainConfig(fromIni("[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=123\n"), true);
    BOOST_CHECK_EQUAL(a.chainId(), "123");

    LoaderProbe b;  // non-alnum chainId rejected
    BOOST_CHECK_THROW(
        b.loadChainConfig(fromIni("[chain]\nchain_id=bad!id\n"), true), bcos::tool::InvalidConfig);

    LoaderProbe c;  // block_limit out of range
    BOOST_CHECK_THROW(c.loadChainConfig(fromIni("[chain]\nchain_id=chain\nblock_limit=0\n"), true),
        bcos::tool::InvalidConfig);
}


BOOST_AUTO_TEST_CASE(web3ChainConfigValidAndInvalid)
{
    LoaderProbe a;
    a.loadWeb3ChainConfig(fromIni("[web3]\nchain_id=42\n"));
    BOOST_CHECK_EQUAL(a.genesisConfig().m_web3ChainID, "42");
    LoaderProbe hex;
    hex.loadWeb3ChainConfig(fromIni("[web3]\nchain_id=0x539\n"));
    BOOST_CHECK_EQUAL(hex.genesisConfig().m_web3ChainID, "0x539");
    LoaderProbe b;
    BOOST_CHECK_THROW(
        b.loadWeb3ChainConfig(fromIni("[web3]\nchain_id=notnum\n")), bcos::tool::InvalidConfig);
    LoaderProbe neg;
    BOOST_CHECK_THROW(
        neg.loadWeb3ChainConfig(fromIni("[web3]\nchain_id=-5\n")), bcos::tool::InvalidConfig);
    LoaderProbe negZero;
    BOOST_CHECK_THROW(
        negZero.loadWeb3ChainConfig(fromIni("[web3]\nchain_id=-0\n")), bcos::tool::InvalidConfig);
}


BOOST_AUTO_TEST_CASE(sealerConfigValidAndInvalid)
{
    LoaderProbe a;
    a.loadSealerConfig(fromIni("[consensus]\nmin_seal_time=500\n"));
    BOOST_CHECK_EQUAL(a.minSealTime(), 500);
    LoaderProbe b;
    BOOST_CHECK_THROW(
        b.loadSealerConfig(fromIni("[consensus]\nmin_seal_time=0\n")), bcos::tool::InvalidConfig);
}


BOOST_AUTO_TEST_CASE(consensusConfigValidAndInvalid)
{
    LoaderProbe a;
    a.loadConsensusConfig(fromIni("[consensus]\ncheckpoint_timeout=3000\npipeline_size=50\n"));
    LoaderProbe b;  // checkpoint_timeout below minimum → throws
    BOOST_CHECK_THROW(
        b.loadConsensusConfig(fromIni("[consensus]\ncheckpoint_timeout=1\npipeline_size=2\n")),
        bcos::tool::InvalidConfig);
}


BOOST_AUTO_TEST_CASE(executorConfigValidAndInvalid)
{
    // default compatibilityVersion is >= 3.3, so a non-empty auth_admin_account
    // is required or the loader throws.
    std::string admin = "0x0000000000000000000000000000000000000001";
    LoaderProbe a;
    BOOST_CHECK_NO_THROW(a.loadExecutorConfig(
        fromIni("[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
                "auth_admin_account=" +
                admin + "\n")));

    LoaderProbe b;  // empty auth_admin_account at >=3.3 → throws
    BOOST_CHECK_THROW(
        b.loadExecutorConfig(
            fromIni("[executor]\nis_wasm=false\nis_auth_check=true\nis_serial_execute=true\n")),
        bcos::tool::InvalidConfig);
    BOOST_CHECK_NO_THROW(b.loadExecutorNormalConfig(fromIni("[executor]\nenable_dag=true\n")));
}


BOOST_AUTO_TEST_CASE(ledgerConfigValidAndInvalid)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    std::string node =
        "1234567890123456789012345678901234567890123456789012345678901234"
        "1234567890123456789012345678901234567890123456789012345678901234";

    LoaderProbe a(keyFactory);
    a.loadLedgerConfig(
        fromIni("[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\n"
                "leader_period=1\nnode.0=" +
                node + ":1:0\n[tx]\ngas_limit=3000000000\n"));
    BOOST_CHECK_EQUAL(a.ledgerConfig()->blockTxCountLimit(), 1000);

    LoaderProbe b(keyFactory);  // illegal consensus type
    BOOST_CHECK_THROW(b.loadLedgerConfig(fromIni("[consensus]\nconsensus_type=foobar\n")),
        bcos::tool::InvalidConfig);

    LoaderProbe c(keyFactory);  // empty sealer list
    BOOST_CHECK_THROW(
        c.loadLedgerConfig(fromIni("[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\n"
                                   "leader_period=1\n[tx]\ngas_limit=3000000000\n")),
        bcos::tool::InvalidConfig);
}


BOOST_AUTO_TEST_CASE(genesisConfigFromStringFull)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    std::string node =
        "1234567890123456789012345678901234567890123456789012345678901234"
        "1234567890123456789012345678901234567890123456789012345678901234";
    // exercises the loadGenesisConfig orchestrator: chain + web3chain + features
    // + ledger (rpbft branch) + executor, in one public call.
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
    BOOST_CHECK_NO_THROW(cfg.loadGenesisConfigFromString(genesis));
    BOOST_CHECK_EQUAL(cfg.chainId(), "1");
}


BOOST_AUTO_TEST_CASE(gettersAfterFullGenesisLoad)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    std::string node =
        "1234567890123456789012345678901234567890123456789012345678901234"
        "1234567890123456789012345678901234567890123456789012345678901234";
    std::string genesis =
        "[version]\ncompatibility_version=3.6.0\n"
        "[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=1\n"
        "[web3]\nchain_id=1\n"
        "[consensus]\nconsensus_type=rpbft\nblock_tx_count_limit=1000\nleader_period=1\n"
        "epoch_sealer_num=4\nepoch_block_num=1000\nnode.0=" +
        node +
        ":1:1\n"
        "[tx]\ngas_limit=3000000000\n"
        "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
        "auth_admin_account=0x0000000000000000000000000000000000000001\n";
    NodeConfig cfg(keyFactory);
    cfg.loadGenesisConfigFromString(genesis);

    // genesis-derived getters (set by loadLedgerConfig/loadExecutorConfig rpbft path)
    BOOST_CHECK_EQUAL(cfg.consensusType(), "rpbft");
    BOOST_CHECK_EQUAL(cfg.epochSealerNum(), 4);
    BOOST_CHECK_EQUAL(cfg.epochBlockNum(), 1000);
    // isWasm() went away with WASM/Liquid execution support (#5348).
    BOOST_CHECK(!cfg.isAuthCheck());
    BOOST_CHECK(!cfg.isSerialExecute());
    BOOST_CHECK_GT(cfg.txGasLimit(), 0U);
    BOOST_CHECK_GT(cfg.compatibilityVersion(), 0U);
    BOOST_CHECK(!cfg.compatibilityVersionStr().empty());
    BOOST_CHECK_NO_THROW(cfg.genesisData());
    BOOST_CHECK_NO_THROW(cfg.pdAddrs());
}

// OP-Stack Jovian fork selection is feature-flag driven: feature_op_jovian in [features]
// (the FISCO-native mechanism) — replaces the former [chain].isthmus_time / jovian_time
// timestamp thresholds.
BOOST_AUTO_TEST_CASE(chainConfigOpJovianActiveByFeatureFlag)
{
    LoaderProbe p;
    p.loadGenesisFeatures(fromIni("[features]\nfeature_op_jovian=true\n"));
    BOOST_CHECK(p.opJovianActive());
}

// Absent feature_op_jovian defaults to Isthmus (feature off).
BOOST_AUTO_TEST_CASE(chainConfigOpJovianDefaultsOff)
{
    LoaderProbe p;
    p.loadGenesisFeatures(fromIni("[features]\nfeature_l2_ethereum_compat=true\n"));
    BOOST_CHECK(!p.opJovianActive());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
