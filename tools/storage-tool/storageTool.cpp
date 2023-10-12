/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief the tool to read and modify data of storage
 * @file storageTool.cpp
 * @author: xingqiangbai
 * @date 2022-07-13
 */

#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/storage/StorageInterface.h"
#include "bcos-ledger/src/libledger/utilities/Common.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tool/bcos-tool/BfsFileFactory.h"
#include "bcos-utilities/BoostLogInitializer.h"
#include "boost/filesystem.hpp"
#include "libinitializer/ProtocolInitializer.h"
#include "libinitializer/StorageInitializer.h"
#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/slice.h"
#include "tikv_client.h"
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework/security/DataEncryptInterface.h>
#include <bcos-security/bcos-security/DataEncryption.h>
#include <bcos-storage/RocksDBStorage.h>
#include <bcos-table/src/KeyPageStorage.h>
#include <bcos-table/src/StateStorageFactory.h>
#include <boost/algorithm/hex.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/program_options.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/throw_exception.hpp>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

using namespace std;
using namespace rocksdb;
using namespace bcos;
using namespace bcos::storage;
using namespace bcos::initializer;

namespace fs = boost::filesystem;
namespace po = boost::program_options;

po::options_description main_options("storage tool used to read/write the data of storage");

po::variables_map initCommandLine(int argc, const char* argv[])
{
    main_options.add_options()("help,h", "help of storage tool")(
        "statistic,s", "statistic the data usage of the storage")(
        "stateSize,S", "statistic the data usage of the contracts state")(
        "read,r", po::value<vector<string>>()->multitoken(), "[TableName] [Key]")("write,w",
        po::value<std::vector<std::string>>()->multitoken(),
        "[TableName] [Key] [Value]")("iterate,i", po::value<std::string>(), "[TableName]")("hex,H",
        po::value<bool>()->default_value(false),
        "if read display value use hex, if write decode hex value")("compare,C",
        po::value<std::vector<std::string>>()->multitoken(),
        "[RocksDB] [path] [Table] or [TiKV] [pd addresses] [Table]/[ca path if use ssl] [cert path "
        "if use ssl] [Table], eg RocksDB ../node0/data s_hash_2_tx"
        "[key path if use ssl]")("config,c",
        boost::program_options::value<std::string>()->default_value("./config.ini"),
        "config file path")("genesis,g",
        boost::program_options::value<std::string>()->default_value("./config.genesis"),
        "genesis config file path");
    po::variables_map varMap;
    try
    {
        po::store(po::parse_command_line(argc, argv, main_options), varMap);
        po::notify(varMap);
    }
    catch (std::exception& e)
    {
        std::cout << "parse_command_line failed:" << e.what() << std::endl;
        std::cout << main_options << std::endl;
        exit(0);
    }
    if ((varMap.count("help") != 0U) || (varMap.count("h") != 0U))
    {
        std::cout << main_options << std::endl;
        exit(0);
    }
    return varMap;
}

std::shared_ptr<std::set<std::string, std::less<>>> getKeyPageIgnoreTables(
    uint32_t version = (uint32_t)protocol::BlockVersion::V3_1_VERSION)
{
    auto ignoreTables = std::make_shared<std::set<std::string, std::less<>>>(
        std::initializer_list<std::set<std::string, std::less<>>::value_type>{
            std::string(ledger::SYS_CONFIG),
            std::string(ledger::SYS_CONSENSUS),
            std::string(ledger::SYS_CURRENT_STATE),
            std::string(ledger::SYS_HASH_2_NUMBER),
            std::string(ledger::SYS_NUMBER_2_HASH),
            std::string(ledger::SYS_BLOCK_NUMBER_2_NONCES),
            std::string(ledger::SYS_NUMBER_2_BLOCK_HEADER),
            std::string(ledger::SYS_NUMBER_2_TXS),
            std::string(ledger::SYS_HASH_2_TX),
            std::string(ledger::SYS_HASH_2_RECEIPT),
            std::string(ledger::SYS_TXHASH_2_NUMBER),
            std::string(ledger::SYS_NUMBER_2_BLOCK_TXS),
            std::string(storage::FS_ROOT),
            std::string(storage::FS_APPS),
            std::string(storage::FS_USER),
            std::string(storage::FS_SYS_BIN),
            std::string(storage::FS_USER_TABLE),
            std::string(ledger::SYS_CONTRACT_ABI),
            std::string(ledger::SYS_CODE_BINARY),
            storage::StorageInterface::SYS_TABLES,
        });
    if (version >= (uint32_t)protocol::BlockVersion::V3_1_VERSION)
    {
        for (const auto& _sub : tool::FS_ROOT_SUBS)
        {
            std::string sub(_sub);
            ignoreTables->erase(sub);
        }
    }
    return ignoreTables;
}

StateStorageInterface::Ptr createKeyPageStorage(
    StorageInterface::Ptr backend, size_t keyPageSize, uint32_t blockVersion)
{
    auto keyPageIgnoreTables = getKeyPageIgnoreTables(blockVersion);
    return std::make_shared<bcos::storage::KeyPageStorage>(
        backend, keyPageSize, blockVersion, keyPageIgnoreTables);
}

void print(
    std::string_view tableName, std::string_view key, std::string_view value, bool hex = false)
{
    cout << "[tableName=" << tableName << "]"
         << " [key=" << key << "] [value=" << (hex ? toHex(value) : value) << "]" << endl;
}

void writeKV(std::ofstream& output, std::string_view key, std::string_view value, bool hex = false)
{
    output << "[key=" << (hex ? toHex(key) : key) << "] [value=" << (hex ? toHex(value) : value)
           << "]" << endl;
}

void writeKeys(std::ostream& output, const std::vector<std::string>& keys, bool hex = false)
{
    output << "[";
    for (size_t i = 0; i < keys.size() - 1; ++i)
    {
        output << (hex ? toHex(keys[i]) : keys[i]) << ",";
    }
    output << (hex ? toHex(keys.back()) : keys.back()) << "]" << endl;
}

DB* createSecondaryRocksDB(
    const std::string& path, const std::string& secondaryPath = "./rocksdb_secondary/")
{
    Options options;
    options.create_if_missing = false;
    options.max_open_files = -1;
    DB* db_secondary = nullptr;
    Status status = DB::OpenAsSecondary(options, path, secondaryPath, &db_secondary);
    if (!status.ok())
    {
        std::cout << "open rocksDB failed: " << status.ToString() << std::endl;
        exit(1);
    }
    status = db_secondary->TryCatchUpWithPrimary();
    if (!status.ok())
    {
        std::cout << "TryCatchUpWithPrimary failed: " << status.ToString() << std::endl;
        exit(1);
    }
    return db_secondary;
}

void getTableSize(DB* db, const string_view& table)
{
    std::string tableName(table);
    double size = 0;
    rocksdb::Iterator* it = db->NewIterator(rocksdb::ReadOptions());
    it->Seek(tableName);
    while (it->Valid())
    {
        if (it->key().starts_with(tableName))
        {
            size += it->value().size();
            size += it->key().size();
        }
        else
        {
            break;
        }
        it->Next();
    }
    delete it;
    auto setw = 30;
    if (size < 1024)
    {  // < 1MB
        cout << std::left << std::setw(setw) << tableName << " size is " << size << "B" << endl;
    }
    else if (size < 1024 * 1024)
    {  // < 1MB
        cout << std::left << std::setw(setw) << tableName << " size is " << (size / 1024) << "KB"
             << endl;
    }
    else if (size < 1024 * 1024 * 1024)
    {  // < 1GB
        cout << std::left << std::setw(setw) << tableName << " size is " << (size / 1024 / 1024)
             << "MB" << endl;
    }
    else
    {
        cout << std::left << std::setw(setw) << tableName << " size is "
             << (size / 1024 / 1024 / 1024) << "GB" << endl;
    }
}

TransactionalStorageInterface::Ptr createBackendStorage(
    std::shared_ptr<bcos::tool::NodeConfig> nodeConfig, const std::string& logPath,
    bool write = false, const std::string& secondaryPath = "./rocksdb_secondary/")
{
    bcos::storage::TransactionalStorageInterface::Ptr storage = nullptr;
    if (boost::iequals(nodeConfig->storageType(), "RocksDB"))
    {
        bcos::security::DataEncryptInterface::Ptr dataEncryption = nullptr;
        if (nodeConfig->storageSecurityEnable())
        {
            dataEncryption = std::make_shared<bcos::security::DataEncryption>(nodeConfig);
        }
        if (write)
        {
            RocksDBOption option;
            option.maxWriteBufferNumber = nodeConfig->maxWriteBufferNumber();
            option.maxBackgroundJobs = nodeConfig->maxBackgroundJobs();
            option.writeBufferSize = nodeConfig->writeBufferSize();
            option.minWriteBufferNumberToMerge = nodeConfig->minWriteBufferNumberToMerge();
            option.blockCacheSize = nodeConfig->blockCacheSize();

            storage = StorageInitializer::build(
                nodeConfig->storagePath(), option, dataEncryption, nodeConfig->keyPageSize());
        }
        else
        {
            auto* rocksdb = createSecondaryRocksDB(nodeConfig->storagePath(), secondaryPath);
            storage = std::make_shared<RocksDBStorage>(
                std::unique_ptr<rocksdb::DB>(rocksdb), dataEncryption);
        }
    }
    else if (boost::iequals(nodeConfig->storageType(), "TiKV"))
    {
#ifdef WITH_TIKV
        storage = StorageInitializer::build(nodeConfig->pdAddrs(), logPath, nodeConfig->pdCaPath(),
            nodeConfig->pdCertPath(), nodeConfig->pdKeyPath());
#endif
    }
    else
    {
        throw std::runtime_error("storage type not support");
    }
    return storage;
}

bool compareTables(StorageInterface::Ptr local, StorageInterface::Ptr remote,
    const std::string& table, auto blockFactory)
{
    std::promise<std::vector<std::string>> localKeysPromise;
    local->asyncGetPrimaryKeys(table, std::nullopt,
        [&localKeysPromise](Error::UniquePtr error, std::vector<std::string> keys) {
            if (error)
            {
                std::cout << "get local primary keys failed: " << error << std::endl;
                exit(1);
            }
            localKeysPromise.set_value(std::move(keys));
        });
    std::promise<std::vector<std::string>> remoteKeysPromise;
    remote->asyncGetPrimaryKeys(table, std::nullopt,
        [&remoteKeysPromise](Error::UniquePtr error, std::vector<std::string> keys) {
            if (error)
            {
                std::cout << "get local primary keys failed: " << error << std::endl;
                exit(1);
            }
            remoteKeysPromise.set_value(std::move(keys));
        });
    // TODO: batch compare keys
    auto localKeys = localKeysPromise.get_future().get();
    auto remoteKeys = remoteKeysPromise.get_future().get();
    std::sort(localKeys.begin(), localKeys.end());
    std::sort(remoteKeys.begin(), remoteKeys.end());
    if (localKeys != remoteKeys)
    {
        std::cout << table << ", keys not equal" << LOG_KV("localSize", localKeys.size())
                  << LOG_KV("remoteSize", remoteKeys.size()) << std::endl;
        std::cout << table << ", Remote:";
        writeKeys(std::cout, localKeys, true);
        std::cout << table << ", Local:";
        writeKeys(std::cout, remoteKeys, true);
        return false;
    }
    std::promise<std::vector<std::optional<Entry>>> localValuesPromise;
    local->asyncGetRows(table, localKeys,
        [&localValuesPromise](Error::UniquePtr error, std::vector<std::optional<Entry>> values) {
            if (error)
            {
                std::cout << "get local values failed: " << error << std::endl;
                exit(1);
            }
            localValuesPromise.set_value(std::move(values));
        });
    std::promise<std::vector<std::optional<Entry>>> remoteValuesPromise;
    remote->asyncGetRows(table, localKeys,
        [&remoteValuesPromise](Error::UniquePtr error, std::vector<std::optional<Entry>> values) {
            if (error)
            {
                std::cout << "get local values failed: " << error << std::endl;
                exit(1);
            }
            remoteValuesPromise.set_value(std::move(values));
        });
    auto localValues = localValuesPromise.get_future().get();
    auto remoteValues = remoteValuesPromise.get_future().get();
    bool equal = true;
    auto transactionFactory = blockFactory->transactionFactory();

    for (size_t i = 0; i < localValues.size(); ++i)
    {
        if (localValues[i]->get().compare(remoteValues[i]->get()) != 0)
        {
            if (table == std::string(ledger::SYS_HASH_2_TX))
            {
                auto lView = localValues[i]->get();
                auto rView = remoteValues[i]->get();
                auto localTx = dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(
                    transactionFactory->createTransaction(
                        bytesConstRef((unsigned char*)lView.data(), lView.size()), false));
                auto remoteTx = dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(
                    transactionFactory->createTransaction(
                        bytesConstRef((unsigned char*)rView.data(), rView.size()), false));
                if (localTx->hash() == remoteTx->hash())
                {
#if 0
                    std::cout << "localTx equal to remoteTx" << std::endl;
                    std::cout << "localTx :"
                              //   << LOG_KV("signatureData", toHex(localTx->signatureData()))
                              //   << LOG_KV("sender", toHex(localTx->sender()))
                              << LOG_KV("importTime", localTx->importTime())
                              << LOG_KV("attribute", localTx->attribute())
                              << LOG_KV("source", localTx->source()) << std::endl;
                    std::cout << "remoteTx:"
                              //   << LOG_KV("signatureData", toHex(remoteTx->signatureData()))
                              //   << LOG_KV("sender", toHex(remoteTx->sender()))
                              << LOG_KV("importTime", remoteTx->importTime())
                              << LOG_KV("attribute", remoteTx->attribute())
                              << LOG_KV("source", remoteTx->source()) << std::endl;
#endif
                }
                else
                {
                    std::cout << table << ", value not equal, key: " << toHex(localKeys[i])
                              << LOG_KV("local", toHex(localValues[i]->get()))
                              << LOG_KV("remote", toHex(remoteValues[i]->get())) << std::endl;
                    std::cout << "localTx not equal to remoteTx" << std::endl;
                    equal = false;
                }
            }
            else if (table == std::string(ledger::SYS_NUMBER_2_BLOCK_HEADER))
            {
                auto lView = localValues[i]->get();
                auto rView = remoteValues[i]->get();
                auto headerFactory = blockFactory->blockHeaderFactory();
                auto localBlockHeader = headerFactory->createBlockHeader(
                    bytesConstRef((unsigned char*)lView.data(), lView.size()));
                auto remoteBlockHeader = headerFactory->createBlockHeader(
                    bytesConstRef((unsigned char*)rView.data(), rView.size()));
                if (localBlockHeader->hash() != remoteBlockHeader->hash())
                {
                    std::cout << table << ", value not equal, key: " << toHex(localKeys[i])
                              << LOG_KV("local", toHex(localValues[i]->get()))
                              << LOG_KV("remote", toHex(remoteValues[i]->get())) << std::endl;
                    std::cout << "localBlock not equal to remoteBlock" << std::endl;
                    equal = false;
                }
            }
            else
            {
                std::cout << table << ", value not equal, key: " << toHex(localKeys[i])
                          << LOG_KV("local", toHex(localValues[i]->get()))
                          << LOG_KV("remote", toHex(remoteValues[i]->get())) << std::endl;
                equal = false;
            }
        }
    }
    cout << "The data of table " << std::setw(64) << table << " is same\r" << flush;
    return equal;
}

int main(int argc, const char* argv[])
{
    boost::property_tree::ptree pt;
    auto params = initCommandLine(argc, argv);
    // TODO: parse config file
    std::string configPath("./config.ini");
    if (params.count("config"))
    {
        configPath = params["config"].as<std::string>();
    }
    if (!fs::exists(configPath))
    {
        cout << "config file not found:" << configPath << endl;
        return 1;
    }

    std::string genesisFilePath("./config.genesis");
    if (params.count("genesis"))
    {
        genesisFilePath = params["genesis"].as<std::string>();
    }

    auto hexEncoded = params["hex"].as<bool>();

    cout << "config file     : " << configPath << endl;
    cout << "genesis file    : " << genesisFilePath << endl;
    boost::property_tree::read_ini(configPath, pt);
    auto logInitializer = std::make_shared<BoostLogInitializer>();
    logInitializer->initLog(pt);

    // load node config
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    auto nodeConfig = std::make_shared<bcos::tool::NodeConfig>(keyFactory);
    if (fs::exists(genesisFilePath))
    {
        nodeConfig->loadGenesisConfig(genesisFilePath);
    }

    nodeConfig->loadConfig(configPath);
    bcos::security::DataEncryptInterface::Ptr dataEncryption = nullptr;
    if (nodeConfig->storageSecurityEnable())
    {
        dataEncryption = std::make_shared<bcos::security::DataEncryption>(nodeConfig);
    }

    auto keyPageSize = nodeConfig->keyPageSize();
    auto keyPageIgnoreTables = getKeyPageIgnoreTables(nodeConfig->compatibilityVersion());
    std::string secondaryPath = "./rocksdb_secondary/";
    std::string remoteSecondaryPath = "./rocksdb_secondary/";
    if (params.count("read"))
    {  // read
        auto readParameters = params["read"].as<vector<string>>();
        if (readParameters.empty())
        {
            cerr << "invalid tableName" << endl;
            return -1;
        }
        auto tableName = readParameters[0];
        string key;
        if (readParameters.size() >= 2)
        {
            key = readParameters[1];
        }
        cout << "read " << tableName << ", key is " << key << endl;
        if (hexEncoded)
        {
            auto keyBytes = fromHexString(key);
            key = std::string((char*)keyBytes->data(), keyBytes->size());
        }
        // create secondary instance
        StorageInterface::Ptr storage = createBackendStorage(nodeConfig, logInitializer->logPath());
        if (keyPageSize > 0 && !keyPageIgnoreTables->count(tableName))
        {
            auto keyPageStorage =
                createKeyPageStorage(storage, keyPageSize, nodeConfig->compatibilityVersion());
            keyPageStorage->setReadOnly(true);
            storage = keyPageStorage;
        }
        std::promise<std::pair<Error::UniquePtr, std::optional<Entry>>> getPromise;
        storage->asyncGetRow(
            tableName, key, [&](Error::UniquePtr err, const std::optional<Entry>& opEntry) {
                getPromise.set_value(std::make_pair(std::move(err), opEntry));
            });
        auto ret = getPromise.get_future().get();
        if (ret.first)
        {
            cerr << "get row failed, err:" << ret.first->errorMessage() << endl;
            return -1;
        }
        // print result
        if (!ret.second.has_value())
        {
            cerr << "get row not found," << LOG_KV("table", tableName) << LOG_KV("key", key)
                 << endl;
            return -1;
        }
        if (key.empty())
        {  // print table meta
            KeyPageStorage::TableMeta meta(ret.second->get());
            cout << meta << std::endl;
        }
        else
        {
            print(tableName, key, ret.second->get(), hexEncoded);
        }
    }
    else if (params.count("write"))
    {  // write
        auto writeParameters = params["write"].as<vector<string>>();
        if (writeParameters.empty() || writeParameters.size() < 3)
        {
            cerr << "invalid parameters, should include [TableName] [Key] [Value]" << endl;
            return -1;
        }
        auto tableName = writeParameters[0];
        auto key = writeParameters[1];
        std::string value;
        if (hexEncoded)
        {
            auto keyBytes = fromHexString(key);
            key = std::string((char*)keyBytes->data(), keyBytes->size());
            auto tempBytes = fromHex(writeParameters[2]);
            value = std::string((char*)tempBytes.data(), tempBytes.size());
        }
        else
        {
            value = writeParameters[2];
        }
        TransactionalStorageInterface::Ptr rocksdbStorage =
            dynamic_pointer_cast<TransactionalStorageInterface>(
                createBackendStorage(nodeConfig, logInitializer->logPath(), true));
        StorageInterface::Ptr storage = rocksdbStorage;
        if (keyPageSize > 0 && !keyPageIgnoreTables->count(tableName))
        {
            storage =
                createKeyPageStorage(storage, keyPageSize, nodeConfig->compatibilityVersion());
        }
        // std::promise<std::pair<Error::UniquePtr, std::optional<Entry>>> getPromise;
        // storage->asyncGetRow(
        //     tableName, key, [&](Error::UniquePtr err, std::optional<Entry> opEntry) {
        //         getPromise.set_value(std::make_pair(std::move(err), opEntry));
        //     });
        // auto ret = getPromise.get_future().get();
        // if (ret.first)
        // {
        //     cerr << "get row failed, err:" << ret.first->errorMessage() << endl;
        //     return -1;
        // }

        // async set row, check if need hex decode and write
        std::promise<Error::UniquePtr> setPromise;
        Entry e;
        if (value.empty())
        {
            e.setStatus(Entry::Status::DELETED);
        }
        else
        {
            e.set(std::move(value));
        }
        storage->asyncSetRow(
            tableName, key, e, [&](Error::UniquePtr err) { setPromise.set_value(std::move(err)); });
        auto err = setPromise.get_future().get();
        if (err)
        {
            cerr << "set row failed, err:" << err->errorMessage() << endl;
            return -1;
        }
        // if use key page need commit use rocksDB
        if (keyPageSize > 0 && !keyPageIgnoreTables->count(tableName))
        {
            auto* keyPageStorage = dynamic_cast<TraverseStorageInterface*>(storage.get());
            bcos::protocol::TwoPCParams param;
            rocksdbStorage->asyncPrepare(
                param, *keyPageStorage, [&](Error::Ptr err, uint64_t, const std::string&) {
                    if (err)
                    {
                        cerr << "asyncPrepare failed, err:" << err->errorMessage() << endl;
                        exit(1);
                    }
                });
            rocksdbStorage->asyncCommit(param, [](Error::Ptr err, uint64_t) {
                if (err)
                {
                    cerr << "asyncCommit failed, err:" << err->errorMessage() << endl;
                    exit(1);
                }
            });
        }
        cout << "set successfully" << endl;
    }
    else if (params.count("iterate"))
    {  // iterate
        auto tableName = params["iterate"].as<string>();
        if (tableName.empty())
        {
            cerr << "empty table name" << endl;
            return -1;
        }
        StorageInterface::Ptr storage = createBackendStorage(nodeConfig, logInitializer->logPath());
        if (keyPageSize > 0 && !keyPageIgnoreTables->count(tableName))
        {
            storage =
                createKeyPageStorage(storage, keyPageSize, nodeConfig->compatibilityVersion());
        }
        auto outputFileName = tableName + ".txt";
        boost::replace_all(outputFileName, "/", "_");
        ofstream outfile("./" + outputFileName);
        outfile << "db path : " << nodeConfig->storagePath() << ", table : " << tableName << endl;
        if (keyPageSize > 0 && !keyPageIgnoreTables->count(tableName))
        {  // keypage
            size_t batchSize = 1000;
            size_t gotKeys = 1000;
            size_t total = 0;
            cout << "iterate use key page" << endl;
            while (gotKeys == batchSize)
            {
                storage::Condition condition;
                condition.limit(total, 1000);
                storage->asyncGetPrimaryKeys(
                    tableName, condition, [&](Error::UniquePtr err, std::vector<std::string> keys) {
                        if (err)
                        {
                            cerr << "asyncGetPrimaryKeys failed, err:" << err->errorMessage()
                                 << endl;
                            exit(1);
                        }
                        gotKeys = keys.size();
                        total += gotKeys;
                        for (auto& key : keys)
                        {
                            storage->asyncGetRow(
                                tableName, key, [&](Error::UniquePtr err, std::optional<Entry> e) {
                                    if (err)
                                    {
                                        cerr << "asyncGetRow failed, err:" << err->errorMessage()
                                             << endl;
                                        exit(1);
                                    }
                                    writeKV(outfile, key, e ? e->get() : "", hexEncoded);
                                });
                        }
                    });
            }
        }
        else
        {
            if (boost::iequals(nodeConfig->storageType(), "RocksDB"))
            {
                // rocksdb
                auto* rocksdb = createSecondaryRocksDB(nodeConfig->storagePath(), secondaryPath);
                rocksdb::Iterator* it = rocksdb->NewIterator(rocksdb::ReadOptions());
                it->Seek(tableName);
                while (it->Valid())
                {
                    if (it->key().starts_with(tableName))
                    {
                        // outfile << "[" << it->key().ToString() << "][" << it->value().ToString()
                        // <<
                        // "]" << endl;
                        writeKV(outfile, it->key().ToString(), it->value().ToString(), hexEncoded);
                    }
                    else
                    {
                        break;
                    }
                    it->Next();
                }
                delete it;
            }
            else if (boost::iequals(nodeConfig->storageType(), "TiKV"))
            {
#ifdef WITH_TIKV
                std::shared_ptr<tikv_client::TransactionClient> cluster = nullptr;
                cluster = storage::newTiKVClient(nodeConfig->pdAddrs(), logInitializer->logPath(),
                    nodeConfig->pdCaPath(), nodeConfig->pdCertPath(), nodeConfig->pdKeyPath());
                auto snapshot = cluster->snapshot();
                bool finished = false;
                uint32_t batch = 256;
                uint32_t count = 0;
                auto lastKey = tableName;
                while (!finished)
                {
                    auto kvPairs =
                        snapshot->scan(lastKey, Bound::Excluded, "", Bound::Unbounded, batch);
                    for (auto& kv : kvPairs)
                    {
                        if (kv.key.rfind(tableName, 0) == 0)
                        {
                            writeKV(outfile, kv.key, kv.value, hexEncoded);
                        }
                        else
                        {
                            finished = true;
                            break;
                        }
                    }
                    lastKey = kvPairs.back().key;
                    count += kvPairs.size();
                    std::cout << "scan count: " << count << "\r";
                    if (kvPairs.size() < batch)
                    {
                        finished = true;
                    }
                }
#endif
            }
            else
            {
                throw std::runtime_error("storage type not support");
            }
        }
        cout << "result in ./" << outputFileName << endl;
        outfile.close();
    }
    else if (params.count("stateSize") || params.count("S") || params.count("statistic") ||
             params.count("s"))
    {
        if (boost::iequals(nodeConfig->storageType(), "RocksDB"))
        {
            if (params.count("statistic") || params.count("s"))
            {  // statistics
                auto* db = createSecondaryRocksDB(nodeConfig->storagePath(), secondaryPath);
                getTableSize(db, storage::StorageInterface::SYS_TABLES);
                getTableSize(db, ledger::SYS_CONSENSUS);
                getTableSize(db, ledger::SYS_CONFIG);
                getTableSize(db, ledger::SYS_CURRENT_STATE);
                getTableSize(db, ledger::SYS_HASH_2_NUMBER);
                getTableSize(db, ledger::SYS_NUMBER_2_HASH);
                getTableSize(db, ledger::SYS_BLOCK_NUMBER_2_NONCES);
                getTableSize(db, ledger::SYS_NUMBER_2_BLOCK_HEADER);
                getTableSize(db, ledger::SYS_NUMBER_2_TXS);
                // calculate transactions data size
                getTableSize(db, ledger::SYS_HASH_2_TX);
                // calculate receipts data size
                getTableSize(db, ledger::SYS_HASH_2_RECEIPT);
                getTableSize(db, ledger::SYS_CODE_BINARY);
                getTableSize(db, ledger::SYS_CONTRACT_ABI);
            }
            if (params.count("stateSize") || params.count("S"))
            {  // calculate contract data size
                auto* db = createSecondaryRocksDB(nodeConfig->storagePath(), secondaryPath);
                getTableSize(db, storage::FS_APPS);
            }
        }
        else if (boost::iequals(nodeConfig->storageType(), "TiKV"))
        {
#ifdef WITH_TIKV
            // TODO: add TiKV support
#endif
        }
    }
    else if (params.count("compare") != 0U)
    {
        auto protocolInitializer = std::make_shared<ProtocolInitializer>();
        protocolInitializer->init(nodeConfig);
        auto blockFactory = protocolInitializer->blockFactory();
        auto compareParameters = params["compare"].as<vector<string>>();
        // compare data with tikv is not supported for now
        StorageInterface::Ptr localStorage =
            createBackendStorage(nodeConfig, logInitializer->logPath());
        StorageInterface::Ptr remoteStorage = nullptr;
        auto DBtype = compareParameters[0];
        std::string specificTable;
        if (boost::iequals(DBtype, "RocksDB"))
        {
            auto remoteDBPath = compareParameters[1];
            std::cout << "remoteDBPath:" << remoteDBPath << std::endl;
            auto* rocksdb = createSecondaryRocksDB(remoteDBPath, remoteSecondaryPath);
            remoteStorage =
                std::make_shared<RocksDBStorage>(std::unique_ptr<rocksdb::DB>(rocksdb), nullptr);
        }
        else if (boost::iequals(DBtype, "TiKV"))
        {
#ifdef WITH_TIKV
            vector<string> pdAddrs;
            std::cout << "pdAddrs:" << compareParameters[1] << std::endl;
            boost::algorithm::split(pdAddrs, compareParameters[1], boost::is_any_of(","));
            std::string caPath;
            std::string cert;
            std::string key;

            if (compareParameters.size() >= 5)
            {
                caPath = compareParameters[2];
                cert = compareParameters[3];
                key = compareParameters[4];
                specificTable = (compareParameters.size() == 6 ? compareParameters[5] : "");
            }
            remoteStorage =
                StorageInitializer::build(pdAddrs, logInitializer->logPath(), caPath, cert, key);
#endif
        }
        else
        {
            throw std::runtime_error("storage type not support");
        }
        if (compareParameters.size() == 3)
        {
            specificTable = compareParameters[2];
        }
        // compare SYS_TABLES
        auto s_tables = std::string(StorageInterface::SYS_TABLES);
        if (!compareTables(localStorage, remoteStorage, s_tables, blockFactory))
        {
            std::cout << "compare SYS_TABLES failed" << std::endl;
            return -1;
        }
        if (specificTable.empty())
        {
            // get all table list and compare
            std::promise<std::vector<std::string>> localKeysPromise;
            localStorage->asyncGetPrimaryKeys(s_tables, std::nullopt,
                [&localKeysPromise](Error::UniquePtr error, std::vector<std::string> keys) {
                    if (error)
                    {
                        std::cout << "get local primary keys failed: " << error << std::endl;
                        exit(1);
                    }
                    localKeysPromise.set_value(std::move(keys));
                });
            auto tableList = localKeysPromise.get_future().get();
            // iterate the list and compare every table
            for (auto& tableName : tableList)
            {
                if (!compareTables(localStorage, remoteStorage, tableName, blockFactory))
                {
                    std::cout << "compare " << tableName << " failed" << std::endl;
                    return -1;
                }
            }
        }
        else
        {
            if (!compareTables(localStorage, remoteStorage, specificTable, blockFactory))
            {
                std::cout << "compare " << specificTable << " failed" << std::endl;
                return -1;
            }
        }
        std::cout << std::endl << "compare data success, all data is same" << std::endl;
    }
    else
    {
        std::cout << "invalid parameters" << std::endl;
        std::cout << main_options << std::endl;
        return 1;
    }


    if (fs::exists(secondaryPath))
    {
        fs::remove_all(secondaryPath);
    }
    if (fs::exists(remoteSecondaryPath))
    {
        fs::remove_all(remoteSecondaryPath);
    }
    return 0;
}
