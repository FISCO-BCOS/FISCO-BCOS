/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-tool/NodeConfig.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::tool;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(NodeConfigTest)

BOOST_AUTO_TEST_CASE(isValidPortRejectsReservedAndOutOfRange)
{
    NodeConfig cfg;
    BOOST_CHECK(!cfg.isValidPort(0));
    BOOST_CHECK(!cfg.isValidPort(1024));  // upper bound of reserved range — rejected
    BOOST_CHECK(cfg.isValidPort(1025));   // first valid
    BOOST_CHECK(cfg.isValidPort(8080));
    BOOST_CHECK(cfg.isValidPort(65535));   // last valid
    BOOST_CHECK(!cfg.isValidPort(65536));  // out of range
    BOOST_CHECK(!cfg.isValidPort(-1));
}

BOOST_AUTO_TEST_CASE(defaultsAreReadableWithoutLoad)
{
    NodeConfig cfg;
    // None of these should crash on a freshly-constructed NodeConfig.
    (void)cfg.genesisConfig.m_smCrypto;
    (void)cfg.genesisConfig.m_compatibilityVersion;
    (void)cfg.genesisConfig.m_chainID;
    (void)cfg.genesisConfig.m_groupID;
    (void)cfg.txpool.limit;
    // notifyWorkerNum() / verifierWorkerNum() were removed with the per-module worker-pool knobs
    // (thread_pool.io_thread_count now sizes the shared pool), so there is nothing to probe here.
    (void)cfg.txpool.checkBlockLimit;
    (void)cfg.chain.blockLimit;
    (void)cfg.security.privateKeyPath;
    (void)cfg.security.hsmLibPath;
    (void)cfg.security.keyIndex;
    (void)cfg.security.password;
    (void)cfg.sealer.minSealTime;
    (void)cfg.sealer.allowFreeNode;
    (void)cfg.consensus.checkPointTimeoutInterval;
    (void)cfg.consensus.pipelineSize;
    (void)cfg.storage.dataPath;
    (void)cfg.storage.stateDBPath;
    (void)cfg.storage.blockDBPath;
    (void)cfg.storage.type;
    (void)cfg.storage.keyPageSize;
    (void)cfg.storage.maxWriteBufferNumber;
    (void)cfg.storage.enableStatistics;
    (void)cfg.storage.maxBackgroundJobs;
    (void)cfg.storage.writeBufferSize;
    (void)cfg.storage.minWriteBufferNumberToMerge;
    (void)cfg.storage.blockCacheSize;
    (void)cfg.storage.enableRocksDBBlob;
    (void)cfg.storage.pdCaPath;
    (void)cfg.storage.pdCertPath;
    (void)cfg.storage.pdKeyPath;
    (void)cfg.storage.dbName;
    (void)cfg.storage.stateDBName;
    (void)cfg.storage.enableArchive;
    (void)cfg.storage.syncArchivedBlocks;
    (void)cfg.storage.enableSeparateBlockAndState;
    (void)cfg.storage.archiveListenIP;
    (void)cfg.storage.archiveListenPort;
    (void)cfg.genesisConfig.m_consensusType;
    (void)cfg.genesisConfig.m_txGasLimit;
}

BOOST_AUTO_TEST_CASE(loadConfigFromStringEmptyDoesNotLoseInvariant)
{
    // Empty input — the loader may throw (required sections missing) or accept;
    // either way the object must remain queryable afterwards.
    NodeConfig cfg;
    try
    {
        cfg.loadConfigFromString("");
    }
    catch (...)
    {}
    (void)cfg.genesisConfig.m_chainID;
}

BOOST_AUTO_TEST_CASE(loadConfigFromStringPartialDocumentDispatchesSubLoaders)
{
    NodeConfig cfg;
    // A partial config — different blocks dispatch through the public entry
    // point. Most sub-loaders ignore missing keys via .get(key, default),
    // so this should not throw for most blocks.
    std::string ini = "[txpool]\nlimit=15000\nnotify_worker_num=2\nverify_worker_num=2\n";
    try
    {
        cfg.loadConfigFromString(ini);
    }
    catch (...)
    {
        // A later required sub-loader (e.g. consensus) may throw on this partial
        // config; loadTxPoolConfig runs before it, so its effect is still visible.
    }
    // Concrete post-condition: the [txpool] block was dispatched and applied.
    // (notifyWorkerNum() was the second post-condition; the getter no longer exists.)
    BOOST_CHECK_EQUAL(cfg.txpool.limit, 15000U);
}

// The cert/key material is directly assignable (used when certs are injected
// rather than read from disk); check each round-trips.
BOOST_AUTO_TEST_CASE(certMaterialSettersRoundTrip)
{
    NodeConfig cfg;
    cfg.cert.path = "/etc/certs";
    BOOST_CHECK_EQUAL(cfg.cert.path, "/etc/certs");
    cfg.cert.caCert = "ca-pem";
    BOOST_CHECK_EQUAL(cfg.cert.caCert, "ca-pem");
    cfg.cert.nodeCert = "node-pem";
    BOOST_CHECK_EQUAL(cfg.cert.nodeCert, "node-pem");
    cfg.cert.nodeKey = "node-key";
    BOOST_CHECK_EQUAL(cfg.cert.nodeKey, "node-key");
    cfg.cert.smCaCert = "sm-ca";
    BOOST_CHECK_EQUAL(cfg.cert.smCaCert, "sm-ca");
    cfg.cert.smNodeCert = "sm-node";
    BOOST_CHECK_EQUAL(cfg.cert.smNodeCert, "sm-node");
    cfg.cert.smNodeKey = "sm-key";
    BOOST_CHECK_EQUAL(cfg.cert.smNodeKey, "sm-key");
    cfg.cert.enSmNodeCert = "en-sm-node";
    BOOST_CHECK_EQUAL(cfg.cert.enSmNodeCert, "en-sm-node");
    cfg.cert.enSmNodeKey = "en-sm-key";
    BOOST_CHECK_EQUAL(cfg.cert.enSmNodeKey, "en-sm-key");
    cfg.service.withoutTarsFramework = true;
    BOOST_CHECK(cfg.service.withoutTarsFramework);
}

// All config fields must be queryable on a default-constructed config
// (they carry the documented defaults, never throw).
BOOST_AUTO_TEST_CASE(readOnlyAccessorsQueryableOnDefault)
{
    NodeConfig cfg;
    (void)cfg.gateway.listenIP;
    (void)cfg.gateway.listenPort;
    (void)cfg.gateway.smSsl;
    (void)cfg.gateway.nodeDir;
    (void)cfg.gateway.nodeFileName;
    (void)cfg.executor.baselineScheduler;
    (void)cfg.tarsRPC;
    (void)cfg.txpool.enableTxsFromFreeNode;
    (void)cfg.txpool.preStoreBackpressureEnabled;
    (void)cfg.txpool.preStoreMaxInflight;
    (void)cfg.genesisConfig;
    (void)cfg.others.checkTransactionSignature;
    (void)cfg.others.checkParallelConflict;
    (void)cfg.genesisConfig.m_executorVersion;
    (void)cfg.others.singlePointConsensus;
    (void)cfg.others.forceSender;
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
