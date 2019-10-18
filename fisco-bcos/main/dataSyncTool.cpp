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
#include <clocale>
#include <ctime>
#include <iostream>
#include <memory>

using namespace std;
using namespace dev;
using namespace dev::ledger;
using namespace dev::storage;
using namespace dev::initializer;
namespace fs = boost::filesystem;

vector<TableInfo::Ptr> parseTableNames(TableData::Ptr data, Entries::Ptr = nullptr)
{
    auto entries = data->dirtyEntries;
    vector<TableInfo::Ptr> res(entries->size(), std::make_shared<TableInfo>());
    for (size_t i = 0; i < entries->size(); ++i)
    {
        // FIXME: skip SYS_HASH_2_BLOCK and SYS_BLOCK_2_NONCES, use storage select
        auto entry = entries->get(i);
        res[i]->name = entry->getField("table_name");
        res[i]->key = entry->getField("key_field");
    }
    return res;
}

void syncData(SQLStorage::Ptr _reader, Storage::Ptr _writer, int64_t _blockNumber)
{
    // TODO: check if already sync part of data
    // TODO: read _sys_tables_ to get all tables at blockNumber
    auto sysTableInfo = getSysTableInfo(SYS_TABLES);
    auto data = _reader->selectTableDataByNum(_blockNumber, sysTableInfo);
    // get tables' data of last step
    vector<TableInfo::Ptr> tableInfos = parseTableNames(data);
    for (const auto& tableInfo : tableInfos)
    {
        auto tableData = _reader->selectTableDataByNum(_blockNumber, tableInfo);
        tableData->info = tableInfo;
        _writer->commit(_blockNumber, vector<TableData::Ptr>{tableData});
        // TODO: write data into _writer, record the committed table name
    }
}

void fastSyncGroupData(const std::string& _genesisConfigFile, const std::string& _dataPath,
    ChannelRPCServer::Ptr _channelRPCServer)
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
    blockNumber = blockNumber > 0 ? blockNumber + 1 : 0;
    // create scalableStorage
    auto scalableStorage =
        std::make_shared<ScalableStorage>(params->mutableStorageParam().scrollThreshold);
    auto blocksDBPath = "";
    auto rocksDBStorageFactory = make_shared<RocksDBStorageFactory>(
        blocksDBPath, params->mutableStorageParam().binaryLog, false);
    rocksDBStorageFactory->setDBOpitons(getRocksDBOptions());
    scalableStorage->setStorageFactory(rocksDBStorageFactory);
    // make RocksDBStorage think cachedStorage is exist
    auto stateStorage =
        createRocksDBStorage(params->mutableStorageParam().path + "/state", false, false, false);
    scalableStorage->setStateStorage(stateStorage);
    auto archiveStorage = rocksDBStorageFactory->getStorage(to_string(blockNumber));
    scalableStorage->setArchiveStorage(archiveStorage, blockNumber);
    // fast sync data
    syncData(sqlStorage, scalableStorage, blockNumber - 100);
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
    char buffer[40];
    auto currentTime = chrono::system_clock::to_time_t(chrono::system_clock::now());
    // get datetime and output welcome info
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localtime(&currentTime));
    /// callback initializer to init all ledgers
    version();
    currentTime = chrono::system_clock::to_time_t(chrono::system_clock::now());
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localtime(&currentTime));
    std::cout << "[" << buffer << "] ";
    std::cout << "The FISCO-BCOS is Initializing..." << std::endl;
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
        rpcInitializer->initChannelRPCServer(pt);
        auto groupConfigPath = pt.get<string>("group.group_config_path", "conf/");
        auto dataPath = pt.get<string>("group.group_data_path", 0);
        boost::filesystem::path path(groupConfigPath);
        if (fs::is_directory(path))
        {
            fs::directory_iterator endIter;
            for (fs::directory_iterator iter(path); iter != endIter; iter++)
            {
                if (fs::extension(*iter) == ".genesis")
                {
                    fastSyncGroupData(
                        iter->path().string(), dataPath, rpcInitializer->channelRPCServer());
                }
            }
        }
    }
    catch (std::exception& e)
    {
        std::cerr << "Init failed!!!" << std::endl;
        return -1;
    }

    currentTime = chrono::system_clock::to_time_t(chrono::system_clock::now());
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localtime(&currentTime));
    std::cout << "[" << buffer << "] ";
    std::cout << "FISCO-BCOS program exit normally." << std::endl;

    return 0;
}
