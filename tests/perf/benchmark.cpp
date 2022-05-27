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
        boost::program_options::value<int>()->default_value(100000), "data set size")("onlyWrite,o",
        boost::program_options::value<bool>()->default_value(false), "only test write performance");
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
    storageChainLength = storageChainLength > 0 ? storageChainLength : 1;
    boost::log::core::get()->set_filter(
        boost::log::trivial::severity >= boost::log::trivial::error);
    std::cout << "pageSize=" << keyPageSize;

    // prepare data set
    std::vector<std::string> keySet(total, "");
    std::vector<std::string> valueSet(total, "");
    for (int i = 0; i < total; ++i)
    {
        keySet[i] = boost::uuids::to_string(boost::uuids::random_generator()());
        valueSet[i] = boost::uuids::to_string(boost::uuids::random_generator()());
    }

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
    auto rocksDBStorage =
        std::make_shared<bcos::storage::RocksDBStorage>(std::unique_ptr<rocksdb::DB>(db));

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
            auto key = keySet[j];
            auto entry = table->newEntry();
            entry.set(valueSet[j]);
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

    // only read after write
    for (int i = 0; i < total && !onlyWrite; ++i)
    {
        auto key = keySet[i];
        auto entry = table->getRow(key);
        if (entry->get() != valueSet[i])
        {
            std::cout << "get row failed" << std::endl;
            return -1;
        }
    }
    auto onlyWriteReadEnd = std::chrono::system_clock::now();
    // commit and read
    auto hashImpl = std::make_shared<Keccak256>();
    for (int i = 0; i < total && !onlyWrite; ++i)
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
        std::make_shared<bcos::storage::RocksDBStorage>(std::unique_ptr<rocksdb::DB>(db));
    if (keyPageSize > 0)
    {
        storage = std::make_shared<KeyPageStorage>(rocksDBStorage, keyPageSize);
    }
    else
    {
        storage = std::make_shared<StateStorage>(rocksDBStorage);
    }
    auto prepareCleanStorageEnd = std::chrono::system_clock::now();
    if (!onlyWrite)
    { // load table meta data
        table = storage->openTable(testTableName).value();
        auto entry = table->getRow(keySet[0]);
    }
    auto loadTableMetaEnd = std::chrono::system_clock::now();
    for (int i = 0; i < total && !onlyWrite; ++i)
    {  // read
        auto key = keySet[i];
        auto entry = table->getRow(key);
        if (entry->get() != valueSet[i])
        {
            std::cout << "get row failed" << std::endl;
            return -1;
        }
    }
    auto cleanReadEnd = std::chrono::system_clock::now();
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
    rocksDBStorage =
        std::make_shared<bcos::storage::RocksDBStorage>(std::unique_ptr<rocksdb::DB>(db));
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
    for (int i = 0; i < storageChainLength && !onlyWrite; ++i)
    {
        for (int j = 0; j < perStorageKeys; ++j)
        {
            auto key = keySet[j];
            auto entryO = table->getRow(key);
            if (entryO)
            {
                std::cout << "get duplicated key" << std::endl;
                return -1;
            }
            auto entry = table->newEntry();
            entry.set(valueSet[j]);
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
            std::cout << "get row failed" << std::endl;
            return -1;
        }
    }
    auto readWriteReadEnd = std::chrono::system_clock::now();
    std::cout
        << "|Total=" << total << "|kv size=" << keySet[0].size() << std::endl
        << "use keypage=" << (keyPageSize > 0 ? "true" : "false") << std::endl
        << std::endl
        << "sequential write: "
        << std::chrono::duration_cast<std::chrono::milliseconds>(OnlyWriteEnd - start).count()
        << "ms" << std::endl
        << "sequential read : "
        << std::chrono::duration_cast<std::chrono::milliseconds>(onlyWriteReadEnd - OnlyWriteEnd)
               .count()
        << "ms" << std::endl
        << "hash and commit : "
        << std::chrono::duration_cast<std::chrono::milliseconds>(
               hashAndCommitEnd - onlyWriteReadEnd)
               .count()
        << "ms" << std::endl
        << "clean read      : "
        << std::chrono::duration_cast<std::chrono::milliseconds>(
               cleanReadEnd - prepareCleanStorageEnd)
               .count()
        << "ms/"
        << std::chrono::duration_cast<std::chrono::milliseconds>(
               loadTableMetaEnd - prepareCleanStorageEnd)
               .count()
        << std::endl
        << "read write      : "
        << std::chrono::duration_cast<std::chrono::milliseconds>(readWriteEnd - prepareCleanDBEnd)
               .count()
        << "ms" << std::endl
        << "sequential read : "
        << std::chrono::duration_cast<std::chrono::milliseconds>(readWriteReadEnd - readWriteEnd)
               .count()
        << "ms" << std::endl;
}