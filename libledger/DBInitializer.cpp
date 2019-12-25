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
#include "libstorage/BinLogHandler.h"
#include "libstorage/BinaryLogStorage.h"
#include "libstorage/RocksDBStorageFactory.h"
#include "libstorage/SQLConnectionPool.h"
#include "libstorage/ScalableStorage.h"
#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/table.h"
#include <libconfig/GlobalConfigure.h>
#include <libdevcore/Common.h>
#include <libdevcore/Exceptions.h>
#include <libmptstate/MPTStateFactory.h>
#include <libsecurity/EncryptedLevelDB.h>
#include <libstorage/BasicRocksDB.h>
#include <libstorage/CachedStorage.h>
#include <libstorage/LevelDBStorage.h>
#include <libstorage/MemoryTableFactoryFactory.h>
#include <libstorage/MemoryTableFactoryFactory2.h>
#include <libstorage/RocksDBStorage.h>
#include <libstorage/SQLStorage.h>
#include <libstorage/ZdbStorage.h>
#include <libstoragestate/StorageStateFactory.h>
#include <boost/lexical_cast.hpp>

using namespace std;
using namespace dev;
using namespace dev::storage;
using namespace dev::blockverifier;
using namespace dev::db;
using namespace dev::ledger;
using namespace dev::eth;
using namespace dev::mptstate;
using namespace dev::executive;
using namespace dev::storagestate;

void DBInitializer::initStorageDB()
{
    DBInitializer_LOG(INFO) << LOG_BADGE("initStorageDB");
    if (!dev::stringCmpIgnoreCase(m_param->mutableStorageParam().type, "LevelDB"))
    {
        initLevelDBStorage();
    }
    else if (!dev::stringCmpIgnoreCase(m_param->mutableStorageParam().type, "External"))
    {
        auto storage = initSQLStorage();
        initTableFactory2(storage, m_param);
    }
    else if (!dev::stringCmpIgnoreCase(m_param->mutableStorageParam().type, "MySQL"))
    {
        auto storage = initZdbStorage();
        initTableFactory2(storage, m_param);
    }
    else if (!dev::stringCmpIgnoreCase(m_param->mutableStorageParam().type, "RocksDB"))
    {
        auto storage = initRocksDBStorage(m_param);
        initTableFactory2(storage, m_param);
    }
    else if (!dev::stringCmpIgnoreCase(m_param->mutableStorageParam().type, "Scalable"))
    {
        auto storage = initScalableStorage(m_param);
        initTableFactory2(storage, m_param);
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
            DBInitializer_LOG(INFO) << LOG_DESC("open encrypted leveldb handler");
            status =
                EncryptedLevelDB::Open(ldb_option, m_param->mutableStorageParam().path, &(pleveldb),
                    g_BCOSConfig.diskEncryption.cipherDataKey, g_BCOSConfig.diskEncryption.dataKey);
        }
        else
        {
            // Not to use disk encryption
            DBInitializer_LOG(INFO) << LOG_DESC("open leveldb handler");
            status =
                BasicLevelDB::Open(ldb_option, m_param->mutableStorageParam().path, &(pleveldb));
        }

        if (!status.ok())
        {
            BOOST_THROW_EXCEPTION(OpenDBFailed() << errinfo_comment(status.ToString()));
        }
        DBInitializer_LOG(INFO) << LOG_BADGE("initLevelDBStorage") << LOG_KV("status", status.ok());
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
        BOOST_THROW_EXCEPTION(OpenDBFailed() << errinfo_comment("initLevelDBStorage failed"));
    }
}

int64_t dev::ledger::getBlockNumberFromStorage(Storage::Ptr _storage)
{
    int64_t startNum = -1;
    auto tableFactoryFactory = std::make_shared<dev::storage::MemoryTableFactoryFactory2>();
    tableFactoryFactory->setStorage(_storage);
    auto memoryTableFactory = tableFactoryFactory->newTableFactory(dev::h256(), startNum);
    Table::Ptr tb = memoryTableFactory->openTable(SYS_CURRENT_STATE, false);
    auto entries = tb->select(SYS_KEY_CURRENT_NUMBER, tb->newCondition());
    if (entries->size() > 0)
    {
        auto entry = entries->get(0);
        std::string currentNumber = entry->getField(SYS_VALUE);
        startNum = boost::lexical_cast<int64_t>(currentNumber);
    }
    return startNum;
}

void DBInitializer::recoverFromBinaryLog(
    std::shared_ptr<dev::storage::BinLogHandler> _binaryLogger, dev::storage::Storage::Ptr _storage)
{
    int64_t startNum = getBlockNumberFromStorage(_storage);
    // getMissingBlocksFromBinLog from (startNum,lastBlockNum]
    int64_t lastBlockNum = _binaryLogger->getLastBlockNum();
    DBInitializer_LOG(INFO) << LOG_DESC("recover from binary logs") << LOG_KV("startNum", startNum)
                            << LOG_KV("endNum", lastBlockNum);
    if (startNum >= lastBlockNum)
    {
        return;
    }
    int64_t interval = 100;
    for (int64_t num = startNum; num <= lastBlockNum; num += interval)
    {
        auto blocksData = _binaryLogger->getMissingBlocksFromBinLog(num, num + interval);
        if (blocksData->size() > 0)
        {
            for (size_t i = 1; i <= blocksData->size(); ++i)
            {
                auto blockDataIter = blocksData->find(num + i);
                if (blockDataIter == blocksData->end() || blockDataIter->second.empty())
                {
                    DBInitializer_LOG(FATAL) << LOG_DESC("recoverFromBinaryLog failed")
                                             << LOG_KV("blockNumber", num + i);
                }
                else
                {
                    const std::vector<TableData::Ptr>& blockData = blockDataIter->second;
                    h256 hash = h256();
                    // get the hash used by commit function
                    for (size_t j = 0; j < blockData.size(); j++)
                    {
                        TableData::Ptr data = blockData[j];
                        if (data->info->name == SYS_NUMBER_2_HASH)
                        {
                            Entries::Ptr newEntries = data->newEntries;
                            Entry::Ptr entry = newEntries->get(0);
                            hash = h256(entry->getField("value"));
                            break;
                        }
                    }
                    _storage->commit(num + i, blockData);
                    DBInitializer_LOG(INFO) << LOG_DESC("recover from binary logs succeed")
                                            << LOG_KV("blockNumber", num + i);
                }
            }
        }
    }
}

void DBInitializer::initTableFactory2(
    Storage::Ptr _backend, std::shared_ptr<LedgerParamInterface> _param)
{
    auto backendStorage = _backend;
    if (_param->mutableStorageParam().CachedStorage)
    {
        auto cachedStorage = std::make_shared<CachedStorage>(m_groupID);
        cachedStorage->setBackend(_backend);
        cachedStorage->setMaxCapacity(
            _param->mutableStorageParam().maxCapacity * 1024 * 1024);  // Bytes
        cachedStorage->setMaxForwardBlock(_param->mutableStorageParam().maxForwardBlock);
        cachedStorage->init();
        backendStorage = cachedStorage;
        DBInitializer_LOG(INFO) << LOG_BADGE("init CachedStorage")
                                << LOG_KV("maxCapacity", _param->mutableStorageParam().maxCapacity)
                                << LOG_KV("maxForwardBlock",
                                       _param->mutableStorageParam().maxForwardBlock);
    }

    auto tableFactoryFactory = std::make_shared<dev::storage::MemoryTableFactoryFactory2>();
    if (_param->mutableStorageParam().binaryLog)
    {
        auto binaryLogStorage = make_shared<BinaryLogStorage>();
        binaryLogStorage->setBackend(backendStorage);

        tableFactoryFactory->setStorage(binaryLogStorage);
        m_tableFactoryFactory = tableFactoryFactory;
        auto path = _param->baseDir() + "/BinaryLogs";
        boost::filesystem::create_directories(path);
        auto binaryLogger = make_shared<BinLogHandler>(path);
        binaryLogger->setBinaryLogSize(g_BCOSConfig.c_binaryLogSize);
        recoverFromBinaryLog(binaryLogger, backendStorage);
        binaryLogStorage->setBinaryLogger(binaryLogger);
        DBInitializer_LOG(INFO) << LOG_BADGE("init BinaryLogger") << LOG_KV("BinaryLogsPath", path);
        m_storage = binaryLogStorage;
    }
    else
    {
        tableFactoryFactory->setStorage(backendStorage);
        m_tableFactoryFactory = tableFactoryFactory;
        m_storage = backendStorage;
    }
}


dev::storage::Storage::Ptr DBInitializer::initSQLStorage()
{
    DBInitializer_LOG(INFO) << LOG_BADGE("initSQLStorage");

    unsupportedFeatures("SQLStorage(External)");
    auto sqlStorage = createSQLStorage(m_param, m_channelRPCServer, [](std::exception& e) {
        DBInitializer_LOG(ERROR) << LOG_BADGE("STORAGE") << LOG_BADGE("External")
                                 << "Access amdb failed exit:" << e.what();
        raise(SIGTERM);
        BOOST_THROW_EXCEPTION(e);
    });
    return sqlStorage;
}

std::function<void(std::string const&, std::string&)> dev::ledger::getEncryptHandler()
{
    // get dataKey according to ciperDataKey from keyCenter
    return [=](std::string const& data, std::string& encData) {
        try
        {
            encData = aesCBCEncrypt(data, g_BCOSConfig.diskEncryption.dataKey);
        }
        catch (const std::exception& e)
        {
            std::string error_info = "encryt value for data=" + data +
                                     " failed, EINFO: " + boost::diagnostic_information(e);
            ROCKSDB_LOG(ERROR) << LOG_DESC(error_info);
            BOOST_THROW_EXCEPTION(EncryptFailed() << errinfo_comment(error_info));
        }
    };
}

std::function<void(std::string&)> dev::ledger::getDecryptHandler()
{
    return [=](std::string& data) {
        try
        {
            data = aesCBCDecrypt(data, g_BCOSConfig.diskEncryption.dataKey);
        }
        catch (const std::exception& e)
        {
            std::string error_info = "decrypt value for data=" + data + " failed";
            ROCKSDB_LOG(ERROR) << LOG_DESC(error_info);
            BOOST_THROW_EXCEPTION(DecryptFailed() << errinfo_comment(error_info));
        }
    };
}

dev::storage::Storage::Ptr DBInitializer::initRocksDBStorage(
    std::shared_ptr<LedgerParamInterface> _param)
{
    DBInitializer_LOG(INFO) << LOG_BADGE("initRocksDBStorage");
    if (g_BCOSConfig.diskEncryption.enable && g_BCOSConfig.version() <= RC3_VERSION)
    {
        unsupportedFeatures("RocksDB in RC3");
    }
    try
    {
        auto rocksdbStorage = createRocksDBStorage(_param->mutableStorageParam().path,
            g_BCOSConfig.diskEncryption.enable, _param->mutableStorageParam().binaryLog,
            _param->mutableStorageParam().CachedStorage);
        return rocksdbStorage;
    }
    catch (std::exception& e)
    {
        DBInitializer_LOG(ERROR) << LOG_DESC("initRocksDBStorage failed")
                                 << LOG_KV("EINFO", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(OpenDBFailed() << errinfo_comment("initRocksDBStorage failed"));
    }
}

dev::storage::Storage::Ptr DBInitializer::initScalableStorage(
    std::shared_ptr<LedgerParamInterface> _param)
{
    DBInitializer_LOG(INFO) << LOG_BADGE("initScalableStorage");
    if (!_param->mutableStorageParam().binaryLog)
    {
        DBInitializer_LOG(ERROR) << LOG_DESC(
            "Invalid config, Scalable Storage require binary_log=true");
        BOOST_THROW_EXCEPTION(
            invalid_argument("Invalid config, Scalable Storage require binary_log=true"));
    }
    try
    {
        auto stateStorage = createRocksDBStorage(_param->mutableStorageParam().path + "/state",
            g_BCOSConfig.diskEncryption.enable, _param->mutableStorageParam().binaryLog,
            _param->mutableStorageParam().CachedStorage);
        auto scalableStorage =
            std::make_shared<ScalableStorage>(_param->mutableStorageParam().scrollThreshold);
        scalableStorage->setStateStorage(stateStorage);
        auto remoteStorage = createSQLStorage(_param, m_channelRPCServer, [](std::exception& e) {
            DBInitializer_LOG(ERROR) << LOG_BADGE("STORAGE") << LOG_BADGE("External")
                                     << "Access amdb failed exit:" << e.what();
            raise(SIGTERM);
            BOOST_THROW_EXCEPTION(e);
        });
        scalableStorage->setRemoteStorage(remoteStorage);
        std::string blocksDBPath = _param->mutableStorageParam().path + "/blocksDB";
        // if enable binary log, then disable rocksDB WAL
        // if enable cachedStorage, then rocksdb should complete dirty entries in committing process
        auto rocksDBStorageFactory = make_shared<RocksDBStorageFactory>(blocksDBPath,
            _param->mutableStorageParam().binaryLog, !_param->mutableStorageParam().CachedStorage);
        rocksDBStorageFactory->setDBOpitons(getRocksDBOptions());
        scalableStorage->setStorageFactory(rocksDBStorageFactory);
        int64_t blockNumber = getBlockNumberFromStorage(stateStorage);
        blockNumber = blockNumber > 0 ? blockNumber + 1 : 0;
        auto archiveStorage = rocksDBStorageFactory->getStorage(to_string(blockNumber));
        scalableStorage->setArchiveStorage(archiveStorage, blockNumber);
        setRemoteBlockNumber(scalableStorage, blocksDBPath);

        return scalableStorage;
    }
    catch (std::exception& e)
    {
        DBInitializer_LOG(ERROR) << LOG_DESC("initScalableStorage failed")
                                 << LOG_KV("EINFO", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(StorageError() << errinfo_comment("initScalableStorage failed"));
    }
}

void DBInitializer::setRemoteBlockNumber(
    std::shared_ptr<ScalableStorage> scalableStorage, const std::string& blocksDBPath)
{
    boost::filesystem::path targetPath(blocksDBPath);
    std::vector<std::string> filenames;
    boost::filesystem::directory_iterator endIter;
    for (boost::filesystem::directory_iterator iter(targetPath); iter != endIter; iter++)
    {
        if (boost::filesystem::is_directory(*iter))
        {
            filenames.push_back(iter->path().filename().string());
        }
    }
    try
    {
        std::sort(
            filenames.begin(), filenames.end(), [](std::string const& a, std::string const& b) {
                return std::stoll(a) >= std::stoll(b);
            });

        int64_t remoteBlockNumber = 0;
        for (size_t i = 0; i < filenames.size(); i++)
        {
            remoteBlockNumber = std::stoll(filenames[i]);
            if (i == filenames.size() - 1)
            {
                break;
            }
            if (scalableStorage->getDBNameOfArchivedBlock(std::stoll(filenames[i]) - 1) !=
                filenames[i + 1])
            {
                break;
            }
        }
        DBInitializer_LOG(INFO) << LOG_DESC("scalableStorage setRemoteBlockNumber")
                                << LOG_KV("number", remoteBlockNumber);
        scalableStorage->setRemoteBlockNumber(remoteBlockNumber);
    }
    catch (std::exception& e)
    {
        BOOST_THROW_EXCEPTION(
            dev::StorageError() << errinfo_comment(boost::diagnostic_information(e)));
    }
}

void DBInitializer::unsupportedFeatures(std::string const& desc)
{
    if (g_BCOSConfig.diskEncryption.enable)
    {
        BOOST_THROW_EXCEPTION(
            UnsupportedFeature() << errinfo_comment(desc + " doesn't support disk encryption"));
    }
}

dev::storage::Storage::Ptr DBInitializer::initZdbStorage()
{
    DBInitializer_LOG(INFO) << LOG_BADGE("initStorageDB") << LOG_BADGE("initZdbStorage");

    // exit when enable the unsupported features
    unsupportedFeatures("ZdbStorage");

    auto zdbStorage = createZdbStorage(m_param, [](std::exception& e) {
        DBInitializer_LOG(ERROR) << LOG_BADGE("STORAGE") << LOG_BADGE("MySQL")
                                 << "access mysql failed exit:" << e.what();
        raise(SIGTERM);
        BOOST_THROW_EXCEPTION(e);
    });
    return zdbStorage;
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

    DBInitializer_LOG(INFO) << LOG_DESC("createExecutiveContext...");
    m_executiveContextFactory = std::make_shared<ExecutiveContextFactory>();
    /// storage
    m_executiveContextFactory->setStateStorage(m_storage);
    // mpt or storage
    m_executiveContextFactory->setStateFactory(m_stateFactory);
    m_executiveContextFactory->setTableFactoryFactory(m_tableFactoryFactory);
    DBInitializer_LOG(INFO) << LOG_DESC("createExecutiveContext SUCC");
}

/// create stateFactory
void DBInitializer::createStateFactory(dev::h256 const& genesisHash)
{
    DBInitializer_LOG(INFO) << LOG_BADGE("createStateFactory")
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
    DBInitializer_LOG(INFO) << LOG_BADGE("createStateFactory SUCC");
}

/// TOCHECK: create the stateStorage with AMDB
void DBInitializer::createStorageState()
{
    m_stateFactory = std::make_shared<StorageStateFactory>(u256(0x0));
    DBInitializer_LOG(INFO) << LOG_DESC("createStorageState SUCC");
}

/// create the mptState
void DBInitializer::createMptState(dev::h256 const& genesisHash)
{
    m_stateFactory = std::make_shared<MPTStateFactory>(
        u256(0x0), m_param->baseDir(), genesisHash, WithExisting::Trust);
    DBInitializer_LOG(INFO) << LOG_DESC("createMptState SUCC");
}

Storage::Ptr dev::ledger::createRocksDBStorage(const std::string& _dbPath,
    bool _enableEncryption = false, bool _disableWAL = false, bool _enableCache = true)
{
    boost::filesystem::create_directories(_dbPath);

    std::shared_ptr<BasicRocksDB> rocksDB = std::make_shared<BasicRocksDB>();
    auto options = getRocksDBOptions();
    // any exception will cause the program to be stopped
    rocksDB->Open(options, _dbPath);
    if (_enableEncryption)
    {
        DBInitializer_LOG(INFO) << LOG_DESC(
            "diskEncryption enabled: set encrypt and decrypt handler for rocksDB");
        rocksDB->setEncryptHandler(getEncryptHandler());
        rocksDB->setDecryptHandler(getDecryptHandler());
    }
    // create and init rocksDBStorage
    std::shared_ptr<RocksDBStorage> rocksdbStorage =
        std::make_shared<RocksDBStorage>(_disableWAL, !_enableCache);
    rocksdbStorage->setDB(rocksDB);
    return rocksdbStorage;
}

dev::storage::Storage::Ptr dev::ledger::createSQLStorage(
    std::shared_ptr<LedgerParamInterface> _param, ChannelRPCServer::Ptr _channelRPCServer,
    std::function<void(std::exception& e)> _fatalHandler)
{
    auto sqlStorage = std::make_shared<SQLStorage>();
    sqlStorage->setChannelRPCServer(_channelRPCServer);
    sqlStorage->setTopic(_param->mutableStorageParam().topic);
    sqlStorage->setFatalHandler(_fatalHandler);
    sqlStorage->setMaxRetry(_param->mutableStorageParam().maxRetry);
    return sqlStorage;
}

dev::storage::Storage::Ptr dev::ledger::createZdbStorage(
    std::shared_ptr<LedgerParamInterface> _param,
    std::function<void(std::exception& e)> _fatalHandler)
{
    ConnectionPoolConfig connectionConfig{_param->mutableStorageParam().dbType,
        _param->mutableStorageParam().dbIP, _param->mutableStorageParam().dbPort,
        _param->mutableStorageParam().dbUsername, _param->mutableStorageParam().dbPasswd,
        _param->mutableStorageParam().dbName, _param->mutableStorageParam().dbCharset,
        _param->mutableStorageParam().initConnections,
        _param->mutableStorageParam().maxConnections};
    auto zdbStorage = std::make_shared<ZdbStorage>();

    auto sqlconnpool = std::make_shared<SQLConnectionPool>();
    sqlconnpool->createDataBase(connectionConfig);
    sqlconnpool->InitConnectionPool(connectionConfig);

    auto sqlAccess = std::make_shared<SQLBasicAccess>();
    zdbStorage->SetSqlAccess(sqlAccess);
    zdbStorage->setConnPool(sqlconnpool);

    zdbStorage->setFatalHandler(_fatalHandler);
    return zdbStorage;
}
