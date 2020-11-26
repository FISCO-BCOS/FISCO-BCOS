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
#include "libledger/DBInitializer.h"
#include "libstorage/BasicRocksDB.h"
#include "libstorage/MemoryTableFactory2.h"
#include "libstorage/RocksDBStorage.h"
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/program_options.hpp>

using namespace std;
using namespace bcos;
using namespace boost;
using namespace bcos::ledger;
using namespace bcos::storage;
using namespace bcos::initializer;
namespace po = boost::program_options;

po::options_description main_options("Main for rocksdb reader");

po::variables_map initCommandLine(int argc, const char* argv[])
{
    main_options.add_options()("help,h", "help of rocksdb reader")("createTable,c",
        po::value<vector<string>>()->multitoken(), "[TableName] [KeyField] [ValueField]")(
        "path,p", po::value<string>()->default_value("data/"), "[RocksDB path]")(
        "select,s", po::value<vector<string>>()->multitoken(), "[TableName] [priKey]")("update,u",
        po::value<vector<string>>()->multitoken(),
        "[TableName] [priKey] [keyEQ] [valueEQ] [Key] [NewValue]")("insert,i",
        po::value<vector<string>>()->multitoken(),
        "[TableName] [priKey] [Key]:[Value],...,[Key]:[Value]")(
        "remove,r", po::value<vector<string>>()->multitoken(), "[TableName] [priKey]")(
        "encrypt,e", po::value<vector<string>>()->multitoken(), "[encryptKey] [SMCrypto]");
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
        cout << " is empty!" << endl;
        return;
    }
    for (size_t i = 0; i < entries->size(); ++i)
    {
        cout << endl << "***" << i << " ";
        auto data = entries->get(i);
        for (auto& it : *data)
        {
            cout << "[ " << it.first << "=" << it.second << " ]";
        }
    }
    cout << endl;
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
    cout << "DB path : " << storagePath << endl;
    // disk encrypt
    bytes encryptKey;
    if (params.count("encrypt") || params.count("e"))
    {
        auto& p = params["encrypt"].as<vector<string>>();
        cout << "encrypt " << p << " || params num : " << p.size() << endl;
        string dataKey = p[0];
        if (p.size() > 1)
        {  // sm crypto
            g_BCOSConfig.setUseSMCrypto(true);
            crypto::initSMCrypto();
            encryptKey = sm3(dataKey).asBytes();
            encryptKey = encryptKey + encryptKey + encryptKey + encryptKey;
        }
        else
        {  // keccak256
            g_BCOSConfig.setUseSMCrypto(false);
            encryptKey = keccak256(dataKey).asBytes();
        }
    }

    auto rocksdbStorage = createRocksDBStorage(storagePath, encryptKey, false, false);
    MemoryTableFactory2::Ptr tableFactory = std::make_shared<MemoryTableFactory2>();
    tableFactory->setStateStorage(rocksdbStorage);
    tableFactory->setBlockHash(h256(0));
    tableFactory->setBlockNum(0);
    tableFactory->init();
    auto commit = [tableFactory, rocksdbStorage]() {
        auto blockNumber = getBlockNumberFromStorage(rocksdbStorage);
        auto stateTable = tableFactory->openTable(SYS_CURRENT_STATE);
        auto entry = stateTable->newEntry();
        entry->setField(SYS_VALUE, to_string(blockNumber));
        entry->setField(SYS_KEY, SYS_KEY_CURRENT_NUMBER);
        stateTable->update(SYS_KEY_CURRENT_NUMBER, entry, stateTable->newCondition());
        tableFactory->commitDB(h256(0), blockNumber);
    };

    if (params.count("createTable") || params.count("c"))
    {
        auto& p = params["createTable"].as<vector<string>>();
        cout << "createTable " << p << " || params num : " << p.size() << endl;
        if (p.size() == 3u)
        {
            auto table = tableFactory->createTable(p[0], p[1], p[2], true);
            if (table)
            {
                cout << "KeyField:[" << p[1] << "]" << endl;
                cout << "ValueField:[" << p[2] << "]" << endl;
                cout << "createTable [" << p[0] << "] success!" << endl;
            }
            commit();
            return 0;
        }
    }
    else if (params.count("select") || params.count("s"))
    {
        auto& p = params["select"].as<vector<string>>();
        cout << "select " << p << " || params num : " << p.size() << endl;
        if (p.size() == 2u)
        {
            auto table = tableFactory->openTable(p[0]);
            if (table)
            {
                cout << "================ open Table [" << p[0] << "] success! key " << p[1];
                auto entries = table->select(p[1], table->newCondition());
                printEntries(entries);
            }
            return 0;
        }
    }
    else if (params.count("update") || params.count("u"))
    {
        auto& p = params["update"].as<vector<string>>();
        cout << "update " << p << " || params num : " << p.size() << endl;
        if (p.size() == 6u)
        {
            auto table = tableFactory->openTable(p[0]);
            if (table)
            {
                cout << "open Table [" << p[0] << "] success!" << endl;
                auto condition = table->newCondition();
                condition->EQ(p[2], p[3]);
                cout << "condition is [" << p[2] << "=" << p[3] << "]" << endl;
                auto entry = table->newEntry();
                entry->setField(p[4], p[5]);
                cout << "update [" << p[4] << ":" << p[5] << "]" << endl;
                int updatedLines = table->update(p[1], entry, condition);
                if (updatedLines >= 1)
                {
                    cout << "update successfully!" << endl;
                }
                commit();
            }
            return 0;
        }
    }
    else if (params.count("insert") || params.count("i"))
    {
        auto& p = params["insert"].as<vector<string>>();
        cout << "insert " << p << " || params num : " << p.size() << endl;
        if (p.size() == 3u)
        {
            auto table = tableFactory->openTable(p[0]);
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
                int insertedLines = table->insert(p[1], entry);
                if (insertedLines >= 1)
                {
                    cout << "insert successfully!" << endl;
                }
                commit();
            }
            return 0;
        }
    }
    else if (params.count("remove") || params.count("r"))
    {
        auto& p = params["remove"].as<vector<string>>();
        cout << "remove " << p << " || params num : " << p.size() << endl;
        if (p.size() == 2u)
        {
            auto table = tableFactory->openTable(p[0]);
            if (table)
            {
                cout << "open Table [" << p[0] << "] success!" << endl;
                int removedLines = table->remove(p[1], table->newCondition());
                if (removedLines >= 1)
                {
                    cout << "remove successfully!" << endl;
                }
                commit();
            }
            return 0;
        }
    }

    help();
    return -1;
}
