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
#include <boost/archive/binary_iarchive.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/serialization.hpp>
#include <boost/serialization/vector.hpp>
#include <fstream>

using namespace std;
using namespace dev;
using namespace boost;
using namespace dev::db;
using namespace dev::ledger;
using namespace dev::storage;
using namespace dev::initializer;
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
        "encrypt,e", po::value<vector<string>>()->multitoken(), "[encryptKey] [SMCrypto]")(
        "diff,d", po::value<vector<string>>()->multitoken(), "[db1 path] [db2 path]")(
        "list,l", po::value<vector<string>>()->multitoken(), "[db path] [table]");
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


Storage::Ptr createRocksDBStorage(const std::string& _dbPath)
{
    boost::filesystem::create_directories(_dbPath);

    std::shared_ptr<BasicRocksDB> rocksDB = std::make_shared<BasicRocksDB>();
    rocksdb::Options options;
    options.create_if_missing = true;
    options.max_open_files = 200;
    options.compression = rocksdb::kSnappyCompression;
    // any exception will cause the program to be stopped
    rocksDB->Open(options, _dbPath);
    // create and init rocksDBStorage
    std::shared_ptr<RocksDBStorage> rocksdbStorage = std::make_shared<RocksDBStorage>();
    rocksdbStorage->setDB(rocksDB);
    return rocksdbStorage;
}

void getEntries(string const& key, string const& value, vector<string>& entries)
{
    vector<map<string, string>> res;
    stringstream ss(value);
    boost::archive::binary_iarchive ia(ss);
    ia >> res;

    bool consense = (key.find(SYS_TX_HASH_2_BLOCK) == string::npos) &&
                    (key.find(SYS_CURRENT_STATE) == string::npos) &&
                    (key.find(SYS_BLOCK_2_NONCES) == string::npos);
    for (auto it = res.begin(); it != res.end(); ++it)
    {
        if ((*it)["_status_"] == "1")
        {
            // Invalid diff : the field of entry has been marked for deletion
            continue;
        }
        stringstream entryDes;
        for (auto valueIt = it->begin(); valueIt != it->end(); ++valueIt)
        {
            if (valueIt->first == "_num_")
            {
                // Invalid diff : the value of "_num_" column is different in the case of single key
                // and multiple fields
                continue;
            }
            else if (valueIt->first == "_id_" && !consense)
            {
                // Invalid diff : _id_ may be different in no consense table
                continue;
            }
            else
            {
                entryDes << valueIt->first << "," << valueIt->second << ";";
            }
        }
        entries.push_back(entryDes.str());
    }
}

bool checkKeyValid(rocksdb::DB* db, string const& key)
{
    string value;
    auto s = db->Get(rocksdb::ReadOptions(), key, &value);
    if (!s.ok())
    {
        cout << "get value error, key : " << key << endl;
        return true;
    }

    vector<map<string, string>> res;
    stringstream ss(value);
    boost::archive::binary_iarchive ia(ss);
    ia >> res;

    bool deleted = true;

    for (auto it = res.begin(); it != res.end(); ++it)
    {
        if ((*it)["_status_"] == "0")
        {
            // Invalid diff : the field of entry has been marked for deletion
            deleted = false;
            break;
        }
    }

    return !deleted;
}

void printEntry(ofstream& outfile, rocksdb::DB* db, string const& key)
{
    string value;
    auto s = db->Get(rocksdb::ReadOptions(), key, &value);
    if (!s.ok())
    {
        cout << "get value error, key : " << key << endl;
        return;
    }

    vector<map<string, string>> res;
    stringstream ss(value);
    boost::archive::binary_iarchive ia(ss);
    ia >> res;

    for (auto it = res.begin(); it != res.end(); ++it)
    {
        stringstream entryDes;
        for (auto valueIt = it->begin(); valueIt != it->end(); ++valueIt)
        {
            entryDes << "[ " << valueIt->first << "=" << valueIt->second << " ]";
        }
        outfile << entryDes.str() << endl;
    }
}

uint64_t getEntryMaxID(rocksdb::DB* db)
{
    string value;
    auto s = db->Get(rocksdb::ReadOptions(), "_sys_current_state__current_id", &value);
    if (!s.ok())
    {
        cout << "get entry max id error." << endl;
        return 0;
    }

    vector<map<string, string>> res;
    stringstream ss(value);
    boost::archive::binary_iarchive ia(ss);
    ia >> res;

    return boost::lexical_cast<size_t>(res[0]["value"]);
}

bool valueDiff(
    ofstream& outfile, string const& key, string const& value1, string const& value2, bool print)
{
    vector<string> entries1, entries2;
    getEntries(key, value1, entries1);
    getEntries(key, value2, entries2);

    vector<string> extraEntries1;
    vector<string> extraEntries2;
    auto it1 = entries1.begin();
    auto it2 = entries2.begin();
    while (it1 != entries1.end() || it2 != entries2.end())
    {
        if (it1 != entries1.end() && it2 == entries2.end())
        {
            extraEntries1.push_back(*it1);
            it1++;
        }
        else if (it1 == entries1.end() && it2 != entries2.end())
        {
            extraEntries2.push_back(*it2);
            it2++;
        }
        else if (*it1 == *it2)
        {
            it1++;
            it2++;
        }
        else if (*it1 < *it2)
        {
            extraEntries1.push_back(*it1);
            it1++;
        }
        else if (*it1 > *it2)
        {
            extraEntries2.push_back(*it2);
            it2++;
        }
    }

    if (print)
    {
        outfile << "\t------extra entries in db1------" << endl;
        for (auto entry : extraEntries1)
        {
            outfile << "\t\t" << entry << endl;
        }
        outfile << "\t------extra entries in db2------" << endl;
        for (auto entry : extraEntries2)
        {
            outfile << "\t\t" << entry << endl;
        }

        /*
        outfile << "\t------entries in db1------" << endl;
        for (auto entry : entries1)
        {
            outfile << entry << endl;
        }
        outfile << "\t ------entries in db2------" << endl;
        for (auto entry : entries2)
        {
            outfile << entry << endl;
        }
        */
    }

    return (extraEntries1.size() > 0 || extraEntries2.size() > 0);
}

void listTable(vector<string> const& paths)
{
    if (paths.size() != 2)
    {
        return;
    }
    cout << "db path : " << paths[0] << ", table : " << paths[1] << endl;

    rocksdb::DB* db;
    rocksdb::Options options;
    options.create_if_missing = false;
    rocksdb::Status status = rocksdb::DB::Open(options, paths[0], &db);
    if (!status.ok())
    {
        cout << "Open db " << paths[0] << " fail!";
        return;
    }

    string ofPath = paths[1] + ".txt";
    ofstream outfile(ofPath);
    outfile << "db path : " << paths[0] << ", table : " << paths[1] << endl;

    rocksdb::Iterator* it = db->NewIterator(rocksdb::ReadOptions());
    it->SeekToFirst();
    while (it->Valid())
    {
        if (it->key().ToString().find(paths[1]) == 0)
        {
            outfile << it->key().ToString() << endl;

            vector<map<string, string>> res;
            stringstream ss(it->value().ToString());
            boost::archive::binary_iarchive ia(ss);
            ia >> res;

            for (auto it = res.begin(); it != res.end(); ++it)
            {
                stringstream entryDes;
                for (auto valueIt = it->begin(); valueIt != it->end(); ++valueIt)
                {
                    entryDes << "[ " << valueIt->first << "=" << valueIt->second << " ]";
                }
                outfile << entryDes.str() << endl;
            }
        }
        it->Next();
    }

    outfile.close();
}

void dbDiff(vector<string> const& paths)
{
    if (paths.size() != 2)
    {
        return;
    }
    cout << "path1 : " << paths[0] << ", path2 : " << paths[1] << endl;

    rocksdb::DB* db1;
    rocksdb::DB* db2;
    rocksdb::Options options;
    options.create_if_missing = false;
    rocksdb::Status status1 = rocksdb::DB::Open(options, paths[0], &db1);
    rocksdb::Status status2 = rocksdb::DB::Open(options, paths[1], &db2);
    if (!status1.ok())
    {
        cout << "Open db " << paths[0] << " fail!";
        return;
    }
    if (!status2.ok())
    {
        cout << "Open db " << paths[1] << " fail!";
        return;
    }

    uint64_t id1 = getEntryMaxID(db1);
    uint64_t id2 = getEntryMaxID(db2);
    uint64_t maxID = id1 < id2 ? id2 : id1;
    if (g_BCOSConfig.version() >= V2_2_0)
    {
        maxID -= ENTRY_ID_START;
    }

    rocksdb::Iterator* it1 = db1->NewIterator(rocksdb::ReadOptions());
    rocksdb::Iterator* it2 = db2->NewIterator(rocksdb::ReadOptions());
    it1->SeekToFirst();
    it2->SeekToFirst();
    vector<string> extraKeys1;
    vector<string> extraKeys2;

    string ofPath = "diff.txt";
    ofstream outfile(ofPath);
    outfile << "db1 path:" << paths[0] << endl;
    outfile << "db2 path:" << paths[1] << endl;
    outfile << endl << "------different keys------" << endl;

    int64_t maxKeyCnt = 0;
    while (it1->Valid() || it2->Valid())
    {
        if (it1->Valid() && !it2->Valid())
        {
            extraKeys1.push_back(it1->key().ToString());
            it1->Next();
        }
        else if (!it1->Valid() && it2->Valid())
        {
            extraKeys2.push_back(it2->key().ToString());
            it2->Next();
        }
        else if (it1->key().ToString() == it2->key().ToString())
        {
            if (it1->value().ToString() != it2->value().ToString() &&
                valueDiff(outfile, it1->key().ToString(), it1->value().ToString(),
                    it2->value().ToString(), false))
            {
                // Invalid diff : the block/blockHeader contents are inconsistent due to the signature list
                if (it1->key().ToString().find(SYS_HASH_2_BLOCK) == string::npos && 
                    it1->key().ToString().find(SYS_HASH_2_BLOCKHEADER) == string::npos)
                {
                    outfile << "key : " << it1->key().ToString() << endl;
                    valueDiff(outfile, it1->key().ToString(), it1->value().ToString(),
                        it2->value().ToString(), true);
                }
            }
            it1->Next();
            it2->Next();
        }
        else if (it1->key().ToString() < it2->key().ToString())
        {
            extraKeys1.push_back(it1->key().ToString());
            it1->Next();
        }
        else if (it1->key().ToString() > it2->key().ToString())
        {
            extraKeys2.push_back(it2->key().ToString());
            it2->Next();
        }

        if (++maxKeyCnt % 1000 == 0)
        {
            cout << "\rComparing db , " << maxKeyCnt << "/" << maxID << " keys done" << flush;
        }
    }
    cout << "\rComparing db , " << maxKeyCnt << " keys done" << flush;
    // Invalid diff : rocksdb database does not store system tables compared to mysql data
    const std::vector<std::string> systemTableKey = {"_sys_tables___sys_block_2_nonces_",
        "_sys_tables___sys_cns_", "_sys_tables___sys_config_", "_sys_tables___sys_consensus_",
        "_sys_tables___sys_current_state_", "_sys_tables___sys_hash_2_block_",
        "_sys_tables___sys_number_2_hash_", "_sys_tables___sys_table_access_",
        "_sys_tables___sys_tables_", "_sys_tables___sys_tx_hash_2_block_", 
        "_sys_tables___sys_hash_2_header_"};

    outfile << endl << "------extra keys in db1------" << endl;
    for (auto key : extraKeys1)
    {
        if (checkKeyValid(db1, key))
        {
            if (systemTableKey.end() == find(systemTableKey.begin(), systemTableKey.end(), key))
            {
                outfile << key << endl;
                printEntry(outfile, db1, key);
            }
        }
    }
    outfile << endl << "------extra keys in db2------" << endl;
    for (auto key : extraKeys2)
    {
        if (checkKeyValid(db2, key))
        {
            if (systemTableKey.end() == find(systemTableKey.begin(), systemTableKey.end(), key))
            {
                outfile << key << endl;
                printEntry(outfile, db2, key);
            }
        }
    }

    /*
    outfile << endl << "------keys in db1------" << endl;
    for (it1->SeekToFirst(); it1->Valid(); it1->Next())
    {
        outfile << it1->key().ToString() << endl;
    }
    outfile << endl << "------keys in db2------" << endl;
    for (it2->SeekToFirst(); it2->Valid(); it2->Next())
    {
        outfile << it2->key().ToString() << endl;
    }
    */

    outfile.close();

    cout << endl << "diff rocksdb database successfully" << endl;
}

int main(int argc, const char* argv[])
{
    // init log
    boost::property_tree::ptree pt;
    auto logInitializer = std::make_shared<LogInitializer>();
    logInitializer->initLog(pt);
    /// init params
    auto params = initCommandLine(argc, argv);

    if (params.count("diff") || params.count("d"))
    {
        auto paths = params["diff"].as<vector<string>>();
        cout << "diff rocksdb database " << paths << " || params num : " << paths.size() << endl;
        dbDiff(paths);
        return 0;
    }
    else if (params.count("list") || params.count("l"))
    {
        auto paths = params["list"].as<vector<string>>();
        cout << "query table all keys " << paths << " || params num : " << paths.size() << endl;
        listTable(paths);
        return 0;
    }

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
    MemoryTableFactory2::Ptr tableFactory = std::make_shared<MemoryTableFactory2>(false);
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
                if (updatedLines >= 1) {
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
                if (insertedLines >= 1) {
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
                if (removedLines >= 1) {
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
