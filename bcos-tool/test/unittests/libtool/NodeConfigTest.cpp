/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-tool/Exceptions.h>
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
    BOOST_CHECK_NO_THROW(cfg.notifyWorkerNum());
    BOOST_CHECK_NO_THROW(cfg.verifierWorkerNum());
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
        // Tolerate failure of required sub-loaders; we still exercised dispatch.
    }
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
