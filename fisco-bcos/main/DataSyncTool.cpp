/**
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
 *
 * @brief: empty framework for main of FISCO-BCOS
 *
 * @file: dataSyncTool.cpp
 * @author: xingqiangbai
 * @date 2019-10-15
 */
#include "Common.h"
#include "libinitializer/Initializer.h"
#include "libledger/DBInitializer.h"
#include "libledger/LedgerParam.h"
#include "libstorage/BasicRocksDB.h"
#include "libstorage/MemoryTableFactoryFactory2.h"
#include "libstorage/RocksDBStorage.h"
#include "libstorage/RocksDBStorageFactory.h"
#include "libstorage/SQLStorage.h"
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <clocale>
#include <ctime>
#include <iostream>
#include <memory>

using namespace std;
using namespace dev;
using namespace boost;
using namespace dev::ledger;
using namespace dev::storage;
using namespace dev::initializer;
namespace fs = boost::filesystem;

vector<TableInfo::Ptr> parseTableNames(TableData::Ptr data, Entries::Ptr = nullptr)
{
    auto entries = data->dirtyEntries;
    vector<TableInfo::Ptr> res(entries->size(), nullptr);
    for (size_t i = 0; i < entries->size(); ++i)
    {
        // FIXME: skip SYS_HASH_2_BLOCK and SYS_BLOCK_2_NONCES, use storage select
        auto entry = entries->get(i);
        res[i] = std::make_shared<TableInfo>();
        res[i]->name = entry->getField("table_name");
        res[i]->key = entry->getField("key_field");
    }
    return res;
}

TableData::Ptr getBlockToNonceData(SQLStorage::Ptr _reader, int64_t _blockNumber)
{
    cout << "Process " << SYS_BLOCK_2_NONCES << endl;
    auto tableFactoryFactory = std::make_shared<dev::storage::MemoryTableFactoryFactory2>();
    tableFactoryFactory->setStorage(_reader);
    auto memoryTableFactory = tableFactoryFactory->newTableFactory(dev::h256(), _blockNumber);
    Table::Ptr tb = memoryTableFactory->openTable(SYS_BLOCK_2_NONCES);
    auto entries = tb->select(lexical_cast<std::string>(_blockNumber), tb->newCondition());

    if (entries->size() == 0)
    {
        // ERROR
        return nullptr;
    }
    else
    {
        auto tableInfo = getSysTableInfo(SYS_BLOCK_2_NONCES);
        auto tableData = std::make_shared<TableData>();
        tableData->info = tableInfo;
        tableData->dirtyEntries = std::make_shared<Entries>();
        for (size_t i = 0; i < entries->size(); ++i)
        {
            auto entry = std::make_shared<Entry>();
            entry->copyFrom(entries->get(i));
            tableData->dirtyEntries->addEntry(entry);
        }
        tableData->newEntries = std::make_shared<Entries>();
        return tableData;
    }
}

TableData::Ptr getHashToBlockData(SQLStorage::Ptr _reader, int64_t _blockNumber)
{
    cout << " Process " << SYS_HASH_2_BLOCK << endl;

    auto tableFactoryFactory = std::make_shared<dev::storage::MemoryTableFactoryFactory2>();
    tableFactoryFactory->setStorage(_reader);
    auto memoryTableFactory = tableFactoryFactory->newTableFactory(dev::h256(), _blockNumber);
    Table::Ptr tb = memoryTableFactory->openTable(SYS_NUMBER_2_HASH);
    auto entries = tb->select(lexical_cast<std::string>(_blockNumber), tb->newCondition());
    h256 blockHash;
    if (entries->size() == 0)
    {
        // ERROR
        return nullptr;
    }
    else
    {
        auto entry = entries->get(0);
        blockHash = h256((entry->getField(SYS_VALUE)));
    }
    tb = memoryTableFactory->openTable(SYS_HASH_2_BLOCK);
    entries = tb->select(blockHash.hex(), tb->newCondition());
    if (entries->size() == 0)
    {
        // ERROR
        return nullptr;
    }
    else
    {
        auto tableInfo = getSysTableInfo(SYS_HASH_2_BLOCK);
        auto tableData = std::make_shared<TableData>();
        tableData->info = tableInfo;
        tableData->dirtyEntries = std::make_shared<Entries>();
        for (size_t i = 0; i < entries->size(); ++i)
        {
            auto entry = std::make_shared<Entry>();
            entry->copyFrom(entries->get(i));
            tableData->dirtyEntries->addEntry(entry);
        }
        tableData->newEntries = std::make_shared<Entries>();
        return tableData;
    }
}

void syncData(SQLStorage::Ptr _reader, ScalableStorage::Ptr _writer, int64_t _blockNumber)
{
    // TODO: check if already sync part of data
    cout << "Sync block number is " << _blockNumber << endl;
    auto sysTableInfo = getSysTableInfo(SYS_TABLES);
    auto data = _reader->selectTableDataByNum(_blockNumber, sysTableInfo);
    _writer->commit(_blockNumber, vector<TableData::Ptr>{data});
    cout << SYS_TABLES << " is committed." << endl;

    // get tables' data of last step
    vector<TableInfo::Ptr> tableInfos = parseTableNames(data);
    for (const auto& tableInfo : tableInfos)
    {
        if (_writer->isStateData(tableInfo->name) && tableInfo->name != SYS_TABLES)
        {
            auto tableData = _reader->selectTableDataByNum(_blockNumber, tableInfo);
            if (!tableData)
            {
                LOG(ERROR) << "query failed. Table=" << tableInfo->name << endl;
            }
            _writer->commit(_blockNumber, vector<TableData::Ptr>{tableData});
            LOG(INFO) << tableInfo->name << " is committed." << endl;
            cout << tableInfo->name << " is committed." << endl;
            // TODO: write data into _writer, record the committed table name
        }
    }
    // SYS_HASH_2_BLOCK
    data = getHashToBlockData(_reader, _blockNumber);
    _writer->commit(_blockNumber, vector<TableData::Ptr>{data});
    // SYS_BLOCK_2_NONCES
    data = getBlockToNonceData(_reader, _blockNumber);
    if (data)
    {
        _writer->commit(_blockNumber, vector<TableData::Ptr>{data});
    }
    else
    {
        cout << "block number to nonce is empty at " << _blockNumber << endl;
    }
    // TODO: record the committed table name, clean records
}

void fastSyncGroupData(const std::string& _genesisConfigFile, const std::string& _dataPath,
    ChannelRPCServer::Ptr _channelRPCServer, int64_t _rollbackNumber = 1000)
{
    auto params = std::make_shared<LedgerParam>();
    params->init(_genesisConfigFile, _dataPath);
    // create SQLStorage
    auto sqlStorage = std::make_shared<SQLStorage>();
    sqlStorage->setChannelRPCServer(_channelRPCServer);
    sqlStorage->setTopic(params->mutableStorageParam().topic);
    sqlStorage->setFatalHandler([](std::exception& e) {
        LOG(ERROR) << LOG_BADGE("STORAGE") << LOG_BADGE("MySQL")
                   << "access mysql failed exit:" << e.what();
        raise(SIGTERM);
        BOOST_THROW_EXCEPTION(e);
    });
    sqlStorage->setMaxRetry(params->mutableStorageParam().maxRetry);
    auto blockNumber = getBlockNumberFromStorage(sqlStorage);
    blockNumber = blockNumber >= _rollbackNumber ? blockNumber - _rollbackNumber : 0;
    // create scalableStorage
    auto scalableStorage =
        std::make_shared<ScalableStorage>(params->mutableStorageParam().scrollThreshold);
    auto rocksDBStorageFactory =
        std::make_shared<RocksDBStorageFactory>(params->mutableStorageParam().path + "/blocksDB",
            params->mutableStorageParam().binaryLog, false);
    rocksDBStorageFactory->setDBOpitons(getRocksDBOptions());
    scalableStorage->setStorageFactory(rocksDBStorageFactory);
    // make RocksDBStorage think cachedStorage is exist
    auto stateStorage =
        createRocksDBStorage(params->mutableStorageParam().path + "/state", false, false, true);
    scalableStorage->setStateStorage(stateStorage);
    auto archiveStorage = rocksDBStorageFactory->getStorage(to_string(blockNumber));
    scalableStorage->setArchiveStorage(archiveStorage, blockNumber);
    scalableStorage->setRemoteBlockNumber(blockNumber);
    // fast sync data
    syncData(sqlStorage, scalableStorage, blockNumber);
}

int main(int argc, const char* argv[])
{
    /// set LC_ALL
    setDefaultOrCLocale();
    std::set_terminate([]() {
        std::cerr << "terminate handler called" << endl;
        abort();
    });
    /// init params
    string configPath = initCommandLine(argc, argv);
    version();
    std::cout << "[" << getCurrentDateTime() << "] "
              << ". The sync-tool is Initializing..." << std::endl;
    try
    {
        /// init log
        boost::property_tree::ptree pt;
        boost::property_tree::read_ini(configPath, pt);
        auto logInitializer = std::make_shared<LogInitializer>();
        logInitializer->initLog(pt);

        /// init global config. must init before DB, for compatibility
        initGlobalConfig(pt);
        /// init channelServer
        auto secureInitializer = std::make_shared<SecureInitializer>();
        secureInitializer->initConfig(pt);

        auto rpcInitializer = std::make_shared<RPCInitializer>();
        rpcInitializer->setSSLContext(
            secureInitializer->SSLContext(SecureInitializer::Usage::ForRPC));
        auto p2pService = std::make_shared<Service>();
        rpcInitializer->setP2PService(p2pService);
        rpcInitializer->initChannelRPCServer(pt);
        auto groupConfigPath = pt.get<string>("group.group_config_path", "conf/");
        auto dataPath = pt.get<string>("group.group_data_path", "data/");
        boost::filesystem::path path(groupConfigPath);
        std::cout << "[" << getCurrentDateTime() << "] The sync-tool is syncing..." << std::endl;
        if (fs::is_directory(path))
        {
            fs::directory_iterator endIter;
            for (fs::directory_iterator iter(path); iter != endIter; iter++)
            {
                if (fs::extension(*iter) == ".genesis")
                {
                    std::cout << "[" << getCurrentDateTime() << "] "
                              << "syncing " << iter->path().string() << " ..." << std::endl;
                    fastSyncGroupData(
                        iter->path().string(), dataPath, rpcInitializer->channelRPCServer());
                }
            }
        }
    }
    catch (std::exception& e)
    {
        std::cerr << LOG_KV("EINFO", boost::diagnostic_information(e));
        std::cerr << "Sync failed!!!" << std::endl;
        return -1;
    }

    std::cout << "[" << getCurrentDateTime() << "] "
              << "Sync complete." << std::endl;

    return 0;
}
