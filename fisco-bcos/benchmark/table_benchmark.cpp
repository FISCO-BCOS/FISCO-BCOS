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
 * @file table_benchmark.cpp
 * @author: xingqiangbai
 * @date 2020-03-18
 */

#include "libinitializer/Initializer.h"
#include "libledger/DBInitializer.h"
#include "libprecompiled/KVTableFactoryPrecompiled.h"
#include "libprecompiled/TableFactoryPrecompiled.h"
#include "libstorage/BasicRocksDB.h"
#include "libstorage/MemoryTableFactory.h"
#include "libstorage/MemoryTableFactoryFactory.h"
#include "libstorage/RocksDBStorage.h"
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/program_options.hpp>
#include <cstdlib>
#include <functional>

using namespace std;
using namespace bcos;
using namespace boost;
using namespace bcos::ledger;
using namespace bcos::storage;
using namespace bcos::blockverifier;
using namespace bcos::initializer;
using namespace bcos::precompiled;

namespace po = boost::program_options;

po::options_description main_options("Main for Table benchmark");

po::variables_map initCommandLine(int argc, const char* argv[])
{
    main_options.add_options()("help,h", "help of Table benchmark")("accounts,a",
        po::value<int>()->default_value(50),
        "accounts with write permission, if 0 every account has write permission")("path,p",
        po::value<string>()->default_value("benchmark/table/"),
        "[RocksDB path]")("cache,c", po::value<int>()->default_value(0),
        "memory size(MB) of CachedStorage, if 0 then no CachedStorage")(
        "keys,k", po::value<int>()->default_value(10000), "the number of different keys")("value,v",
        po::value<int>()->default_value(128),
        "the length of value")("random,r", "every test use a new rocksdb");
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
    auto logInitializer = std::make_shared<LogInitializer>();
    logInitializer->initLog(pt);
    auto params = initCommandLine(argc, argv);
    uint32_t authorityAccounts = params["accounts"].as<int>();
    auto storagePath = params["path"].as<string>();
    if (params.count("random"))
    {
        storagePath += to_string(utcTime());
    }
    auto cacheSize = params["cache"].as<int>();
    bool useCachedStorage = cacheSize == 0 ? false : true;
    auto keys = params["keys"].as<int>();
    auto valueLength = params["value"].as<int>();
    int tables = 10000;
    int64_t blockNumber = 0;

    string value;
    value.resize(valueLength);
    cout << "rocksdb path    : " << storagePath << endl;
    cout << "value length(B) : " << valueLength << endl;
    for (int i = 0; i < valueLength; ++i)
    {
        value[i] = '0' + rand() % 10;
    }

    auto rocksdbStorage = createRocksDBStorage(storagePath, bytes(), false, useCachedStorage);
    Storage::Ptr storage = rocksdbStorage;
    if (useCachedStorage)
    {
        int16_t groupID = 1;
        auto cachedStorage = std::make_shared<CachedStorage>(groupID);
        cachedStorage->setBackend(rocksdbStorage);
        cachedStorage->setMaxCapacity(cacheSize * 1024 * 1024);  // Bytes
        cachedStorage->setMaxForwardBlock(1);
        cachedStorage->init();
        storage = cachedStorage;
    }
    auto tableFactoryFactory = std::make_shared<bcos::storage::MemoryTableFactoryFactory>();
    tableFactoryFactory->setStorage(storage);
    auto tableFactory = tableFactoryFactory->newTableFactory(bcos::h256(), blockNumber);
    // create context
    auto context = std::make_shared<ExecutiveContext>();
    context->setMemoryTableFactory(tableFactory);
    auto tableFactoryPrecompiled = std::make_shared<precompiled::TableFactoryPrecompiled>();
    tableFactoryPrecompiled->setMemoryTableFactory(tableFactory);
    context->setAddress2Precompiled(Address(0x1001), tableFactoryPrecompiled);
    // BlockInfo blockInfo{h256(), 1, h256()};
    // context->setBlockInfo(blockInfo);
    string keyField("key");
    string valueFields("value,value2");
    Address origin(1);
    auto kvTableFactoryPrecompiled = std::make_shared<precompiled::KVTableFactoryPrecompiled>();
    kvTableFactoryPrecompiled->setMemoryTableFactory(tableFactory);

    auto statetable = tableFactory->openTable(SYS_CURRENT_STATE);
    auto entry = statetable->newEntry();
    entry->setField(SYS_VALUE, "0");
    entry->setField(SYS_KEY, SYS_KEY_CURRENT_NUMBER);
    statetable->insert(SYS_KEY_CURRENT_NUMBER, entry);
    tableFactory->commitDB(h256(0), blockNumber++);
    tableFactory = tableFactoryFactory->newTableFactory(bcos::h256(), blockNumber);


    auto commitData = [&](int64_t block) {
        auto statetable = tableFactory->openTable(SYS_CURRENT_STATE);
        auto entry = statetable->newEntry();
        entry->setField(SYS_VALUE, to_string(block));
        entry->setField(SYS_KEY, SYS_KEY_CURRENT_NUMBER);
        statetable->update(SYS_KEY_CURRENT_NUMBER, entry, statetable->newCondition());
        tableFactory->commitDB(h256(0), block);
        tableFactory = tableFactoryFactory->newTableFactory(bcos::h256(), block + 1);
    };
    auto setPermission = [&](const string& tableName, uint32_t accounts) {
        auto acTable = tableFactory->openTable(SYS_ACCESS_TABLE);
        for (uint32_t i = 0; i < accounts; ++i)
        {
            auto entry = acTable->newEntry();
            entry->setField("table_name", tableName);
            entry->setField("address", Address(i).hex());
            entry->setField("enable_num", boost::lexical_cast<std::string>(blockNumber));
            acTable->insert(tableName, entry, std::make_shared<AccessOptions>(origin, false));
        }
        commitData(blockNumber++);
    };

    auto performance = [&](const string& description, int count, std::function<void()> operation) {
        auto now = std::chrono::steady_clock::now();
        operation();
        std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - now;
        cout << "time used(s)=" << std::setiosflags(std::ios::fixed) << std::setprecision(3)
             << elapsed.count() << " rounds=" << count << " tps=" << count / elapsed.count() << "|"
             << description << flush;
        now = std::chrono::steady_clock::now();
        commitData(blockNumber++);
        elapsed = std::chrono::steady_clock::now() - now;
        cout << " | commit time(s)=" << elapsed.count() << endl;
    };


    auto createTable = [&](const string& prefix, int count) {
        for (int i = 0; i < count; ++i)
        {
            tableFactory->createTable(prefix + to_string(i), keyField, valueFields, true, origin);
        }
    };
    auto insert = [&](const string& tableName, const string& value, int count) {
        auto table = tableFactory->openTable(tableName);
        for (int i = 0; i < count; ++i)
        {
            auto entry = table->newEntry();
            entry->setField("value", value);
            table->insert(to_string(i), entry, std::make_shared<AccessOptions>(origin, true));
        }
    };
    auto select = [&](const string& tableName, int count) {
        auto table = tableFactory->openTable(tableName);
        for (int i = 0; i < count; ++i)
        {
            auto entries = table->select(to_string(i), table->newCondition());
            if (entries->size() == 0)
            {
                cout << "empty key:" << i << endl;
            }
        }
    };
    auto update = [&](const string& tableName, const string& value, int count) {
        auto table = tableFactory->openTable(tableName);
        for (int i = 0; i < count; ++i)
        {
            auto entry = table->newEntry();
            entry->setField("value", value);
            table->update(to_string(i), entry, table->newCondition(),
                std::make_shared<AccessOptions>(origin));
        }
    };
    auto remove = [&](const string& tableName, int count) {
        auto table = tableFactory->openTable(tableName);
        for (int i = 0; i < count; ++i)
        {
            table->remove(
                to_string(i), table->newCondition(), std::make_shared<AccessOptions>(origin));
        }
    };
    cout << "<<<<<<<<<< no permission accounts " << endl;
    performance("create Table", tables, [&]() { createTable("table", tables); });
    string testTableName("table0");
    performance("Table insert", keys, [&]() { insert(testTableName, value, keys); });
    performance("Table select", keys, [&]() { select(testTableName, keys); });
    performance("Table update", keys, [&]() { update(testTableName, value + "0", keys); });
    performance("Table remove", keys, [&]() { remove(testTableName, keys); });


    if (authorityAccounts != 0)
    {
        setPermission(SYS_TABLES, authorityAccounts);
        cout << "<<<<<<<<<< add permission accounts " << authorityAccounts << endl;
        performance("create Table", tables, [&]() { createTable("pTable", tables); });
        testTableName = "pTable0";
        setPermission(testTableName, authorityAccounts);
        performance("Table insert", keys, [&]() { insert(testTableName, value, keys); });
        performance("Table select", keys, [&]() { select(testTableName, keys); });
        performance("Table update", keys, [&]() { update(testTableName, value + "0", keys); });
        performance("Table remove", keys, [&]() { remove(testTableName, keys); });
    }

    return 0;
}
