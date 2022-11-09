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
#include "bcos-ledger/src/libledger/Ledger.h"
#include "bcos-ledger/src/libledger/utilities/Common.h"
#include "bcos-rpc/jsonrpc/JsonRpcImpl_2_0.h"
#include "bcos-storage/bcos-storage/TiKVStorage.h"
#include "bcos-utilities/BoostLogInitializer.h"
#include "boost/filesystem.hpp"
#include "libinitializer/ProtocolInitializer.h"
#include "libinitializer/StorageInitializer.h"
#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/slice.h"
#include "tikv_client.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-security/bcos-security/DataEncryption.h>
#include <bcos-storage/RocksDBStorage.h>
#include <bcos-table/src/KeyPageStorage.h>
#include <json/value.h>
#include <sys/queue.h>
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
#include <iterator>
#include <memory>
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
    main_options.add_options()("help,h", "help of storage tool")("start,s",
        po::value<uint64_t>()->default_value(1),
        "start block number to be archived")("end,e", po::value<uint64_t>()->default_value(0),
        "end block number to be archived, which is not included, default is current block number - "
        "1000")("config,c",
        boost::program_options::value<std::string>()->default_value("./config.ini"),
        "config file path")("genesis,g",
        boost::program_options::value<std::string>()->default_value("./config.genesis"),
        "genesis config file path")("path,p",
        boost::program_options::value<std::string>()->default_value("./archive"),
        "the path to store the archived data, require if type is rocksDB")("type,t",
        boost::program_options::value<std::string>()->default_value("rocksDB"),
        "default archive data to rocksDB, support rocksDB and TiKV")(
        "pd_addr,pd", po::value<vector<string>>()->multitoken(), "pd address of TiKV");
    po::variables_map varMap;
    try
    {
        po::store(po::parse_command_line(argc, argv, main_options), varMap);
        po::notify(varMap);
    }
    catch (...)
    {
        std::cout << "parse_command_line failed" << std::endl;
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

TransactionalStorageInterface::Ptr createBackendStorage(
    std::shared_ptr<bcos::tool::NodeConfig> nodeConfig, const std::string& logPath,
    bool write = false, const std::string& secondaryPath = "./rocksdb_secondary/")
{
    bcos::storage::TransactionalStorageInterface::Ptr storage = nullptr;
    if (boost::iequals(nodeConfig->storageType(), "RocksDB"))
    {
        bcos::security::DataEncryption::Ptr dataEncryption = nullptr;
        if (nodeConfig->storageSecurityEnable())
        {
            dataEncryption = std::make_shared<bcos::security::DataEncryption>(nodeConfig);
            dataEncryption->init();
        }
        if (write)
        {
            storage = StorageInitializer::build(
                nodeConfig->storagePath(), dataEncryption, nodeConfig->keyPageSize());
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

int main(int argc, const char* argv[])
{
    boost::property_tree::ptree propertyTree;
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

    auto startBlockNumber = params["start"].as<uint64_t>();
    auto endBlockNumber = params["end"].as<uint64_t>();
    auto archivePath = params["path"].as<std::string>();
    auto archiveType = params["type"].as<std::string>();

    std::string secondaryPath = "./rocksdb_secondary/";

    cout << "config file     : " << configPath << endl;
    cout << "genesis file    : " << genesisFilePath << endl;
    boost::property_tree::read_ini(configPath, propertyTree);
    auto logInitializer = std::make_shared<BoostLogInitializer>();
    logInitializer->initLog(propertyTree);

    // load node config
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    auto nodeConfig = std::make_shared<bcos::tool::NodeConfig>(keyFactory);
    nodeConfig->loadConfig(configPath);
    if (fs::exists(genesisFilePath))
    {
        nodeConfig->loadGenesisConfig(genesisFilePath);
    }

    // create ledger to get block data
    auto readerStorage = createBackendStorage(nodeConfig, logInitializer->logPath());
    auto protocolInitializer = std::make_shared<ProtocolInitializer>();
    protocolInitializer->init(nodeConfig);
    auto ledger =
        std::make_shared<bcos::ledger::Ledger>(protocolInitializer->blockFactory(), readerStorage);
    std::promise<uint64_t> promise;
    ledger->asyncGetBlockNumber([&promise](Error::Ptr error, bcos::protocol::BlockNumber number) {
        if (error)
        {
            cout << "get block number failed: " << error->errorMessage() << endl;
            exit(1);
        }
        promise.set_value(number);
        cout << "block number is " << number << endl;
    });
    auto currentBlockNumber = promise.get_future().get();
    if (endBlockNumber == 0)
    {
        endBlockNumber = currentBlockNumber - 1000;
    }

    // verify the start and end block number, the genesis block should not be archived and the end
    // block number should not be larger than the latest block number
    if (startBlockNumber <= 0 || startBlockNumber >= currentBlockNumber || endBlockNumber <= 0 ||
        endBlockNumber >= currentBlockNumber || startBlockNumber >= endBlockNumber)
    {
        cout << "invalid block number, start: " << startBlockNumber << ", end: " << endBlockNumber
             << ", current: " << currentBlockNumber << endl;
        return 1;
    }
    StorageInterface::Ptr storage = nullptr;
    std::function<void(std::vector<std::string> keys, std::vector<std::string> values)> writeFunc =
        nullptr;
    // create archive storage
    if (boost::iequals(archiveType, "RocksDB"))
    {
        std::shared_ptr<rocksdb::DB> rocksdb =
            bcos::initializer::StorageInitializer::createRocksDB(archivePath);
        std::cout << "write to archive rocksdb: " << archivePath << std::endl;
        writeFunc = [rocksdb](std::vector<std::string> keys, std::vector<std::string> values) {
            auto writeBatch = WriteBatch();
            for (size_t i = 0; i < keys.size(); i++)
            {
                writeBatch.Put(keys[i], values[i]);
            }
            WriteOptions options;
            auto status = rocksdb->Write(options, &writeBatch);
            if (!status.ok())
            {
                std::cerr << "write failed: " << status.ToString() << endl;
                exit(1);
            }
        };
    }
    else if (boost::iequals(archiveType, "TiKV"))
    {
#ifdef WITH_TIKV
        std::shared_ptr<tikv_client::TransactionClient> cluster =
            storage::newTiKVClient(nodeConfig->pdAddrs(), logInitializer->logPath(),
                nodeConfig->pdCaPath(), nodeConfig->pdCertPath(), nodeConfig->pdKeyPath());
        std::cout << "write to archive TiKV: " << nodeConfig->pdAddrs() << std::endl;
        writeFunc = [cluster](std::vector<std::string> keys, std::vector<std::string> values) {
            try
            {
                auto txn = cluster->begin();
                for (size_t i = 0; i < keys.size(); ++i)
                {
                    txn.put(keys[i], values[i]);
                }
                txn.commit();
            }
            catch (std::exception& e)
            {
                std::cerr << "write failed: " << e.what() << endl;
                exit(1);
            }
        };
#endif
    }
    else
    {
        std::cerr << "archive storage type not support, only support RocksDB and TiKV, type: "
                  << archiveType << std::endl;
        return 1;
    }
    // auto transactionFlag = bcos::ledger::TRANSACTIONS;
    auto receiptFlag = bcos::ledger::RECEIPTS;
    auto blockFlag = bcos::ledger::FULL_BLOCK;
    // get transaction list of block
    bool isWasm = nodeConfig->isWasm();
    bcos::crypto::Hash::Ptr hashImpl = nullptr;
    if (nodeConfig->smCryptoType())
    {
        hashImpl = std::make_shared<bcos::crypto::SM3>();
    }
    else
    {
        hashImpl = std::make_shared<::bcos::crypto::Keccak256>();
    }
    for (uint64_t i = startBlockNumber; i < endBlockNumber; ++i)
    {
        // getBlockByNumber
        std::promise<bcos::protocol::Block::Ptr> promise;
        ledger->asyncGetBlockDataByNumber(
            i, blockFlag, [&promise](Error::Ptr error, bcos::protocol::Block::Ptr block) {
                if (error)
                {
                    std::cerr << "get block failed: " << error->errorMessage() << endl;
                    exit(1);
                }
                promise.set_value(block);
            });
        auto block = promise.get_future().get();
        auto size = block->receiptsSize();
        std::vector<std::string> keys(size, "");
        std::vector<std::string> transactions(size, "");
        std::vector<std::string> receipts(size, "");
        // read the transaction and store to archive database use json format
        for (uint64_t j = 0; j < size; ++j)
        {
            auto receipt = block->receipt(j);
            auto transaction = block->transaction(j);
            auto transactionHash = transaction->hash();
            // auto receiptHash = receipt->hash();
            keys[j] = std::string((char*)transactionHash.data(), transactionHash.size());
            // write transactions and receipts to archive database
            Json::Value transactionJson;
            bcos::rpc::toJsonResp(transactionJson, transaction);
            transactions[j] = transactionJson.toStyledString();

            // read the receipt and store to archive database use json format
            Json::Value receiptJson;
            bcos::rpc::toJsonResp(receiptJson, keys[j], *receipt, isWasm, *hashImpl);
            receipts[j] = receiptJson.toStyledString();
        }
        std::cout << "write to archive database, block " << i << "\r" << std::flush;
        writeFunc(keys, transactions);
        writeFunc(std::move(keys), receipts);
    }
    std::cout << "write to archive database, block range [" << startBlockNumber << ","
              << endBlockNumber << "]" << std::endl;
    return 0;
}
