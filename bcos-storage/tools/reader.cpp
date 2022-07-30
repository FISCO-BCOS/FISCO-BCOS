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
 * @file reader.cpp
 * @author: xingqiangbai
 * @date 2020-06-29
 * @file reader.cpp
 * @author: ancelmo
 * @date 2021-11-05
 */

#include "bcos-framework/storage/StorageInterface.h"
#include "boost/filesystem.hpp"
#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/slice.h"
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-security/bcos-security/DataEncryption.h>
#include <bcos-storage/RocksDBStorage.h>
#include <boost/algorithm/hex.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/program_options.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/throw_exception.hpp>
#include <cstdlib>
#include <functional>
#include <iterator>

using namespace std;
using namespace rocksdb;
using namespace bcos;
using namespace bcos::storage;

namespace fs = boost::filesystem;
namespace po = boost::program_options;

po::options_description main_options("Main for Table benchmark");

po::variables_map initCommandLine(int argc, const char* argv[])
{
    main_options.add_options()("help,h", "help of Table benchmark")(
        "path,p", po::value<string>()->default_value(""), "[RocksDB path]")("name,n",
        po::value<string>()->default_value(""), "[RocksDB name]")("table,t", po::value<string>(),
        "table name ")("key,k", po::value<string>()->default_value(""), "table key")(
        "iterate,i", po::value<bool>()->default_value(false), "traverse table")("config,c",
        boost::program_options::value<std::string>()->default_value("./config.ini"),
        "config file path, eg. config.ini")("genesis,g",
        boost::program_options::value<std::string>()->default_value("./config.genesis"),
        "genesis config file path, eg. genesis.ini");
    po::variables_map vm;
    try
    {
        po::store(po::parse_command_line(argc, argv, main_options), vm);
        po::notify(vm);
    }
    catch (...)
    {
        std::cout << "invalid input" << std::endl;
        exit(0);
    }
    if (vm.count("help") || vm.count("h"))
    {
        std::cout << main_options << std::endl;
        exit(0);
    }
    return vm;
}

int main(int argc, const char* argv[])
{
    boost::property_tree::ptree pt;
    auto params = initCommandLine(argc, argv);
    auto storagePath = params["path"].as<string>();
    auto storageName = params["name"].as<string>();
    if (!fs::exists(storagePath))
    {
        cout << "the path is empty:" << storagePath << endl;
        return 0;
    }
    auto iterate = params["iterate"].as<bool>();
    auto tableName = params["table"].as<string>();
    auto key = params["key"].as<string>();

    cout << "rocksdb path : " << storagePath << endl;
    cout << "tableName    : " << tableName << endl;
    // auto factory = make_shared<RocksDBAdapterFactory>(storagePath);

    rocksdb::DB* db;
    rocksdb::Options options;
    options.IncreaseParallelism();
    options.OptimizeLevelStyleCompaction();
    options.create_if_missing = false;
    rocksdb::Status s = rocksdb::DB::Open(options, storagePath, &db);

    std::string configPath("./config.ini");
    if (params.count("config"))
    {
        configPath = params["config"].as<std::string>();
    }
    if (params.count("c"))
    {
        configPath = params["c"].as<std::string>();
    }

    std::string genesisFilePath("./config.genesis");
    if (params.count("genesis"))
    {
        genesisFilePath = params["genesis"].as<std::string>();
    }
    if (params.count("g"))
    {
        genesisFilePath = params["g"].as<std::string>();
    }

    if (!boost::filesystem::exists(configPath))
    {
        std::cout << "config \'" << configPath << "\' not found!";
        exit(0);
    }

    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    auto nodeConfig = std::make_shared<bcos::tool::NodeConfig>(keyFactory);
    nodeConfig->loadConfig(configPath);
    if (true == boost::filesystem::exists(genesisFilePath))
        nodeConfig->loadGenesisConfig(genesisFilePath);

    bcos::security::DataEncryption::Ptr dataEncryption =
        std::make_shared<bcos::security::DataEncryption>(nodeConfig);
    dataEncryption->init();

    auto adapter =
        std::make_shared<RocksDBStorage>(std::unique_ptr<rocksdb::DB>(db), dataEncryption);

    if (iterate)
    {
        cout << "iterator " << tableName << endl;

        std::vector<std::string> keys;
        adapter->asyncGetPrimaryKeys(
            tableName, {}, [&](Error::Ptr error, std::vector<std::string> _keys) {
                if (error)
                {
                    BOOST_THROW_EXCEPTION(*error);
                }

                keys = std::move(_keys);
            });

        if (keys.empty())
        {
            cout << tableName << " is empty" << endl;
            return 0;
        }

        // cout << "keys=" << boost::algorithm::join(keys, "\t") << endl;
        for (auto& k : keys)
        {
            std::string hex;
            hex.reserve(k.size() * 2);
            boost::algorithm::hex_lower(k.begin(), k.end(), std::back_inserter(hex));
            cout << "key=" << hex << "|";

            std::optional<Entry> row;
            adapter->asyncGetRow(tableName, k, [&](Error::Ptr error, std::optional<Entry> entry) {
                if (error)
                {
                    BOOST_THROW_EXCEPTION(*error);
                }

                row = std::move(entry);
            });

            auto view = row->get();
            std::string hexData;
            hexData.reserve(view.size() * 2);
            boost::algorithm::hex_lower(view.begin(), view.end(), std::back_inserter(hexData));
            cout << " [" << hex << "] ";

            cout << " [status=" << row->status() << "]";
            //  << " [num=" << row->num() << "]";
            cout << endl;
        }
        return 0;
    }

    std::optional<Entry> row;
    adapter->asyncGetRow(tableName, key, [&](Error::Ptr error, std::optional<Entry> entry) {
        if (error)
        {
            BOOST_THROW_EXCEPTION(*error);
        }

        row = std::move(entry);
    });

    auto view = row->get();
    std::string hexData;
    hexData.reserve(view.size() * 2);
    boost::algorithm::hex_lower(view.begin(), view.end(), std::back_inserter(hexData));
    cout << " [" << hex << "] ";

    cout << " [status=" << row->status() << "]";
    return 0;
}
