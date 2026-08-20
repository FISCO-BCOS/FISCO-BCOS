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
    // Defaults on a freshly-constructed NodeConfig — the struct declarations in
    // NodeConfig.h are the single source (load() falls back to them), so these
    // assertions pin them and catch any future header/load divergence.
    BOOST_CHECK_EQUAL(cfg.genesisConfig.m_smCrypto, false);
    BOOST_CHECK_GT(cfg.genesisConfig.m_compatibilityVersion, 0U);
    BOOST_CHECK_EQUAL(cfg.genesisConfig.m_chainID, "");
    BOOST_CHECK_EQUAL(cfg.genesisConfig.m_groupID, "");
    BOOST_CHECK_EQUAL(cfg.txpool.limit, 15000U);
    // notifyWorkerNum() / verifierWorkerNum() were removed with the per-module worker-pool knobs
    // (thread_pool.io_thread_count now sizes the shared pool), so there is nothing to probe here.
    BOOST_CHECK_EQUAL(cfg.txpool.checkBlockLimit, true);
    BOOST_CHECK_EQUAL(cfg.chain.blockLimit, 0U);  // default until loadChainConfig runs
    BOOST_CHECK_EQUAL(cfg.security.privateKeyPath, "node.pem");
    BOOST_CHECK_EQUAL(cfg.security.hsmLibPath, "");
    BOOST_CHECK_EQUAL(cfg.security.keyIndex, 0);
    BOOST_CHECK_EQUAL(cfg.security.password, "");
    BOOST_CHECK_EQUAL(cfg.sealer.minSealTime, 500U);
    BOOST_CHECK_EQUAL(cfg.sealer.allowFreeNode, false);
    BOOST_CHECK_EQUAL(cfg.consensus.checkPointTimeoutInterval,
        (size_t)NodeConfig::DEFAULT_MIN_CONSENSUS_TIME_MS);
    BOOST_CHECK_EQUAL(cfg.consensus.pipelineSize, (size_t)NodeConfig::DEFAULT_PIPELINE_SIZE);
    BOOST_CHECK_EQUAL(cfg.storage.dataPath, "");
    BOOST_CHECK_EQUAL(cfg.storage.stateDBPath, "");
    BOOST_CHECK_EQUAL(cfg.storage.blockDBPath, "");
    BOOST_CHECK_EQUAL(cfg.storage.type, "RocksDB");
    BOOST_CHECK_EQUAL(cfg.storage.keyPageSize, 10240U);
    BOOST_CHECK_EQUAL(cfg.storage.maxWriteBufferNumber, 4);
    BOOST_CHECK_EQUAL(cfg.storage.maxBackgroundJobs, 4);
    BOOST_CHECK_EQUAL(cfg.storage.writeBufferSize, 64U << 20);
    BOOST_CHECK_EQUAL(cfg.storage.minWriteBufferNumberToMerge, 1);
    BOOST_CHECK_EQUAL(cfg.storage.blockCacheSize, 128U << 20);
    BOOST_CHECK_EQUAL(cfg.storage.enableRocksDBBlob, false);
    BOOST_CHECK_EQUAL(cfg.storage.pdCaPath, "");
    BOOST_CHECK_EQUAL(cfg.storage.pdCertPath, "");
    BOOST_CHECK_EQUAL(cfg.storage.pdKeyPath, "");
    BOOST_CHECK_EQUAL(cfg.storage.dbName, "storage");
    BOOST_CHECK_EQUAL(cfg.storage.stateDBName, "state");
    BOOST_CHECK_EQUAL(cfg.storage.enableArchive, false);
    BOOST_CHECK_EQUAL(cfg.storage.syncArchivedBlocks, false);
    BOOST_CHECK_EQUAL(cfg.storage.enableSeparateBlockAndState, false);
    BOOST_CHECK_EQUAL(cfg.storage.archiveListenIP, "");
    BOOST_CHECK_EQUAL(cfg.storage.archiveListenPort, 0);
    BOOST_CHECK_EQUAL(cfg.genesisConfig.m_consensusType, "");
    BOOST_CHECK_EQUAL(cfg.genesisConfig.m_txGasLimit, 3000000000U);
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
    // an empty load must leave the genesis fields at their defaults
    BOOST_CHECK_EQUAL(cfg.genesisConfig.m_chainID, "");
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
    BOOST_CHECK_EQUAL(cfg.gateway.listenIP, "");
    BOOST_CHECK_EQUAL(cfg.gateway.listenPort, 0);
    BOOST_CHECK_EQUAL(cfg.gateway.smSsl, false);
    BOOST_CHECK_EQUAL(cfg.gateway.nodeDir, "./");
    BOOST_CHECK_EQUAL(cfg.gateway.nodeFileName, "nodes.json");
    BOOST_CHECK_EQUAL(cfg.executor.baselineScheduler.parallel, false);
    BOOST_CHECK_EQUAL(cfg.executor.baselineScheduler.grainSize, 0);
    BOOST_CHECK_EQUAL(cfg.tarsRPC.host, "");
    BOOST_CHECK_EQUAL(cfg.tarsRPC.port, 0);
    BOOST_CHECK_EQUAL(cfg.txpool.enableTxsFromFreeNode, false);
    BOOST_CHECK_EQUAL(cfg.txpool.preStoreBackpressureEnabled, true);
    BOOST_CHECK_EQUAL(cfg.txpool.preStoreMaxInflight, 1024U);
    BOOST_CHECK_EQUAL(cfg.others.checkTransactionSignature, true);
    BOOST_CHECK_EQUAL(cfg.others.checkParallelConflict, true);
    BOOST_CHECK_EQUAL(cfg.genesisConfig.m_executorVersion, 0);
    BOOST_CHECK_EQUAL(cfg.others.singlePointConsensus, false);
    BOOST_CHECK(cfg.others.forceSender.empty());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
