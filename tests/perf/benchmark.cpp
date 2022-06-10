#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/interfaces/storage/StorageInterface.h"
#include "bcos-storage/src/RocksDBStorage.h"
#include "bcos-table/src/KeyPageStorage.h"
#include "bcos-table/src/StateStorage.h"
#include "bcos-table/src/StateStorageInterface.h"
#include <boost/filesystem.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/trivial.hpp>
#include <boost/program_options.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <ostream>

using namespace std;
using namespace bcos;
using namespace bcos::crypto;
using namespace bcos::storage;

int main(int argc, const char* argv[])
{
    boost::program_options::options_description main_options("Usage of Table benchmark");
    main_options.add_options()("help,h", "print help information")("pageSize,p",
        boost::program_options::value<int>()->default_value(0),
        "sizeof page(if >0 use KeyPageStorage, else use stateStorage)")("chainLength,c",
        boost::program_options::value<int>()->default_value(1), "storage queue length")("total,t",
        boost::program_options::value<int>()->default_value(100000),
        "data set size")("onlyWrite,o", boost::program_options::value<bool>()->default_value(false),
        "only test write performance")("sorted,s",
        boost::program_options::value<bool>()->default_value(true), "use sorted data set")(
        "db,d", boost::program_options::value<int>()->default_value(0), "init db keys count");
    boost::program_options::variables_map vm;
    try
    {
        boost::program_options::store(
            boost::program_options::parse_command_line(argc, argv, main_options), vm);
    }
    catch (...)
    {
        std::cout << "invalid parameters" << std::endl;
        std::cout << main_options << std::endl;
        exit(0);
    }
    if (vm.count("help"))
    {
        std::cout << main_options << std::endl;
        exit(0);
    }
    int keyPageSize = vm["pageSize"].as<int>();
    int total = vm["total"].as<int>();
    int storageChainLength = vm["chainLength"].as<int>();
    bool onlyWrite = vm["onlyWrite"].as<bool>();
    bool sorted = vm["sorted"].as<bool>();
    int dbKeys = vm["db"].as<int>();

    storageChainLength = storageChainLength > 0 ? storageChainLength : 1;
    // set log level
    boost::log::core::get()->set_filter(
        boost::log::trivial::severity >= boost::log::trivial::error);


    // prepare data set
    int max = std::max(total, dbKeys);
    std::vector<std::string> keySet(max, "");
    std::vector<std::string> valueSet(max, "");
#pragma omp parallel for
    for (int i = 0; i < max; ++i)
    {
        keySet[i] = boost::uuids::to_string(boost::uuids::random_generator()());
        boost::erase_all(keySet[i], "-");
        valueSet[i] = boost::uuids::to_string(boost::uuids::random_generator()());
        boost::erase_all(valueSet[i], "-");
    }
    if (sorted)
    {
        std::sort(keySet.begin(), keySet.end());
        std::sort(valueSet.begin(), valueSet.end());
    }

    std::cout << "pageSize=" << keyPageSize << "|Total=" << total << "|kv size=" << keySet[0].size()
              << "|chain storage len=" << storageChainLength << std::endl
              << "use keypage=" << (keyPageSize > 0 ? "true" : "false") << std::endl;
    // create storage
    StateStorageInterface::Ptr storage = nullptr;
    auto dbPath = "./testdata/testdb";
    boost::filesystem::remove_all(dbPath);
    boost::filesystem::create_directories(dbPath);
    rocksdb::DB* db;
    rocksdb::Options options;
    options.create_if_missing = true;
    options.enable_blob_files = keyPageSize > 1 ? true : false;
    rocksdb::Status s = rocksdb::DB::Open(options, dbPath, &db);
    if (!s.ok())
    {
        std::cout << "open db failed, " << (int)s.code() << s.getState() << std::endl;
        return -1;
    }
    // insert init keys to rocksDB
    if (dbKeys > 0)
    {
        rocksdb::WriteBatch b;

#pragma omp parallel for
        for (int i = 0; i < dbKeys; ++i)
        {
#pragma omp critical
            b.Put(keySet[i], valueSet[i]);
        }
        db->Write(rocksdb::WriteOptions(), &b);
    }

    auto rocksDBStorage =
        std::make_shared<bcos::storage::RocksDBStorage>(std::unique_ptr<rocksdb::DB>(db), nullptr);

    if (keyPageSize > 0)
    {
        storage = std::make_shared<KeyPageStorage>(rocksDBStorage, keyPageSize);
    }
    else
    {
        storage = std::make_shared<StateStorage>(rocksDBStorage);
    }
    std::vector<StateStorageInterface::Ptr> storages;
    // create Table
    auto testTableName = "testTable";
    auto table = storage->createTable(testTableName, "value");
    if (!table)
    {
        std::cout << "create table failed" << std::endl;
        return -1;
    }
    // only write, hash, commit
    auto start = std::chrono::system_clock::now();
    auto perStorageKeys = total / storageChainLength;
    for (int i = 0; i < storageChainLength; ++i)
    {
        for (int j = 0; j < perStorageKeys; ++j)
        {
            auto key = keySet[j + perStorageKeys * i];
            auto entry = table->newEntry();
            entry.set(valueSet[j + perStorageKeys * i]);
            table->setRow(key, entry);
        }
        storage->setReadOnly(true);
        storages.push_back(storage);
        if (keyPageSize > 0)
        {
            storage = std::make_shared<KeyPageStorage>(storage, keyPageSize);
        }
        else
        {
            storage = std::make_shared<StateStorage>(storage);
        }
        table = storage->openTable(testTableName).value();
    }
    auto OnlyWriteEnd = std::chrono::system_clock::now();
    std::cout << "sequential write: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(OnlyWriteEnd - start).count()
              << "ms" << std::endl;
    // only read after write
    for (int i = 0; i < total && !onlyWrite; ++i)
    {
        auto key = keySet[i];
        auto entry = table->getRow(key);
        if (entry->get() != valueSet[i])
        {
            std::cout << i << " get row failed at sequential read, value wrong:" << entry->get()
                      << "!=" << valueSet[i] << std::endl;
            return -1;
        }
    }
    auto onlyWriteReadEnd = std::chrono::system_clock::now();
    // commit and read
    auto hashImpl = std::make_shared<Keccak256>();
    for (int i = 0; i < storageChainLength && !onlyWrite; ++i)
    {
        auto s = storages[i];
        s->hash(hashImpl);
        TraverseStorageInterface::Ptr t =
            std::dynamic_pointer_cast<bcos::storage::TraverseStorageInterface>(s);
        bcos::protocol::TwoPCParams p;
        rocksDBStorage->asyncPrepare(p, *t, [](bcos::Error::Ptr, uint64_t) {
            // std::cout << "asyncPrepare finished" << std::endl;
        });
        rocksDBStorage->asyncCommit(p, [](bcos::Error::Ptr) {
            // std::cout << "asyncCommit finished" << std::endl;
        });
        s.reset();
    }
    auto hashAndCommitEnd = std::chrono::system_clock::now();
    storages.clear();
    storage.reset();
    table = Table(nullptr, nullptr);
    rocksDBStorage.reset();
    db = nullptr;
    s = rocksdb::DB::Open(options, dbPath, &db);
    if (!s.ok())
    {
        std::cout << "open db failed, " << (int)s.code() << s.getState() << std::endl;
        return -1;
    }
    rocksDBStorage =
        std::make_shared<bcos::storage::RocksDBStorage>(std::unique_ptr<rocksdb::DB>(db), nullptr);
    if (keyPageSize > 0)
    {
        storage = std::make_shared<KeyPageStorage>(rocksDBStorage, keyPageSize);
    }
    else
    {
        storage = std::make_shared<StateStorage>(rocksDBStorage);
    }
    auto prepareCleanStorageEnd = std::chrono::system_clock::now();

    std::cout << "sequential read : "
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                     onlyWriteReadEnd - OnlyWriteEnd)
                     .count()
              << "ms" << std::endl
              << "hash and commit : "
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                     hashAndCommitEnd - onlyWriteReadEnd)
                     .count()
              << "ms" << std::endl;
    if (!onlyWrite)
    {  // load table meta data
        table = storage->openTable(testTableName).value();
        auto entry = table->getRow(keySet[0]);
    }
    auto loadTableMetaEnd = std::chrono::system_clock::now();
    for (int i = 0; i < total && !onlyWrite; ++i)
    {  // read
        auto entry = table->getRow(keySet[i]);
        if (entry->get() != valueSet[i])
        {
            std::cout << i << " get row failed at clean read, value wrong:" << entry->get()
                      << "!=" << valueSet[i] << std::endl;
            return -1;
        }
    }
    auto cleanReadEnd = std::chrono::system_clock::now();
    std::cout << "clean read      : "
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                     cleanReadEnd - prepareCleanStorageEnd)
                     .count()
              << "ms/"
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                     loadTableMetaEnd - prepareCleanStorageEnd)
                     .count()
              << std::endl;
    // read and write, hash, commit
    storage.reset();
    table = Table(nullptr, nullptr);
    rocksDBStorage.reset();
    boost::filesystem::remove_all(dbPath);
    boost::filesystem::create_directories(dbPath);
    s = rocksdb::DB::Open(options, dbPath, &db);
    if (!s.ok())
    {
        std::cout << "open db failed, " << (int)s.code() << s.getState() << std::endl;
        return -1;
    }
    // insert init keys to rocksDB
    if (dbKeys > 0)
    {
        rocksdb::WriteBatch b;

#pragma omp parallel for
        for (int i = 0; i < dbKeys; ++i)
        {
#pragma omp critical
            b.Put(keySet[i], valueSet[i]);
        }
        db->Write(rocksdb::WriteOptions(), &b);
    }
    rocksDBStorage =
        std::make_shared<bcos::storage::RocksDBStorage>(std::unique_ptr<rocksdb::DB>(db), nullptr);
    if (keyPageSize > 0)
    {
        storage = std::make_shared<KeyPageStorage>(rocksDBStorage, keyPageSize);
    }
    else
    {
        storage = std::make_shared<StateStorage>(rocksDBStorage);
    }
    table = storage->createTable(testTableName, "value");
    if (!table)
    {
        std::cout << "create table failed" << std::endl;
        return -1;
    }
    auto prepareCleanDBEnd = std::chrono::system_clock::now();
    std::cout << "prepareCleanDB  : "
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                     prepareCleanDBEnd - cleanReadEnd)
                     .count()
              << "ms" << std::endl;
    for (int i = 0; i < storageChainLength && !onlyWrite; ++i)
    {
        for (int j = 0; j < perStorageKeys; ++j)
        {
            auto key = keySet[j + perStorageKeys * i];
            auto entryO = table->getRow(key);
            if (entryO)
            {
                std::cout << "get duplicated key" << std::endl;
                return -1;
            }
            auto entry = table->newEntry();
            entry.set(valueSet[j + perStorageKeys * i]);
            table->setRow(key, entry);
        }
        storage->setReadOnly(true);
        storages.push_back(storage);
        if (keyPageSize > 0)
        {
            storage = std::make_shared<KeyPageStorage>(storage, keyPageSize);
        }
        else
        {
            storage = std::make_shared<StateStorage>(storage);
        }
        table = storage->openTable(testTableName).value();
    }
    auto readWriteEnd = std::chrono::system_clock::now();
    for (int i = 0; i < total && !onlyWrite; ++i)
    {
        auto key = keySet[i];
        auto entry = table->getRow(key);
        if (entry->get() != valueSet[i])
        {
            std::cout << "get row failed after write" << std::endl;
            return -1;
        }
    }
    auto readWriteReadEnd = std::chrono::system_clock::now();
    std::cout << "read write      : "
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                     readWriteEnd - prepareCleanDBEnd)
                     .count()
              << "ms" << std::endl
              << "sequential read : "
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                     readWriteReadEnd - readWriteEnd)
                     .count()
              << "ms" << std::endl;
}