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
 * @file storage_main.cpp
 * @author: xingqiangbai
 * @date 2018-11-14
 */

#include "libinitializer/Initializer.h"
#include "libstorage/MemoryTableFactory.h"
#include <leveldb/db.h>
#include <libstorage/LevelDBStorage.h>
#include <libutilities/BasicLevelDB.h>
#include <libutilities/Common.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/program_options.hpp>


using namespace std;
using namespace bcos;
using namespace boost;
using namespace bcos::storage;
using namespace bcos::initializer;
namespace po = boost::program_options;

po::options_description main_options("Main for mini-storage");

po::variables_map initCommandLine(int argc, const char* argv[])
{
    main_options.add_options()("help,h", "help of mini-storage")("createTable,c",
        po::value<vector<string>>()->multitoken(), "[TableName] [KeyField] [ValueField]")(
        "path,p", po::value<string>()->default_value("data/"), "[LevelDB path]")(
        "select,s", po::value<vector<string>>()->multitoken(), "[TableName] [priKey]")("update,u",
        po::value<vector<string>>()->multitoken(), "[TableName] [priKey] [Key] [NewValue]")(
        "insert,i", po::value<vector<string>>()->multitoken(),
        "[TableName] [priKey] [Key]:[Value],...,[Key]:[Value]")(
        "remove,r", po::value<vector<string>>()->multitoken(), "[TableName] [priKey]");
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
    /// help information
    if (vm.count("help") || vm.count("h"))
    {
        std::cout << main_options << std::endl;
        exit(0);
    }

    return vm;
}

void help()
{
    std::cout << main_options << std::endl;
}

void printEntries(Entries::ConstPtr entries)
{
    if (entries->size() == 0)
    {
        cout << "--------------Empty!--------------" << endl;
        return;
    }
    cout << "============================" << endl;
    for (size_t i = 0; i < entries->size(); ++i)
    {
        cout << "***************" << i << "***************" << endl;
        auto data = entries->get(i);
        for (auto& it : *data)
        {
            cout << "[ " << it.first << " ]:[ " << it.second << " ]" << endl;
        }
    }
    cout << "============================" << endl;
}

int main(int argc, const char* argv[])
{
    // init log
    boost::property_tree::ptree pt;
    auto logInitializer = std::make_shared<LogInitializer>();
    logInitializer->initLog(pt);
    /// init params
    auto params = initCommandLine(argc, argv);
    auto storagePath = params["path"].as<string>();
    cout << "LevelDB path : " << storagePath << endl;
    filesystem::create_directories(storagePath);
    leveldb::Options option;
    option.create_if_missing = true;
    option.max_open_files = 1000;
    bcos::db::BasicLevelDB* dbPtr = NULL;
    leveldb::Status s = bcos::db::BasicLevelDB::Open(option, storagePath, &dbPtr);
    if (!s.ok())
    {
        cerr << "Open storage leveldb error: " << s.ToString() << endl;
        return -1;
    }

    auto storageDB = std::shared_ptr<bcos::db::BasicLevelDB>(dbPtr);
    auto storage = std::make_shared<bcos::storage::LevelDBStorage>();
    storage->setDB(storageDB);
    bcos::storage::MemoryTableFactory::Ptr memoryTableFactory =
        std::make_shared<bcos::storage::MemoryTableFactory>();
    memoryTableFactory->setStateStorage(storage);
    memoryTableFactory->setBlockHash(h256(0));
    memoryTableFactory->setBlockNum(0);

    if (params.count("createTable") || params.count("c"))
    {
        auto& p = params["createTable"].as<vector<string>>();
        cout << "createTable " << p << " || params num : " << p.size() << endl;
        if (p.size() == 3u)
        {
            auto table = memoryTableFactory->createTable(p[0], p[1], p[2], true);
            if (table)
            {
                cout << "KeyField:[" << p[1] << "]" << endl;
                cout << "ValueField:[" << p[2] << "]" << endl;
                cout << "createTable [" << p[0] << "] success!" << endl;
            }
            memoryTableFactory->commitDB(h256(0), 1);
            return 0;
        }
    }
    else if (params.count("select") || params.count("s"))
    {
        auto& p = params["select"].as<vector<string>>();
        cout << "select " << p << " || params num : " << p.size() << endl;
        if (p.size() == 2u)
        {
            auto table = memoryTableFactory->openTable(p[0]);
            if (table)
            {
                cout << "open Table [" << p[0] << "] success!" << endl;
                auto entries = table->select(p[1], table->newCondition());
                printEntries(entries);
            }
            // memoryTableFactory->commitDB(h256(0), 1);
            return 0;
        }
    }
    else if (params.count("update") || params.count("u"))
    {
        auto& p = params["update"].as<vector<string>>();
        cout << "update " << p << " || params num : " << p.size() << endl;
        if (p.size() == 4u)
        {
            auto table = memoryTableFactory->openTable(p[0]);
            if (table)
            {
                cout << "open Table [" << p[0] << "] success!" << endl;
                auto entry = table->newEntry();
                entry->setField(p[2], p[3]);
                table->update(p[1], entry, table->newCondition());
            }
            memoryTableFactory->commitDB(h256(0), 1);
            return 0;
        }
    }
    else if (params.count("insert") || params.count("i"))
    {
        auto& p = params["insert"].as<vector<string>>();
        cout << "insert " << p << " || params num : " << p.size() << endl;
        if (p.size() == 3u)
        {
            auto table = memoryTableFactory->openTable(p[0]);
            if (table)
            {
                cout << "open Table [" << p[0] << "] success!" << endl;
                auto entry = table->newEntry();
                vector<string> KVs;
                boost::split(KVs, p[2], boost::is_any_of(","));
                for (auto& kv : KVs)
                {
                    vector<string> KV;
                    boost::split(KV, kv, boost::is_any_of(":"));
                    entry->setField(KV[0], KV[1]);
                }
                table->insert(p[1], entry);
            }
            memoryTableFactory->commitDB(h256(0), 1);
            return 0;
        }
    }
    else if (params.count("remove") || params.count("r"))
    {
        auto& p = params["remove"].as<vector<string>>();
        cout << "remove " << p << " || params num : " << p.size() << endl;
        if (p.size() == 2u)
        {
            auto table = memoryTableFactory->openTable(p[0]);
            if (table)
            {
                cout << "open Table [" << p[0] << "] success!" << endl;
                table->remove(p[1], table->newCondition());
            }
            memoryTableFactory->commitDB(h256(0), 1);
            return 0;
        }
    }

    help();
    return -1;
}
