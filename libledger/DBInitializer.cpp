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
#include <libconfig/GlobalConfigure.h>
#include <libdevcore/Common.h>
#include <libmptstate/MPTStateFactory.h>
#include <libsecurity/EncryptedLevelDB.h>
#include <libstorage/CachedStorage.h>
#include <libstorage/LevelDBStorage.h>
#include <libstorage/LevelDBStorage2.h>
#include <libstorage/MemoryTableFactoryFactory.h>
#include <libstorage/MemoryTableFactoryFactory2.h>
#include <libstorage/SQLStorage.h>
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
    // else if (!dev::stringCmpIgnoreCase(m_param->mutableStorageParam().type, "LevelDB2"))
    // {
    //     initLevelDBStorage2();
    // }
    else
    {
        DBInitializer_LOG(ERROR) << LOG_DESC(
            "Unsupported dbType, current version only supports levelDB");
        BOOST_THROW_EXCEPTION(StorageError() << errinfo_comment("initStorage failed"));
    }
}

/// init the storage with leveldb
void DBInitializer::initLevelDBStorage()
{
    DBInitializer_LOG(INFO) << LOG_BADGE("initStorageDB") << LOG_BADGE("initLevelDBStorage");
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
            DBInitializer_LOG(DEBUG)
                << LOG_BADGE("initStorageDB") << LOG_BADGE("initLevelDBStorage")
                << LOG_DESC("open encrypted leveldb handler");
            status = EncryptedLevelDB::Open(ldb_option, m_param->mutableStorageParam().path,
                &(pleveldb), g_BCOSConfig.diskEncryption.cipherDataKey);
        }
        else
        {
            // Not to use disk encryption
            DBInitializer_LOG(DEBUG)
                << LOG_BADGE("initStorageDB") << LOG_BADGE("initLevelDBStorage")
                << LOG_DESC("open leveldb handler");
            status =
                BasicLevelDB::Open(ldb_option, m_param->mutableStorageParam().path, &(pleveldb));
        }

        if (!status.ok())
        {
            DBInitializer_LOG(ERROR)
                << LOG_BADGE("initStorageDB") << LOG_DESC("openLevelDBStorage failed");
            throw std::runtime_error("open LevelDB failed");
        }
        DBInitializer_LOG(DEBUG) << LOG_BADGE("initStorageDB") << LOG_BADGE("initLevelDBStorage")
                                 << LOG_KV("status", status.ok());
        std::shared_ptr<LevelDBStorage> leveldb_storage = std::make_shared<LevelDBStorage>();
        std::shared_ptr<dev::db::BasicLevelDB> leveldb_handler =
            std::shared_ptr<dev::db::BasicLevelDB>(pleveldb);
        leveldb_storage->setDB(leveldb_handler);
        m_storage = leveldb_storage;

        auto tableFactoryFactory = std::make_shared<dev::storage::MemoryTableFactoryFactory>();
        tableFactoryFactory->setStorage(m_storage);

        m_tableFactoryFactory = tableFactoryFactory;
    }
    catch (std::exception& e)
    {
        DBInitializer_LOG(ERROR) << LOG_BADGE("initLevelDBStorage")
                                 << LOG_DESC("initLevelDBStorage failed")
                                 << LOG_KV("EINFO", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(OpenLevelDBFailed() << errinfo_comment("initLevelDBStorage failed"));
    }
}

void DBInitializer::initSQLStorage()
{
    DBInitializer_LOG(INFO) << LOG_BADGE("initStorageDB") << LOG_BADGE("initSQLStorage");

    auto sqlStorage = std::make_shared<SQLStorage>();
    sqlStorage->setChannelRPCServer(m_channelRPCServer);
    sqlStorage->setTopic(m_param->mutableStorageParam().topic);
    sqlStorage->setFatalHandler([](std::exception& e) {
        (void)e;
        LOG(FATAL) << "Access amdb failed, exit";
        exit(1);
    });
    sqlStorage->setMaxRetry(m_param->mutableStorageParam().maxRetry);

    auto cachedStorage = std::make_shared<CachedStorage>();
    cachedStorage->setBackend(sqlStorage);
    cachedStorage->setMaxStoreKey(m_param->mutableStorageParam().maxStoreKey);
    cachedStorage->setMaxForwardBlock(m_param->mutableStorageParam().maxForwardBlock);

    cachedStorage->init();

    auto tableFactoryFactory = std::make_shared<dev::storage::MemoryTableFactoryFactory2>();
    tableFactoryFactory->setStorage(cachedStorage);

    m_storage = cachedStorage;
    m_tableFactoryFactory = tableFactoryFactory;
}

void DBInitializer::initLevelDBStorage2()
{
    DBInitializer_LOG(INFO) << LOG_BADGE("initStorageDB") << LOG_BADGE("initLevelDBStorage");
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

        // Not to use disk encryption
        DBInitializer_LOG(DEBUG) << LOG_BADGE("initStorageDB") << LOG_BADGE("initLevelDBStorage")
                                 << LOG_DESC("open leveldb handler");
        status = BasicLevelDB::Open(ldb_option, m_param->mutableStorageParam().path, &(pleveldb));

        if (!status.ok())
        {
            DBInitializer_LOG(ERROR)
                << LOG_BADGE("initStorageDB") << LOG_DESC("openLevelDBStorage failed");
            throw std::runtime_error("open LevelDB failed");
        }
        DBInitializer_LOG(DEBUG) << LOG_BADGE("initStorageDB") << LOG_BADGE("initLevelDBStorage")
                                 << LOG_KV("status", status.ok());
        std::shared_ptr<LevelDBStorage2> leveldb_storage = std::make_shared<LevelDBStorage2>();
        std::shared_ptr<dev::db::BasicLevelDB> leveldb_handler =
            std::shared_ptr<dev::db::BasicLevelDB>(pleveldb);
        leveldb_storage->setDB(leveldb_handler);

        auto cachedStorage = std::make_shared<CachedStorage>();
        cachedStorage->setBackend(leveldb_storage);
        cachedStorage->setMaxStoreKey(m_param->mutableStorageParam().maxStoreKey);
        cachedStorage->setMaxForwardBlock(m_param->mutableStorageParam().maxForwardBlock);
        m_storage = cachedStorage;

        auto tableFactoryFactory = std::make_shared<dev::storage::MemoryTableFactoryFactory2>();
        tableFactoryFactory->setStorage(cachedStorage);

        m_tableFactoryFactory = tableFactoryFactory;
    }
    catch (std::exception& e)
    {
        DBInitializer_LOG(ERROR) << LOG_BADGE("initLevelDBStorage")
                                 << LOG_DESC("initLevelDBStorage failed")
                                 << LOG_KV("EINFO", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(OpenLevelDBFailed() << errinfo_comment("initLevelDBStorage failed"));
    }
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
    DBInitializer_LOG(DEBUG) << LOG_BADGE("createStateFactory")
                             << LOG_DESC("createStorageState SUCC");
}

/// create the mptState
void DBInitializer::createMptState(dev::h256 const& genesisHash)
{
    DBInitializer_LOG(DEBUG) << LOG_BADGE("createStateFactory") << LOG_BADGE("createMptState");
    m_stateFactory = std::make_shared<MPTStateFactory>(
        u256(0x0), m_param->baseDir(), genesisHash, WithExisting::Trust);
    DBInitializer_LOG(DEBUG) << LOG_BADGE("createStateFactory") << LOG_DESC("createMptState SUCC");
}

}  // namespace ledger
}  // namespace dev
