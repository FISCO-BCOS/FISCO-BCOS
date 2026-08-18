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
    BOOST_CHECK_NO_THROW(cfg.smCryptoType());
    BOOST_CHECK_NO_THROW(cfg.compatibilityVersion());
    BOOST_CHECK_NO_THROW(cfg.chainId());
    BOOST_CHECK_NO_THROW(cfg.groupId());
    BOOST_CHECK_NO_THROW(cfg.txpoolLimit());
    // notifyWorkerNum() / verifierWorkerNum() were removed with the per-module worker-pool knobs
    // (thread_pool.io_thread_count now sizes the shared pool), so there is nothing to probe here.
    BOOST_CHECK_NO_THROW(cfg.checkBlockLimit());
    BOOST_CHECK_NO_THROW(cfg.blockLimit());
    BOOST_CHECK_NO_THROW(cfg.privateKeyPath());
    BOOST_CHECK_NO_THROW(cfg.hsmLibPath());
    BOOST_CHECK_NO_THROW(cfg.keyIndex());
    BOOST_CHECK_NO_THROW(cfg.encKeyIndex());
    BOOST_CHECK_NO_THROW(cfg.password());
    BOOST_CHECK_NO_THROW(cfg.minSealTime());
    BOOST_CHECK_NO_THROW(cfg.allowFreeNodeSync());
    BOOST_CHECK_NO_THROW(cfg.checkPointTimeoutInterval());
    BOOST_CHECK_NO_THROW(cfg.pipelineSize());
    BOOST_CHECK_NO_THROW(cfg.storagePath());
    BOOST_CHECK_NO_THROW(cfg.stateDBPath());
    BOOST_CHECK_NO_THROW(cfg.blockDBPath());
    BOOST_CHECK_NO_THROW(cfg.storageType());
    BOOST_CHECK_NO_THROW(cfg.keyPageSize());
    BOOST_CHECK_NO_THROW(cfg.maxWriteBufferNumber());
    BOOST_CHECK_NO_THROW(cfg.enableStatistics());
    BOOST_CHECK_NO_THROW(cfg.maxBackgroundJobs());
    BOOST_CHECK_NO_THROW(cfg.writeBufferSize());
    BOOST_CHECK_NO_THROW(cfg.minWriteBufferNumberToMerge());
    BOOST_CHECK_NO_THROW(cfg.blockCacheSize());
    BOOST_CHECK_NO_THROW(cfg.enableRocksDBBlob());
    BOOST_CHECK_NO_THROW(cfg.pdCaPath());
    BOOST_CHECK_NO_THROW(cfg.pdCertPath());
    BOOST_CHECK_NO_THROW(cfg.pdKeyPath());
    BOOST_CHECK_NO_THROW(cfg.storageDBName());
    BOOST_CHECK_NO_THROW(cfg.stateDBName());
    BOOST_CHECK_NO_THROW(cfg.enableArchive());
    BOOST_CHECK_NO_THROW(cfg.syncArchivedBlocks());
    BOOST_CHECK_NO_THROW(cfg.enableSeparateBlockAndState());
    BOOST_CHECK_NO_THROW(cfg.archiveListenIP());
    BOOST_CHECK_NO_THROW(cfg.archiveListenPort());
    BOOST_CHECK_NO_THROW(cfg.consensusType());
    BOOST_CHECK_NO_THROW(cfg.txGasLimit());
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
    BOOST_CHECK_NO_THROW(cfg.chainId());
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
    BOOST_CHECK_EQUAL(cfg.txpoolLimit(), 15000U);
}

// The cert/key material has direct setters (used when certs are injected
// rather than read from disk); check each round-trips.
BOOST_AUTO_TEST_CASE(certMaterialSettersRoundTrip)
{
    NodeConfig cfg;
    cfg.setCertPath("/etc/certs");
    BOOST_CHECK_EQUAL(cfg.certPath(), "/etc/certs");
    cfg.setCaCert("ca-pem");
    BOOST_CHECK_EQUAL(cfg.caCert(), "ca-pem");
    cfg.setNodeCert("node-pem");
    BOOST_CHECK_EQUAL(cfg.nodeCert(), "node-pem");
    cfg.setNodeKey("node-key");
    BOOST_CHECK_EQUAL(cfg.nodeKey(), "node-key");
    cfg.setSmCaCert("sm-ca");
    BOOST_CHECK_EQUAL(cfg.smCaCert(), "sm-ca");
    cfg.setSmNodeCert("sm-node");
    BOOST_CHECK_EQUAL(cfg.smNodeCert(), "sm-node");
    cfg.setSmNodeKey("sm-key");
    BOOST_CHECK_EQUAL(cfg.smNodeKey(), "sm-key");
    cfg.setEnSmNodeCert("en-sm-node");
    BOOST_CHECK_EQUAL(cfg.enSmNodeCert(), "en-sm-node");
    cfg.setEnSmNodeKey("en-sm-key");
    BOOST_CHECK_EQUAL(cfg.enSmNodeKey(), "en-sm-key");
    cfg.setWithoutTarsFramework(true);
    BOOST_CHECK(cfg.withoutTarsFramework());
}

// All read-only accessors must be queryable on a default-constructed config
// (they return the documented defaults, never throw).
BOOST_AUTO_TEST_CASE(readOnlyAccessorsQueryableOnDefault)
{
    NodeConfig cfg;
    BOOST_CHECK_NO_THROW(cfg.p2pListenIP());
    BOOST_CHECK_NO_THROW(cfg.p2pListenPort());
    BOOST_CHECK_NO_THROW(cfg.p2pSmSsl());
    BOOST_CHECK_NO_THROW(cfg.p2pNodeDir());
    BOOST_CHECK_NO_THROW(cfg.p2pNodeFileName());
    BOOST_CHECK_NO_THROW(cfg.baselineSchedulerConfig());
    BOOST_CHECK_NO_THROW(cfg.tarsRPCConfig());
    BOOST_CHECK_NO_THROW(cfg.enableTxsFromFreeNode());
    BOOST_CHECK_NO_THROW(cfg.preStoreBackpressureEnabled());
    BOOST_CHECK_NO_THROW(cfg.preStoreMaxInflight());
    BOOST_CHECK_NO_THROW(cfg.genesisConfig());
    BOOST_CHECK_NO_THROW(cfg.checkTransactionSignature());
    BOOST_CHECK_NO_THROW(cfg.checkParallelConflict());
    BOOST_CHECK_NO_THROW(cfg.executorVersion());
    BOOST_CHECK_NO_THROW(cfg.singlePointConsensus());
    BOOST_CHECK_NO_THROW(cfg.forceSender());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
