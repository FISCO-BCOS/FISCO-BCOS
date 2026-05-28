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
#include <bcos-tool/Exceptions.h>
#include <bcos-tool/NodeConfig.h>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/test/unit_test.hpp>
#include <sstream>

using namespace bcos;
using namespace bcos::tool;

namespace bcos::test
{
namespace
{
// The per-section NodeConfig::loadXxx(ptree) loaders are protected; subclass to
// drive them in isolation, feeding each a hand-built ptree. This exercises the
// pure config-parsing branches (defaults, populated values, validation throws)
// without going through the all-or-nothing public loadConfig().
struct LoaderProbe : public NodeConfig
{
    using NodeConfig::loadCertConfig;
    using NodeConfig::loadChainConfig;
    using NodeConfig::loadConsensusConfig;
    using NodeConfig::loadExecutorConfig;
    using NodeConfig::loadExecutorNormalConfig;
    using NodeConfig::loadFailOverConfig;
    using NodeConfig::loadGatewayConfig;
    using NodeConfig::loadLedgerConfig;
    using NodeConfig::loadOthersConfig;
    using NodeConfig::loadRpcConfig;
    using NodeConfig::loadSealerConfig;
    using NodeConfig::loadSecurityConfig;
    using NodeConfig::loadStorageConfig;
    using NodeConfig::loadStorageSecurityConfig;
    using NodeConfig::loadSyncConfig;
    using NodeConfig::loadTxPoolConfig;
    using NodeConfig::loadWeb3ChainConfig;
    using NodeConfig::loadWeb3RpcConfig;
    using NodeConfig::NodeConfig;
};

boost::property_tree::ptree fromIni(std::string const& ini)
{
    boost::property_tree::ptree pt;
    std::stringstream ss(ini);
    boost::property_tree::read_ini(ss, pt);
    return pt;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(NodeConfigLoadersTest)

BOOST_AUTO_TEST_CASE(rpcConfigDefaultsAndPopulated)
{
    LoaderProbe a;
    a.loadRpcConfig({});  // all defaults
    BOOST_CHECK_EQUAL(a.rpcListenIP(), "0.0.0.0");

    LoaderProbe b;
    auto pt = fromIni(
        "[rpc]\nlisten_ip=1.2.3.4\nlisten_port=12345\nthread_count=4\nsm_ssl=true\n"
        "enable_ssl=true\nfilter_timeout=10\nreturn_input_params=false\n");
    b.loadRpcConfig(pt);
    BOOST_CHECK_EQUAL(b.rpcListenIP(), "1.2.3.4");
    BOOST_CHECK_EQUAL(b.rpcListenPort(), 12345);
    BOOST_CHECK(!b.rpcDisableSsl());  // enable_ssl=true → disableSsl=false
}

BOOST_AUTO_TEST_CASE(web3RpcConfigDefaultsAndPopulated)
{
    LoaderProbe a;
    a.loadWeb3RpcConfig({});
    BOOST_CHECK(!a.enableWeb3Rpc());

    LoaderProbe b;
    auto pt = fromIni(
        "[web3_rpc]\nenable=true\nlisten_port=8545\nthread_count=2\nenable_cors=false\n"
        "cors_allowed_origins=http://x\nsync_transaction=true\n");
    b.loadWeb3RpcConfig(pt);
    BOOST_CHECK(b.enableWeb3Rpc());
}

BOOST_AUTO_TEST_CASE(gatewayConfigDefaultsAndPopulated)
{
    LoaderProbe a;
    a.loadGatewayConfig({});
    LoaderProbe b;
    auto pt =
        fromIni("[p2p]\nlisten_ip=0.0.0.0\nlisten_port=30300\nsm_ssl=true\nnodes_file=n.json\n");
    BOOST_CHECK_NO_THROW(b.loadGatewayConfig(pt));
}

BOOST_AUTO_TEST_CASE(certConfigDefaultsAndPopulated)
{
    LoaderProbe a;
    a.loadCertConfig({});  // pure string concatenation, no file IO
    LoaderProbe b;
    auto pt = fromIni("[cert]\nca_path=/tmp\nca_cert=ca.crt\nnode_cert=n.crt\nnode_key=n.key\n");
    BOOST_CHECK_NO_THROW(b.loadCertConfig(pt));
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
    LoaderProbe b;
    BOOST_CHECK_THROW(
        b.loadWeb3ChainConfig(fromIni("[web3]\nchain_id=notnum\n")), bcos::tool::InvalidConfig);
}

BOOST_AUTO_TEST_CASE(securityConfigLegacyAndKmsError)
{
    LoaderProbe a;
    a.loadSecurityConfig(fromIni("[security]\nprivate_key_path=node.pem\nkms_type=LEGACY\n"));
    BOOST_CHECK_EQUAL(a.privateKeyPath(), "node.pem");

    LoaderProbe b;  // bad kms_type → throws
    BOOST_CHECK_THROW(
        b.loadSecurityConfig(fromIni("[security]\nkms_type=BOGUS\n")), bcos::tool::InvalidConfig);

    LoaderProbe c;  // storage_security.enable promotes LEGACY→BCOSKMS, needs url+key
    BOOST_CHECK_THROW(c.loadSecurityConfig(fromIni("[storage_security]\nenable=true\n")),
        bcos::tool::InvalidConfig);
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

BOOST_AUTO_TEST_CASE(storageSecurityConfigDisabledAndError)
{
    LoaderProbe a;  // disabled → early return
    BOOST_CHECK_NO_THROW(
        a.loadStorageSecurityConfig(fromIni("[storage_security]\nenable=false\n")));
    LoaderProbe b;  // enabled legacy without key_center_url → throws
    BOOST_CHECK_THROW(
        b.loadStorageSecurityConfig(fromIni("[storage_security]\nenable=true\nkms_type=LEGACY\n")),
        bcos::tool::InvalidConfig);
}

BOOST_AUTO_TEST_CASE(syncConfigValidAndInvalid)
{
    LoaderProbe a;
    a.loadSyncConfig(fromIni("[sync]\nsync_block_by_tree=true\ntree_width=5\n"));
    LoaderProbe b;
    BOOST_CHECK_THROW(
        b.loadSyncConfig(fromIni("[sync]\ntree_width=0\n")), bcos::tool::InvalidConfig);
}

BOOST_AUTO_TEST_CASE(storageConfigDefaultsAndTikv)
{
    LoaderProbe a;
    a.loadStorageConfig({});  // pure defaults — covers the bulk of the loader
    BOOST_CHECK_EQUAL(a.storageType(), "RocksDB");

    LoaderProbe b;  // TiKV branch disables separate block/state
    b.loadStorageConfig(fromIni("[storage]\ntype=TiKV\nenable_separate_block_state=true\n"));
    BOOST_CHECK(!b.enableSeparateBlockAndState());
}

BOOST_AUTO_TEST_CASE(failOverConfigDisabledAndError)
{
    LoaderProbe a;  // disabled → early return
    BOOST_CHECK_NO_THROW(a.loadFailOverConfig(fromIni("[failover]\nenable=false\n"), true));
    LoaderProbe b;  // enabled, enforce member id, empty → throws
    BOOST_CHECK_THROW(b.loadFailOverConfig(fromIni("[failover]\nenable=true\n"), true),
        bcos::tool::InvalidConfig);
}

BOOST_AUTO_TEST_CASE(othersConfigDefaultsAndForceSender)
{
    LoaderProbe a;
    BOOST_CHECK_NO_THROW(a.loadOthersConfig({}));
    LoaderProbe b;
    BOOST_CHECK_NO_THROW(b.loadOthersConfig(
        fromIni("[experimental]\nforce_sender=0x0000000000000000000000000000000000000001\n")));
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

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
