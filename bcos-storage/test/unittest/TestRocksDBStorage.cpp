#include "bcos-framework/storage/StorageInterface.h"
#include "bcos-table/src/StateStorage.h"
#include "boost/filesystem.hpp"
#include <bcos-storage/RocksDBStorage.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <rocksdb/write_batch.h>
#include <tbb/concurrent_vector.h>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/format.hpp>
#include <boost/format/format_fwd.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <future>
#include <optional>

using namespace bcos::storage;
using namespace std;

// ostream& operator<<(ostream& os, const std::unique_ptr<bcos::Error>& error)
// {
//     os << error->what();
//     return os;
// }

namespace bcos::test
{
class Header256Hash : public bcos::crypto::Hash
{
public:
    typedef std::shared_ptr<Header256Hash> Ptr;
    Header256Hash() = default;
    virtual ~Header256Hash(){};
    bcos::crypto::HashType hash(bytesConstRef _data) override
    {
        std::hash<std::string_view> hash;
        return bcos::crypto::HashType(
            hash(std::string_view((const char*)_data.data(), _data.size())));
    }
    bcos::crypto::hasher::AnyHasher hasher() override
    {
        return bcos::crypto::hasher::AnyHasher{bcos::crypto::hasher::openssl::OpenSSL_SM3_Hasher{}};
    }
};
struct TestRocksDBStorageFixture
{
    TestRocksDBStorageFixture()
    {
        boost::log::core::get()->set_logging_enabled(false);
        rocksdb::DB* db;
        rocksdb::Options options;
        // Optimize RocksDB. This is the easiest way to get RocksDB to perform well

        // options.IncreaseParallelism();
        // options.OptimizeLevelStyleCompaction();

        // create the DB if it's not already present
        options.create_if_missing = true;

        // open DB
        rocksdb::Status s = rocksdb::DB::Open(options, path, &db);
        BOOST_CHECK_EQUAL(s.ok(), true);

        rocksDBStorage =
            std::make_shared<RocksDBStorage>(std::unique_ptr<rocksdb::DB>(db), nullptr);
        rocksDBStorage->asyncOpenTable(testTableName, [&](auto&& error, auto&& table) {
            BOOST_CHECK(!error);

            if (error)
            {
                std::cout << boost::diagnostic_information(*error) << std::endl;
            }

            if (table)
            {
                testTableInfo = table->tableInfo();
            }
        });
        if (!testTableInfo)
        {
            std::promise<std::optional<Table>> prom;
            rocksDBStorage->asyncCreateTable(testTableName, "v1,v2,v3,v4,v5,v6",
                [&prom](Error::UniquePtr error, std::optional<Table>&& table) {
                    BOOST_CHECK_EQUAL(error.get(), nullptr);
                    BOOST_CHECK_EQUAL(table.has_value(), true);
                    prom.set_value(table);
                });
            auto table = prom.get_future().get();
            testTableInfo = table->tableInfo();
        }
    }

    void prepareTestTableData()
    {
        for (size_t i = 0; i < 1000; ++i)
        {
            std::string key = "key" + boost::lexical_cast<std::string>(i);
            Entry entry(testTableInfo);
            entry.importFields({"value_" + boost::lexical_cast<std::string>(i)});
            rocksDBStorage->asyncSetRow(testTableName, key, entry,
                [](Error::UniquePtr error) { BOOST_CHECK_EQUAL(error.get(), nullptr); });
        }
    }

    void cleanupTestTableData()
    {
        for (size_t i = 0; i < 1000; ++i)
        {
            std::string key = "key" + boost::lexical_cast<std::string>(i);
            Entry entry(testTableInfo);
            entry.setStatus(Entry::DELETED);

            rocksDBStorage->asyncSetRow(testTableName, key, entry,
                [](Error::UniquePtr error) { BOOST_CHECK_EQUAL(error.get(), nullptr); });
        }
    }

    void writeReadDeleteSingleTable(size_t count)
    {
        auto storage = rocksDBStorage;
        size_t tableEntries = count;
        auto hashImpl = std::make_shared<Header256Hash>();
        auto stateStorage = std::make_shared<bcos::storage::StateStorage>(storage);
        auto testTable = stateStorage->openTable(testTableName);
        BOOST_CHECK_EQUAL(testTable.has_value(), true);
        for (size_t i = 0; i < tableEntries; ++i)
        {
            std::string key = "key" + boost::lexical_cast<std::string>(i);
            Entry entry(testTableInfo);
            entry.importFields({"value_delete"});
            testTable->setRow(key, std::move(entry));
        }

        auto params1 = bcos::protocol::TwoPCParams();
        params1.number = 100;
        params1.primaryTableName = testTableName;
        params1.primaryTableKey = "key0";
        auto start = std::chrono::system_clock::now();
        // prewrite
        storage->asyncPrepare(params1, *stateStorage, [&](Error::Ptr error, uint64_t ts) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(ts, 0);
            params1.timestamp = ts;
        });

        // commit
        storage->asyncCommit(bcos::protocol::TwoPCParams(),
            [&](Error::Ptr error, uint64_t) { BOOST_CHECK_EQUAL(error, nullptr); });
        auto commitEnd = std::chrono::system_clock::now();
        // check commit success
        storage->asyncGetPrimaryKeys(testTableName, std::optional<storage::Condition const>(),
            [&](Error::UniquePtr error, std::vector<std::string> keys) {
                BOOST_CHECK_EQUAL(error.get(), nullptr);
                BOOST_CHECK_EQUAL(keys.size(), tableEntries);
                storage->asyncGetRows(testTableName, keys,
                    [&](Error::UniquePtr error, std::vector<std::optional<Entry>> entries) {
                        BOOST_CHECK_EQUAL(error.get(), nullptr);
                        BOOST_CHECK_EQUAL(entries.size(), tableEntries);
                        for (size_t i = 0; i < tableEntries; ++i)
                        {
                            BOOST_CHECK_EQUAL(entries[i]->getField(0), "value_delete");
                        }
                    });
            });
        auto getEnd = std::chrono::system_clock::now();

        for (size_t i = 0; i < tableEntries; ++i)
        {
            auto entry = testTable->newEntry();
            auto key = "key" + boost::lexical_cast<std::string>(i);
            entry.setStatus(Entry::DELETED);
            testTable->setRow(key, std::move(entry));
        }
        params1.timestamp = 0;
        storage->asyncPrepare(params1, *stateStorage, [&](Error::Ptr error, uint64_t ts) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(ts, 0);
            params1.timestamp = ts;
        });
        // commit
        storage->asyncCommit(bcos::protocol::TwoPCParams(),
            [&](Error::Ptr error, uint64_t) { BOOST_CHECK_EQUAL(error, nullptr); });
        auto deleteEnd = std::chrono::system_clock::now();
        // check if the data is deleted
        storage->asyncGetPrimaryKeys(testTableName, std::optional<storage::Condition const>(),
            [](Error::UniquePtr error, std::vector<std::string> keys) {
                BOOST_CHECK_EQUAL(error.get(), nullptr);
                BOOST_CHECK_EQUAL(keys.size(), 0);
            });
        cerr << "entries count=" << tableEntries << "|>>>>>>>>> commit="
             << std::chrono::duration_cast<chrono::milliseconds>(commitEnd - start).count()
             << "ms|getAll="
             << std::chrono::duration_cast<chrono::milliseconds>(getEnd - commitEnd).count()
             << "ms|deleteAll="
             << std::chrono::duration_cast<chrono::milliseconds>(deleteEnd - getEnd).count() << "ms"
             << endl
             << endl;
    }

    ~TestRocksDBStorageFixture()
    {
        if (boost::filesystem::exists(path))
        {
            boost::filesystem::remove_all(path);
        }
        boost::log::core::get()->set_logging_enabled(true);
    }

    std::string path = "./unittestdb";
    RocksDBStorage::Ptr rocksDBStorage;
    std::string testTableName = "TestTable";
    TableInfo::ConstPtr testTableInfo = nullptr;
};

BOOST_FIXTURE_TEST_SUITE(TestRocksDBStorage, TestRocksDBStorageFixture)

BOOST_AUTO_TEST_CASE(asyncGetRow)
{
    prepareTestTableData();

    tbb::concurrent_vector<std::function<void()>> checks;
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, 1050), [&](const tbb::blocked_range<size_t>& range) {
            for (size_t i = range.begin(); i != range.end(); ++i)
            {
                std::string key = "key" + boost::lexical_cast<std::string>(i);
                rocksDBStorage->asyncGetRow(
                    testTableName, key, [&](Error::UniquePtr error, std::optional<Entry> entry) {
                        BOOST_CHECK(!error);
                        if (error)
                        {
                            std::cout << boost::diagnostic_information(*error);
                        }

                        checks.push_back([i, entry]() {
                            if (i < 1000)
                            {
                                BOOST_CHECK_NE(entry.has_value(), false);
                                auto data = entry->get();
                                BOOST_CHECK_EQUAL(
                                    data, "value_" + boost::lexical_cast<std::string>(i));
                            }
                            else
                            {
                                BOOST_CHECK_EQUAL(entry.has_value(), false);
                            }
                        });
                    });
            }
        });

    for (auto& it : checks)
    {
        it();
    }

    cleanupTestTableData();
}

BOOST_AUTO_TEST_CASE(asyncGetPrimaryKeys)
{
    prepareTestTableData();
    rocksDBStorage->asyncGetPrimaryKeys(testTableName, std::optional<Condition const>(),
        [&](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(keys.size(), 1000);

            std::vector<std::string> sortedKeys;

            for (size_t i = 0; i < 1000; ++i)
            {
                sortedKeys.emplace_back("key" + boost::lexical_cast<std::string>(i));
            }

            std::sort(sortedKeys.begin(), sortedKeys.end());

            BOOST_CHECK_EQUAL_COLLECTIONS(
                sortedKeys.begin(), sortedKeys.end(), keys.begin(), keys.end());
        });

    auto tableInfo = std::make_shared<TableInfo>("new_table", vector<string>{"value"});

    for (size_t i = 1000; i < 2000; ++i)
    {
        std::string key = "newkey" + boost::lexical_cast<std::string>(i);
        auto entry = Entry(tableInfo);
        entry.importFields({"value12345"});

        rocksDBStorage->asyncSetRow(tableInfo->name(), key, entry,
            [&](Error::UniquePtr error) { BOOST_CHECK_EQUAL(error.get(), nullptr); });
    }

    // query old data
    auto condition = std::optional<Condition const>();
    rocksDBStorage->asyncGetPrimaryKeys(
        tableInfo->name(), condition, [](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(keys.size(), 1000);

            std::vector<std::string> sortedKeys;

            for (size_t i = 0; i < 1000; ++i)
            {
                sortedKeys.emplace_back("newkey" + boost::lexical_cast<std::string>(i + 1000));
            }
            std::sort(sortedKeys.begin(), sortedKeys.end());

            BOOST_CHECK_EQUAL_COLLECTIONS(
                sortedKeys.begin(), sortedKeys.end(), keys.begin(), keys.end());
        });

    // re-query non table data
    rocksDBStorage->asyncGetPrimaryKeys(
        testTableName, condition, [&](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(keys.size(), 1000);

            std::vector<std::string> sortedKeys;

            for (size_t i = 0; i < 1000; ++i)
            {
                sortedKeys.emplace_back("key" + boost::lexical_cast<std::string>(i));
            }

            std::sort(sortedKeys.begin(), sortedKeys.end());

            BOOST_CHECK_EQUAL_COLLECTIONS(
                sortedKeys.begin(), sortedKeys.end(), keys.begin(), keys.end());
        });

    rocksDBStorage->asyncGetRow(tableInfo->name(),
        "newkey" + boost::lexical_cast<std::string>(1050),
        [&](Error::UniquePtr error, std::optional<Entry> entry) {
            // SetRow doesn't need create table

            BOOST_CHECK(!error);
            BOOST_CHECK(entry);
        });

    rocksDBStorage->asyncGetRow(tableInfo->name(),
        "newkey" + boost::lexical_cast<std::string>(2000),
        [&](Error::UniquePtr error, std::optional<Entry> entry) {
            // SetRow doesn't need create table

            BOOST_CHECK(!error);
            BOOST_CHECK(!entry);
        });

    // clean new data
    for (size_t i = 0; i < 1000; ++i)
    {
        std::string key = "newkey" + boost::lexical_cast<std::string>(i + 1000);
        auto entry = Entry();
        entry.setStatus(Entry::DELETED);

        rocksDBStorage->asyncSetRow(tableInfo->name(), key, entry,
            [](Error::UniquePtr error) { BOOST_CHECK_EQUAL(error.get(), nullptr); });
    }

    rocksDBStorage->asyncGetRow(tableInfo->name(),
        "newkey" + boost::lexical_cast<std::string>(1050),
        [&](Error::UniquePtr error, std::optional<Entry> entry) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(entry.has_value(), false);
        });

    // check if the data is deleted
    rocksDBStorage->asyncGetPrimaryKeys(tableInfo->name(),
        std::optional<storage::Condition const>(),
        [](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(keys.size(), 0);
        });

    cleanupTestTableData();
}

BOOST_AUTO_TEST_CASE(asyncGetRows)
{
    prepareTestTableData();

    std::vector<std::string> keys;
    for (size_t i = 0; i < 1050; ++i)
    {
        std::string key = "key" + boost::lexical_cast<std::string>(i);
        keys.push_back(key);
    }
    rocksDBStorage->asyncGetRows(testTableName, keys,
        [&](Error::UniquePtr error, std::vector<std::optional<Entry>> entries) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(entries.size(), 1050);

            for (size_t i = 0; i < 1050; ++i)
            {
                auto& entry = entries[i];
                if (i < 1000)
                {
                    BOOST_CHECK_NE(entry.has_value(), false);
                    auto data = entry->get();
                    BOOST_CHECK_EQUAL(data, "value_" + boost::lexical_cast<std::string>(i));
                }
                else
                {
                    BOOST_CHECK_EQUAL(entry.has_value(), false);
                }
            }
        });

    cleanupTestTableData();
}

BOOST_AUTO_TEST_CASE(asyncPrepare)
{
    prepareTestTableData();

    auto hashImpl = std::make_shared<Header256Hash>();
    auto storage = std::make_shared<bcos::storage::StateStorage>(rocksDBStorage);
    BOOST_CHECK(storage->createTable("table1", "value1,value2,value3"));
    BOOST_CHECK(storage->createTable("table2", "value1,value2,value3,value4,value5"));

    auto table1 = storage->openTable("table1");
    auto table2 = storage->openTable("table2");

    BOOST_CHECK_NE(table1.has_value(), false);
    BOOST_CHECK_NE(table2.has_value(), false);

    std::vector<std::string> table1Keys;
    std::vector<std::string> table2Keys;

    for (size_t i = 0; i < 10; ++i)
    {
        auto entry = table1->newEntry();
        auto key1 = "key" + boost::lexical_cast<std::string>(i);
        entry.setField(0, "hello world!" + boost::lexical_cast<std::string>(i));
        table1->setRow(key1, entry);
        table1Keys.push_back(key1);

        auto entry2 = table2->newEntry();
        auto key2 = "key" + boost::lexical_cast<std::string>(i);
        entry2.setField(0, "hello world!abc" + boost::lexical_cast<std::string>(i));
        table2->setRow(key2, entry2);
        table2Keys.push_back(key2);
    }

    rocksDBStorage->asyncPrepare(
        bcos::protocol::TwoPCParams(), *storage, [&](Error::Ptr error, uint64_t ts) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(ts, 0);
        });
    // TODO: asyncPrepare can't be query
    rocksDBStorage->asyncCommit(bcos::protocol::TwoPCParams(),
        [&](Error::Ptr error, uint64_t) { BOOST_CHECK_EQUAL(error, nullptr); });

    rocksDBStorage->asyncGetPrimaryKeys(table1->tableInfo()->name(),
        std::optional<storage::Condition const>(),
        [&](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(keys.size(), 10);

            std::sort(table1Keys.begin(), table1Keys.end());
            BOOST_CHECK_EQUAL_COLLECTIONS(
                table1Keys.begin(), table1Keys.end(), keys.begin(), keys.end());

            rocksDBStorage->asyncGetRows(table1->tableInfo()->name(), table1Keys,
                [&](Error::UniquePtr error, std::vector<std::optional<Entry>> entries) {
                    BOOST_CHECK_EQUAL(error.get(), nullptr);
                    if (error)
                    {
                        std::cout << boost::diagnostic_information(*error) << std::endl;
                    }

                    BOOST_CHECK_EQUAL(entries.size(), 10);

                    for (size_t i = 0; i < 10; ++i)
                    {
                        BOOST_CHECK_EQUAL(entries[i]->getField(0),
                            std::string("hello world!") + table1Keys[i][3]);
                    }
                });
        });

    rocksDBStorage->asyncGetPrimaryKeys(table2->tableInfo()->name(),
        std::optional<storage::Condition const>(),
        [&](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(keys.size(), 10);

            std::sort(table2Keys.begin(), table2Keys.end());
            BOOST_CHECK_EQUAL_COLLECTIONS(
                table2Keys.begin(), table2Keys.end(), keys.begin(), keys.end());

            rocksDBStorage->asyncGetRows(table2->tableInfo()->name(), table2Keys,
                [&](Error::UniquePtr error, std::vector<std::optional<Entry>> entries) {
                    BOOST_CHECK_EQUAL(error.get(), nullptr);
                    BOOST_CHECK_EQUAL(entries.size(), 10);

                    for (size_t i = 0; i < 10; ++i)
                    {
                        BOOST_CHECK_EQUAL(entries[i]->getField(0),
                            std::string("hello world!abc") + table2Keys[i][3]);
                    }
                });
        });

    cleanupTestTableData();
}

BOOST_AUTO_TEST_CASE(boostSerialize)
{
    // encode the vector
    std::vector<std::string> forEncode(5);
    forEncode[3] = "hello world!";

    std::string buffer;
    boost::iostreams::stream<boost::iostreams::back_insert_device<std::string>> outputStream(
        buffer);
    boost::archive::binary_oarchive archive(outputStream,
        boost::archive::no_header | boost::archive::no_codecvt | boost::archive::no_tracking);

    archive << forEncode;
    outputStream.flush();

    std::cout << forEncode << std::endl;

    // decode the vector
    boost::iostreams::stream<boost::iostreams::array_source> inputStream(
        buffer.data(), buffer.size());
    boost::archive::binary_iarchive archive2(inputStream,
        boost::archive::no_header | boost::archive::no_codecvt | boost::archive::no_tracking);

    std::vector<std::string> forDecode;
    archive2 >> forDecode;

    std::cout << forDecode;

    BOOST_CHECK_EQUAL_COLLECTIONS(
        forEncode.begin(), forEncode.end(), forDecode.begin(), forDecode.end());
}

BOOST_AUTO_TEST_CASE(rocksDBiter)
{
    std::string testPath = "./iterDBTest";

    rocksdb::DB* db;
    rocksdb::Options options;
    // Optimize RocksDB. This is the easiest way to get RocksDB to perform well
    options.IncreaseParallelism();
    options.OptimizeLevelStyleCompaction();
    // create the DB if it's not already present
    options.create_if_missing = true;

    // open DB
    rocksdb::Status s = rocksdb::DB::Open(options, testPath, &db);
    BOOST_CHECK_EQUAL(s.ok(), true);

    for (uint32_t i = 0; i < 10; ++i)
    {
        rocksdb::WriteBatch writeBatch;

        for (size_t j = 0; j != 1000; ++j)
        {
            std::string key = *(bcos::toHexString(std::string((char*)&i, sizeof(i)))) + "_key_" +
                              boost::lexical_cast<std::string>(j);
            std::string value = "hello world!";

            writeBatch.Put(key, value);
        }

        db->Write(rocksdb::WriteOptions(), &writeBatch);

        rocksdb::ReadOptions read_options;
        read_options.total_order_seek = true;
        auto iter = db->NewIterator(read_options);

        size_t total = 0;
        for (iter->SeekToFirst(); iter->Valid(); iter->Next())
        {
            ++total;
        }
        delete iter;

        BOOST_CHECK_EQUAL(total, 1000 * (i + 1));
    }

    if (boost::filesystem::exists(testPath))
    {
        boost::filesystem::remove_all(testPath);
    }
}

BOOST_AUTO_TEST_CASE(writeReadDelete_1Table)
{
    writeReadDeleteSingleTable(1000);
    writeReadDeleteSingleTable(5000);
    writeReadDeleteSingleTable(10000);
    writeReadDeleteSingleTable(20000);
    writeReadDeleteSingleTable(50000);
}

BOOST_AUTO_TEST_CASE(commitAndCheck)
{
    auto initState = std::make_shared<StateStorage>(rocksDBStorage);

    initState->asyncCreateTable(
        "test_table1", "value", [](Error::UniquePtr error, std::optional<Table> table) {
            BOOST_CHECK(!error);
            if (error)
            {
                std::cout << boost::diagnostic_information(*error) << std::endl;
            }
            BOOST_CHECK(table);
        });

    initState->asyncCreateTable(
        "test_table2", "value", [](Error::UniquePtr error, std::optional<Table> table) {
            BOOST_CHECK(!error);
            if (error)
            {
                std::cout << boost::diagnostic_information(*error) << std::endl;
            }
            BOOST_CHECK(table);
        });

    for (size_t keyIndex = 0; keyIndex < 100; ++keyIndex)
    {
        Entry entry;
        entry.importFields({boost::lexical_cast<std::string>(100)});

        auto key = (boost::format("key_%d") % keyIndex).str();
        initState->asyncSetRow("test_table1", key, std::move(entry),
            [](Error::UniquePtr error) { BOOST_CHECK(!error); });
    }

    bcos::protocol::TwoPCParams params;
    params.number = 1;
    rocksDBStorage->asyncPrepare(
        params, *initState, [](Error::Ptr error, uint64_t) { BOOST_CHECK(!error); });
    rocksDBStorage->asyncCommit(params, [](Error::Ptr error, uint64_t) { BOOST_CHECK(!error); });

    STORAGE_LOG(INFO) << "Init state finished";

    for (size_t i = 100; i < 1000; i += 100)
    {
        auto state = std::make_shared<StateStorage>(rocksDBStorage);

        STORAGE_LOG(INFO) << "Expected: " << i;
        for (size_t keyIndex = 0; keyIndex < 100; ++keyIndex)
        {
            auto key = (boost::format("key_%d") % keyIndex).str();

            size_t num = 0;
            state->asyncGetRow(
                "test_table1", key, [&num](Error::UniquePtr error, std::optional<Entry> entry) {
                    BOOST_CHECK(!error);
                    num = boost::lexical_cast<size_t>(entry->getField(0));
                });

            BOOST_CHECK_EQUAL(num, i);

            num += 100;

            Entry newEntry;
            newEntry.importFields({boost::lexical_cast<std::string>(num)});

            state->asyncSetRow("test_table1", key, std::move(newEntry),
                [](Error::UniquePtr error) { BOOST_CHECK(!error); });
        }

        tbb::concurrent_vector<std::function<void()>> checks;
        auto keySet = std::make_shared<std::set<std::string>>();
        state->parallelTraverse(true, [&checks, i, keySet](const std::string_view& table,
                                          const std::string_view& key, const Entry& entry) {
            checks.push_back([tableName = std::string(table), key = std::string(key), entry = entry,
                                 i, keySet]() {
                BOOST_CHECK_EQUAL(tableName, "test_table1");
                auto [it, inserted] = keySet->emplace(key);
                boost::ignore_unused(it);
                BOOST_CHECK(inserted);
                auto num = boost::lexical_cast<size_t>(entry.getField(0));
                BOOST_CHECK_EQUAL(i + 100, num);
            });
            return true;
        });

        BOOST_CHECK_EQUAL(checks.size(), 100);
        for (auto& it : checks)
        {
            it();
        }

        bcos::protocol::TwoPCParams params;
        params.number = i;
        rocksDBStorage->asyncPrepare(
            params, *state, [](Error::Ptr error, uint64_t) { BOOST_CHECK(!error); });
        rocksDBStorage->asyncCommit(
            params, [](Error::Ptr error, uint64_t) { BOOST_CHECK(!error); });
    }
}
BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test