/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */

/**
 * @brief : initializer for DB
 * @file: DBInitializer.cpp
 * @author: yujiechen
 * @date: 2018-10-24
 */
#include "DBInitializer.h"
#include "LedgerParam.h"
#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include <libconfig/GlobalConfigure.h>
#include <libdevcore/Common.h>
#include <libmptstate/MPTStateFactory.h>
#include <libsecurity/EncryptedLevelDB.h>
#include <libstorage/CachedStorage.h>
#include <libstorage/LevelDBStorage.h>
#include <libstorage/LevelDBStorage2.h>
#include <libstorage/MemoryTableFactoryFactory.h>
#include <libstorage/MemoryTableFactoryFactory2.h>
#include <libstorage/RocksDBStorage.h>
#include <libstorage/SQLStorage.h>
#include <libstorage/ZdbStorage.h>
#include <libstoragestate/StorageStateFactory.h>

using namespace dev;
using namespace dev::storage;
using namespace dev::blockverifier;
using namespace dev::db;
using namespace dev::eth;
using namespace dev::mptstate;
using namespace dev::executive;
using namespace dev::storagestate;

namespace dev
{
namespace ledger
{
void DBInitializer::initStorageDB()
{
    DBInitializer_LOG(DEBUG) << LOG_BADGE("initStorageDB");

    if (!dev::stringCmpIgnoreCase(m_param->mutableStorageParam().type, "External"))
    {
        initSQLStorage();
    }
    else if (!dev::stringCmpIgnoreCase(m_param->mutableStorageParam().type, "LevelDB"))
    {
        initLevelDBStorage();
    }
    else if (!dev::stringCmpIgnoreCase(m_param->mutableStorageParam().type, "MySQL"))
    {
        initZdbStorage();
    }
#if 0
    else if (!dev::stringCmpIgnoreCase(m_param->mutableStorageParam().type, "LevelDB2"))
    {
        initLevelDBStorage2();
    }
#endif
    else if (!dev::stringCmpIgnoreCase(m_param->mutableStorageParam().type, "RocksDB"))
    {
        initRocksDBStorage();
    }
    else
    {
        DBInitializer_LOG(ERROR) << LOG_DESC("Unsupported dbType")
                                 << LOG_KV("config storage", m_param->mutableStorageParam().type);
        BOOST_THROW_EXCEPTION(StorageError() << errinfo_comment("initStorage failed"));
    }
}

/// init the storage with leveldb
void DBInitializer::initLevelDBStorage()
{
    DBInitializer_LOG(INFO) << LOG_BADGE("initLevelDBStorage");
    /// open and init the levelDB
    leveldb::Options ldb_option;
    dev::db::BasicLevelDB* pleveldb = nullptr;
    try
    {
        boost::filesystem::create_directories(m_param->mutableStorageParam().path);
        ldb_option.create_if_missing = true;
        ldb_option.max_open_files = 1000;
        ldb_option.compression = leveldb::kSnappyCompression;
        leveldb::Status status;

        if (g_BCOSConfig.diskEncryption.enable)
        {
            // Use disk encryption
            DBInitializer_LOG(DEBUG) << LOG_DESC("open encrypted leveldb handler");
            status = EncryptedLevelDB::Open(ldb_option, m_param->mutableStorageParam().path,
                &(pleveldb), g_BCOSConfig.diskEncryption.cipherDataKey);
        }
        else
        {
            // Not to use disk encryption
            DBInitializer_LOG(DEBUG) << LOG_DESC("open leveldb handler");
            status =
                BasicLevelDB::Open(ldb_option, m_param->mutableStorageParam().path, &(pleveldb));
        }

        if (!status.ok())
        {
            BOOST_THROW_EXCEPTION(OpenLevelDBFailed() << errinfo_comment(status.ToString()));
        }
        DBInitializer_LOG(DEBUG) << LOG_BADGE("initLevelDBStorage")
                                 << LOG_KV("status", status.ok());
        std::shared_ptr<LevelDBStorage> leveldbStorage = std::make_shared<LevelDBStorage>();
        std::shared_ptr<dev::db::BasicLevelDB> leveldb_handler =
            std::shared_ptr<dev::db::BasicLevelDB>(pleveldb);
        leveldbStorage->setDB(leveldb_handler);
        m_storage = leveldbStorage;

        auto tableFactoryFactory = std::make_shared<dev::storage::MemoryTableFactoryFactory>();
        tableFactoryFactory->setStorage(m_storage);

        m_tableFactoryFactory = tableFactoryFactory;
    }
    catch (std::exception& e)
    {
        DBInitializer_LOG(ERROR) << LOG_DESC("initLevelDBStorage failed")
                                 << LOG_KV("EINFO", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(OpenLevelDBFailed() << errinfo_comment("initLevelDBStorage failed"));
    }
}

void DBInitializer::initTableFactory2(Storage::Ptr _backend)
{
    auto cachedStorage = std::make_shared<CachedStorage>();
    cachedStorage->setBackend(_backend);
    cachedStorage->setMaxStoreKey(m_param->mutableStorageParam().maxStoreKey);
    cachedStorage->setMaxForwardBlock(m_param->mutableStorageParam().maxForwardBlock);

    cachedStorage->init();

    auto tableFactoryFactory = std::make_shared<dev::storage::MemoryTableFactoryFactory2>();
    tableFactoryFactory->setStorage(cachedStorage);

    m_storage = cachedStorage;
    m_tableFactoryFactory = tableFactoryFactory;
}

void DBInitializer::initSQLStorage()
{
    DBInitializer_LOG(INFO) << LOG_BADGE("initSQLStorage");

    auto sqlStorage = std::make_shared<SQLStorage>();
    sqlStorage->setChannelRPCServer(m_channelRPCServer);
    sqlStorage->setTopic(m_param->mutableStorageParam().topic);
    sqlStorage->setFatalHandler([](std::exception& e) {
        (void)e;
        LOG(FATAL) << "Access amdb failed, exit";
        exit(1);
    });
    sqlStorage->setMaxRetry(m_param->mutableStorageParam().maxRetry);
    initTableFactory2(sqlStorage);
}

void DBInitializer::initLevelDBStorage2()
{
    DBInitializer_LOG(INFO) << LOG_BADGE("initLevelDBStorage2");
    /// open and init the levelDB
    leveldb::Options ldb_option;
    dev::db::BasicLevelDB* pleveldb = nullptr;
    try
    {
        m_param->mutableStorageParam().path = m_param->mutableStorageParam().path + "/LevelDB2";
        boost::filesystem::create_directories(m_param->mutableStorageParam().path);
        ldb_option.create_if_missing = true;
        ldb_option.max_open_files = 1000;
        ldb_option.compression = leveldb::kSnappyCompression;
        leveldb::Status status;

        // Not to use disk encryption
        DBInitializer_LOG(DEBUG) << LOG_DESC("open leveldb handler");
        status = BasicLevelDB::Open(ldb_option, m_param->mutableStorageParam().path, &(pleveldb));

        if (!status.ok())
        {
            throw std::runtime_error("open LevelDB failed");
        }
        DBInitializer_LOG(DEBUG) << LOG_BADGE("initLevelDBStorage")
                                 << LOG_KV("status", status.ok());
        std::shared_ptr<LevelDBStorage2> leveldbStorage = std::make_shared<LevelDBStorage2>();
        std::shared_ptr<dev::db::BasicLevelDB> leveldb_handler =
            std::shared_ptr<dev::db::BasicLevelDB>(pleveldb);
        leveldbStorage->setDB(leveldb_handler);
        initTableFactory2(leveldbStorage);
    }
    catch (std::exception& e)
    {
        DBInitializer_LOG(ERROR) << LOG_DESC("initLevelDBStorage2 failed")
                                 << LOG_KV("EINFO", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(OpenLevelDBFailed() << errinfo_comment("initLevelDBStorage failed"));
    }
}

void DBInitializer::initRocksDBStorage()
{
    DBInitializer_LOG(INFO) << LOG_BADGE("initRocksDBStorage");
    /// open and init the levelDB
    rocksdb::Options options;
    rocksdb::DB* db = nullptr;
    try
    {
        m_param->mutableStorageParam().path = m_param->mutableStorageParam().path + "/RocksDB";
        boost::filesystem::create_directories(m_param->mutableStorageParam().path);
        options.IncreaseParallelism();
        options.OptimizeLevelStyleCompaction();
        options.create_if_missing = true;
        options.max_open_files = 1000;
        options.compression = rocksdb::kSnappyCompression;
        rocksdb::Status status;

        // Not to use disk encryption
        DBInitializer_LOG(DEBUG) << LOG_DESC("open rocks handler");
        status = rocksdb::DB::Open(options, m_param->mutableStorageParam().path, &db);

        if (!status.ok())
        {
            throw std::runtime_error("open RocksDB failed");
        }
        DBInitializer_LOG(DEBUG) << LOG_KV("status", status.ok());
        std::shared_ptr<RocksDBStorage> rocksdbStorage = std::make_shared<RocksDBStorage>();
        std::shared_ptr<rocksdb::DB> rocksDB;
        rocksDB.reset(db);

        rocksdbStorage->setDB(rocksDB);
        initTableFactory2(rocksdbStorage);
    }
    catch (std::exception& e)
    {
        DBInitializer_LOG(ERROR) << LOG_DESC("initRocksDBStorage failed")
                                 << LOG_KV("EINFO", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(OpenLevelDBFailed() << errinfo_comment("initRocksDBStorage failed"));
    }
}

void DBInitializer::initZdbStorage()
{
    DBInitializer_LOG(INFO) << LOG_BADGE("initStorageDB") << LOG_BADGE("initZdbStorage");
    auto zdbStorage = std::make_shared<ZdbStorage>();
    ZDBConfig zdbConfig{m_param->mutableStorageParam().dbType, m_param->mutableStorageParam().dbIP,
        m_param->mutableStorageParam().dbPort, m_param->mutableStorageParam().dbUsername,
        m_param->mutableStorageParam().dbPasswd, m_param->mutableStorageParam().dbName,
        m_param->mutableStorageParam().dbCharset, m_param->mutableStorageParam().initConnections,
        m_param->mutableStorageParam().maxConnections};
    zdbStorage->initSqlAccess(zdbConfig);
    initTableFactory2(zdbStorage);
}

/// create ExecutiveContextFactory
void DBInitializer::createExecutiveContext()
{
    if (!m_storage || !m_stateFactory)
    {
        DBInitializer_LOG(ERROR) << LOG_DESC(
            "createExecutiveContext Failed for storage has not been initialized");
        return;
    }
    DBInitializer_LOG(DEBUG) << LOG_DESC("createExecutiveContext...");
    m_executiveContextFactory = std::make_shared<ExecutiveContextFactory>();
    /// storage
    m_executiveContextFactory->setStateStorage(m_storage);
    // mpt or storage
    m_executiveContextFactory->setStateFactory(m_stateFactory);
    m_executiveContextFactory->setTableFactoryFactory(m_tableFactoryFactory);
    DBInitializer_LOG(DEBUG) << LOG_DESC("createExecutiveContext SUCC");
}

/// create stateFactory
void DBInitializer::createStateFactory(dev::h256 const& genesisHash)
{
    DBInitializer_LOG(DEBUG) << LOG_BADGE("createStateFactory")
                             << LOG_KV("type", m_param->mutableStateParam().type);
    if (dev::stringCmpIgnoreCase(m_param->mutableStateParam().type, "mpt") == 0)
        createMptState(genesisHash);
    else if (dev::stringCmpIgnoreCase(m_param->mutableStateParam().type, "storage") ==
             0)  /// default is storage state
        createStorageState();
    else
    {
        DBInitializer_LOG(WARNING)
            << LOG_BADGE("createStateFactory")
            << LOG_DESC("only support storage and mpt now, create storage by default");
        createStorageState();
    }
    DBInitializer_LOG(DEBUG) << LOG_BADGE("createStateFactory SUCC");
}

/// TOCHECK: create the stateStorage with AMDB
void DBInitializer::createStorageState()
{
    m_stateFactory = std::make_shared<StorageStateFactory>(u256(0x0));
    DBInitializer_LOG(DEBUG) << LOG_DESC("createStorageState SUCC");
}

/// create the mptState
void DBInitializer::createMptState(dev::h256 const& genesisHash)
{
    m_stateFactory = std::make_shared<MPTStateFactory>(
        u256(0x0), m_param->baseDir(), genesisHash, WithExisting::Trust);
    DBInitializer_LOG(DEBUG) << LOG_DESC("createMptState SUCC");
}

}  // namespace ledger
}  // namespace dev
