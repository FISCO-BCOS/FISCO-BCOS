#include "bcos-framework/interfaces/storage/StorageInterface.h"
#include "bcos-storage/src/TiKVStorage.h"
#include "bcos-table/src/StateStorage.h"
#include "boost/filesystem.hpp"
#include <bcos-utilities/DataConvertUtility.h>
#include <rocksdb/write_batch.h>
#include <tbb/concurrent_vector.h>
#include <tbb/parallel_for.h>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
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

namespace bcos::test
{
size_t total = 500;

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
};

struct TestTiKVStorageFixture
{
    TestTiKVStorageFixture()
    {
        std::vector<std::string> pd_addrs{"127.0.0.1:2379"};
        m_cluster = newTiKVCluster(pd_addrs);

        storage = std::make_shared<TiKVStorage>(m_cluster);
        storage->asyncOpenTable(testTableName, [&](auto error, auto table) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            if (table)
            {
                testTableInfo = table->tableInfo();
            }
        });
        if (!testTableInfo)
        {
            std::promise<std::optional<Table>> prom;
            storage->asyncCreateTable(testTableName, "v1,v2,v3,v4,v5,v6",
                [&prom](Error::UniquePtr error, std::optional<Table> table) {
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
        for (size_t i = 0; i < total; ++i)
        {
            std::string key = "key" + boost::lexical_cast<std::string>(i);
            Entry entry(testTableInfo);
            entry.importFields({"value_" + boost::lexical_cast<std::string>(i)});
            storage->asyncSetRow(testTableName, key, entry,
                [](Error::UniquePtr error) { BOOST_CHECK_EQUAL(error.get(), nullptr); });
        }
    }

    void cleanupTestTableData()
    {
        for (size_t i = 0; i < total; ++i)
        {
            std::string key = "key" + boost::lexical_cast<std::string>(i);
            Entry entry(testTableInfo);
            entry.setStatus(Entry::DELETED);

            storage->asyncSetRow(testTableName, key, entry,
                [](Error::UniquePtr error) { BOOST_CHECK_EQUAL(error.get(), nullptr); });
        }
    }
    void writeReadDeleteSingleTable(size_t count)
    {
        size_t tableEntries = count;
        auto hashImpl = std::make_shared<Header256Hash>();
        auto stateStorage = std::make_shared<bcos::storage::StateStorage>(storage);
        auto testTable = stateStorage->openTable(testTableName);
        BOOST_CHECK_EQUAL(testTable.has_value(), true);
        for (size_t i = 0; i < tableEntries; ++i)
        {
            std::string key = "key" + boost::lexical_cast<std::string>(i);
            Entry entry(testTableInfo);
            entry.importFields({"value_" + boost::lexical_cast<std::string>(i)});
            testTable->setRow(key, std::move(entry));
        }

        auto params1 = bcos::storage::TransactionalStorageInterface::TwoPCParams();
        params1.number = 100;
        params1.primaryTableName = testTableName;
        params1.primaryTableKey = "key0";
        auto start = std::chrono::system_clock::now();
        // prewrite
        storage->asyncPrepare(params1, *stateStorage, [&](Error::Ptr error, uint64_t ts) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_NE(ts, 0);
            params1.startTS = ts;
        });

        // commit
        storage->asyncCommit(bcos::storage::TransactionalStorageInterface::TwoPCParams(),
            [&](Error::Ptr error) { BOOST_CHECK_EQUAL(error, nullptr); });
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
                            // BOOST_CHECK_EQUAL(entries[i]->getField(0), std::string("value3"));
                            BOOST_CHECK_EQUAL(
                                entries[i]->getField(0), std::string("value_") + keys[i].substr(3));
                        }
                    });
            });
        auto getEnd = std::chrono::system_clock::now();
        // clean data
        for (size_t i = 0; i < tableEntries; ++i)
        {
            auto entry = testTable->newEntry();
            auto key = "key" + boost::lexical_cast<std::string>(i);
            entry.setStatus(Entry::DELETED);
            testTable->setRow(key, std::move(entry));
        }
        params1.startTS = 0;
        storage->asyncPrepare(params1, *stateStorage, [&](Error::Ptr error, uint64_t ts) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_NE(ts, 0);
            params1.startTS = ts;
        });
        // commit
        storage->asyncCommit(bcos::storage::TransactionalStorageInterface::TwoPCParams(),
            [&](Error::Ptr error) { BOOST_CHECK_EQUAL(error, nullptr); });
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
    ~TestTiKVStorageFixture()
    {
        if (boost::filesystem::exists(path))
        {
            boost::filesystem::remove_all(path);
        }
    }

    std::string path = "./unittestdb";
    TransactionalStorageInterface::Ptr storage;
    std::string testTableName = "TestTable";
    TableInfo::ConstPtr testTableInfo = nullptr;
    std::shared_ptr<pingcap::kv::Cluster> m_cluster;
};

BOOST_FIXTURE_TEST_SUITE(TestTiKVStorage, TestTiKVStorageFixture)

BOOST_AUTO_TEST_CASE(asyncGetRow)
{
    prepareTestTableData();

    tbb::concurrent_vector<std::function<void()>> checks;
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, 1050), [&](const tbb::blocked_range<size_t>& range) {
            for (size_t i = range.begin(); i != range.end(); ++i)
            {
                std::string key = "key" + boost::lexical_cast<std::string>(i);
                storage->asyncGetRow(
                    testTableName, key, [&](Error::UniquePtr error, std::optional<Entry> entry) {
                        BOOST_CHECK_EQUAL(error.get(), nullptr);
                        if (i < total)
                        {
                            BOOST_CHECK_EQUAL(entry.has_value(), true);
                        }
                        else
                        {
                            BOOST_CHECK_EQUAL(entry.has_value(), false);
                        }
                        checks.push_back([i, entry]() {
                            if (i < total)
                            {
                                BOOST_CHECK_NE(entry.has_value(), false);
                                auto data = entry->get();
                                auto fields =
                                    std::string("value_" + boost::lexical_cast<std::string>(i));

                                BOOST_CHECK_EQUAL(data, fields);
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
    storage->asyncGetPrimaryKeys(testTableName, std::optional<Condition const>(),
        [&](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(keys.size(), total);

            std::vector<std::string> sortedKeys;

            for (size_t i = 0; i < total; ++i)
            {
                sortedKeys.emplace_back("key" + boost::lexical_cast<std::string>(i));
            }

            std::sort(sortedKeys.begin(), sortedKeys.end());

            BOOST_CHECK_EQUAL_COLLECTIONS(
                sortedKeys.begin(), sortedKeys.end(), keys.begin(), keys.end());
        });

    auto tableInfo = std::make_shared<TableInfo>("new_table", vector<string>{"value"});

    for (size_t i = 1000; i < 1000 + total; ++i)
    {
        std::string key = "newkey" + boost::lexical_cast<std::string>(i);
        auto entry = Entry(tableInfo);
        entry.importFields({"value12345"});

        storage->asyncSetRow(tableInfo->name(), key, entry,
            [&](Error::UniquePtr error) { BOOST_CHECK_EQUAL(error.get(), nullptr); });
    }

    // query old data
    auto condition = std::optional<Condition const>();
    BOOST_CHECK_EQUAL(condition.has_value(), false);
    storage->asyncGetPrimaryKeys(
        tableInfo->name(), condition, [](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(keys.size(), total);

            std::vector<std::string> sortedKeys;

            for (size_t i = 0; i < total; ++i)
            {
                sortedKeys.emplace_back("newkey" + boost::lexical_cast<std::string>(i + 1000));
            }
            std::sort(sortedKeys.begin(), sortedKeys.end());

            BOOST_CHECK_EQUAL_COLLECTIONS(
                sortedKeys.begin(), sortedKeys.end(), keys.begin(), keys.end());
        });

    // re-query non table data
    storage->asyncGetPrimaryKeys(
        testTableName, condition, [&](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(keys.size(), total);

            std::vector<std::string> sortedKeys;

            for (size_t i = 0; i < total; ++i)
            {
                sortedKeys.emplace_back("key" + boost::lexical_cast<std::string>(i));
            }

            std::sort(sortedKeys.begin(), sortedKeys.end());

            BOOST_CHECK_EQUAL_COLLECTIONS(
                sortedKeys.begin(), sortedKeys.end(), keys.begin(), keys.end());
        });

    storage->asyncGetRow(tableInfo->name(), "newkey" + boost::lexical_cast<std::string>(1050),
        [&](Error::UniquePtr error, std::optional<Entry> entry) {
            BOOST_CHECK(!error.get());
            BOOST_CHECK(entry.has_value());
        });

    // clean new data
    for (size_t i = 0; i < total; ++i)
    {
        std::string key = "newkey" + boost::lexical_cast<std::string>(i + 1000);
        auto entry = Entry();
        entry.setStatus(Entry::DELETED);

        storage->asyncSetRow(tableInfo->name(), key, entry,
            [](Error::UniquePtr error) { BOOST_CHECK_EQUAL(error.get(), nullptr); });
    }

    storage->asyncGetRow(tableInfo->name(), "newkey" + boost::lexical_cast<std::string>(1000),
        [&](Error::UniquePtr error, std::optional<Entry> entry) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(entry.has_value(), false);
        });

    // check if the data is deleted
    storage->asyncGetPrimaryKeys(tableInfo->name(), std::optional<storage::Condition const>(),
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
    storage->asyncGetRows(testTableName, keys,
        [&](Error::UniquePtr error, std::vector<std::optional<Entry>> entries) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(entries.size(), 1050);

            for (size_t i = 0; i < 1050; ++i)
            {
                auto& entry = entries[i];
                if (i < total)
                {
                    BOOST_CHECK_EQUAL(entry.has_value(), true);

                    auto data = entry->get();
                    BOOST_CHECK_EQUAL(
                        data, std::string("value_" + boost::lexical_cast<std::string>(i)));

                    // BOOST_CHECK_EQUAL_COLLECTIONS(
                    //     data.begin(), data.end(), fields.begin(), fields.end());
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
    auto stateStorage = std::make_shared<bcos::storage::StateStorage>(storage);
    auto table1Name = "table1";
    auto table2Name = "table2";
    BOOST_CHECK_EQUAL(
        stateStorage->createTable(table1Name, "value1,value2,value3").has_value(), true);
    BOOST_CHECK_EQUAL(
        stateStorage->createTable(table2Name, "value1,value2,value3,value4,value5").has_value(),
        true);
    auto table1 = stateStorage->openTable(table1Name);
    auto table2 = stateStorage->openTable(table2Name);

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
        entry2.setField(0, "hello world!" + boost::lexical_cast<std::string>(i));
        table2->setRow(key2, entry2);
        table2Keys.push_back(key2);
    }

    storage->asyncPrepare(bcos::storage::TransactionalStorageInterface::TwoPCParams(),
        *stateStorage, [&](Error::Ptr error, uint64_t ts) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_NE(ts, 0);
        });
    storage->asyncCommit(bcos::storage::TransactionalStorageInterface::TwoPCParams(),
        [&](Error::Ptr error) { BOOST_CHECK_EQUAL(error, nullptr); });

    storage->asyncGetPrimaryKeys(table1->tableInfo()->name(),
        std::optional<storage::Condition const>(),
        [&](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(keys.size(), 10);

            std::sort(table1Keys.begin(), table1Keys.end());
            BOOST_CHECK_EQUAL_COLLECTIONS(
                table1Keys.begin(), table1Keys.end(), keys.begin(), keys.end());

            storage->asyncGetRows(table1->tableInfo()->name(), table1Keys,
                [&](Error::UniquePtr error, std::vector<std::optional<Entry>> entries) {
                    BOOST_CHECK_EQUAL(error.get(), nullptr);
                    BOOST_CHECK_EQUAL(entries.size(), 10);

                    for (size_t i = 0; i < 10; ++i)
                    {
                        BOOST_CHECK_EQUAL(entries[i]->getField(0),
                            std::string("hello world!") + table1Keys[i][3]);
                    }
                });
        });

    storage->asyncGetPrimaryKeys(table2->tableInfo()->name(),
        std::optional<storage::Condition const>(),
        [&](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(keys.size(), 10);

            std::sort(table2Keys.begin(), table2Keys.end());
            BOOST_CHECK_EQUAL_COLLECTIONS(
                table2Keys.begin(), table2Keys.end(), keys.begin(), keys.end());

            storage->asyncGetRows(table2->tableInfo()->name(), table2Keys,
                [&](Error::UniquePtr error, std::vector<std::optional<Entry>> entries) {
                    BOOST_CHECK_EQUAL(error.get(), nullptr);
                    BOOST_CHECK_EQUAL(entries.size(), 10);

                    for (size_t i = 0; i < 10; ++i)
                    {
                        BOOST_CHECK_EQUAL(entries[i]->getField(0),
                            std::string("hello world!") + table2Keys[i][3]);
                    }
                });
        });

    auto entry1 = Entry();
    entry1.setStatus(Entry::DELETED);
    storage->asyncSetRow(storage::StateStorage::SYS_TABLES, table1Name, entry1,
        [](Error::UniquePtr error) { BOOST_CHECK_EQUAL(error.get(), nullptr); });
    auto entry2 = Entry();
    entry2.setStatus(Entry::DELETED);
    storage->asyncSetRow(storage::StateStorage::SYS_TABLES, table2Name, entry2,
        [](Error::UniquePtr error) { BOOST_CHECK_EQUAL(error.get(), nullptr); });

    cleanupTestTableData();
}

BOOST_AUTO_TEST_CASE(multiStorageCommit)
{
    size_t tableEntries = 101;
    auto storage2 = std::make_shared<TiKVStorage>(m_cluster);
    auto storage3 = std::make_shared<TiKVStorage>(m_cluster);
    auto hashImpl = std::make_shared<Header256Hash>();
    auto stateStorage = std::make_shared<bcos::storage::StateStorage>(storage);
    auto testTable = stateStorage->openTable(testTableName);
    BOOST_CHECK_EQUAL(testTable.has_value(), true);
    for (size_t i = 0; i < total; ++i)
    {
        std::string key = "key" + boost::lexical_cast<std::string>(i);
        Entry entry(testTableInfo);
        entry.importFields({"value_" + boost::lexical_cast<std::string>(i)});
        testTable->setRow(key, std::move(entry));
    }
    auto stateStorage2 = std::make_shared<bcos::storage::StateStorage>(storage2);
    auto stateStorage3 = std::make_shared<bcos::storage::StateStorage>(storage3);
    auto table1Name = "table1";
    auto table2Name = "table2";
    BOOST_CHECK_EQUAL(
        stateStorage2->createTable(table1Name, "value1,value2,value3").has_value(), true);
    BOOST_CHECK_EQUAL(
        stateStorage3->createTable(table2Name, "value1,value2,value3,value4,value5").has_value(),
        true);
    auto table1 = stateStorage2->openTable(table1Name);
    auto table2 = stateStorage3->openTable(table2Name);

    BOOST_CHECK_EQUAL(table1.has_value(), true);
    BOOST_CHECK_EQUAL(table2.has_value(), true);

    std::vector<std::string> table1Keys;
    std::vector<std::string> table2Keys;

    for (size_t i = 0; i < tableEntries; ++i)
    {
        auto entry = table1->newEntry();
        auto key1 = "key" + boost::lexical_cast<std::string>(i);
        entry.setField(0, "hello world!" + boost::lexical_cast<std::string>(i));
        table1->setRow(key1, entry);
        table1Keys.push_back(key1);

        auto entry2 = table2->newEntry();
        auto key2 = "key" + boost::lexical_cast<std::string>(i);
        entry2.setField(0, "hello world!" + boost::lexical_cast<std::string>(i));
        table2->setRow(key2, entry2);
        table2Keys.push_back(key2);
    }
    auto params1 = bcos::storage::TransactionalStorageInterface::TwoPCParams();
    params1.number = 100;
    params1.primaryTableName = testTableName;
    params1.primaryTableKey = "key0";
    auto stateStorage0 = std::make_shared<bcos::storage::StateStorage>(storage);
    // check empty storage error
    storage->asyncPrepare(params1, *stateStorage0, [&](Error::Ptr error, uint64_t ts) {
        BOOST_CHECK_NE(error.get(), nullptr);
        BOOST_CHECK_EQUAL(ts, 0);
    });
    // prewrite
    storage->asyncPrepare(params1, *stateStorage, [&](Error::Ptr error, uint64_t ts) {
        BOOST_CHECK_EQUAL(error.get(), nullptr);
        BOOST_CHECK_NE(ts, 0);
        params1.startTS = ts;
        storage2->asyncPrepare(params1, *stateStorage2, [&](Error::Ptr error, uint64_t ts) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(ts, 0);
        });
        storage3->asyncPrepare(params1, *stateStorage3, [&](Error::Ptr error, uint64_t ts) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(ts, 0);
        });
    });
    // only storage call asyncCommit
    storage->asyncCommit(bcos::storage::TransactionalStorageInterface::TwoPCParams(),
        [&](Error::Ptr error) { BOOST_CHECK_EQUAL(error, nullptr); });
    // check commit success
    storage->asyncGetPrimaryKeys(table1->tableInfo()->name(),
        std::optional<storage::Condition const>(),
        [&](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(keys.size(), tableEntries);

            std::sort(table1Keys.begin(), table1Keys.end());
            BOOST_CHECK_EQUAL_COLLECTIONS(
                table1Keys.begin(), table1Keys.end(), keys.begin(), keys.end());

            storage->asyncGetRows(table1->tableInfo()->name(), table1Keys,
                [&](Error::UniquePtr error, std::vector<std::optional<Entry>> entries) {
                    BOOST_CHECK_EQUAL(error.get(), nullptr);
                    BOOST_CHECK_EQUAL(entries.size(), tableEntries);

                    for (size_t i = 0; i < tableEntries; ++i)
                    {
                        BOOST_CHECK_EQUAL(entries[i]->getField(0),
                            std::string("hello world!") + table1Keys[i].substr(3));
                    }
                });
        });

    storage->asyncGetPrimaryKeys(table2->tableInfo()->name(),
        std::optional<storage::Condition const>(),
        [&](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(keys.size(), tableEntries);

            std::sort(table2Keys.begin(), table2Keys.end());
            BOOST_CHECK_EQUAL_COLLECTIONS(
                table2Keys.begin(), table2Keys.end(), keys.begin(), keys.end());

            storage->asyncGetRows(table2->tableInfo()->name(), table2Keys,
                [&](Error::UniquePtr error, std::vector<std::optional<Entry>> entries) {
                    BOOST_CHECK_EQUAL(error.get(), nullptr);
                    BOOST_CHECK_EQUAL(entries.size(), tableEntries);

                    for (size_t i = 0; i < tableEntries; ++i)
                    {
                        BOOST_CHECK_EQUAL(entries[i]->getField(0),
                            std::string("hello world!") + table2Keys[i].substr(3));
                    }
                });
        });
    // clean data
    auto entry1 = Entry();
    entry1.setStatus(Entry::DELETED);
    storage->asyncSetRow(storage::StateStorage::SYS_TABLES, table1Name, entry1,
        [](Error::UniquePtr error) { BOOST_CHECK_EQUAL(error.get(), nullptr); });
    auto entry2 = Entry();
    entry2.setStatus(Entry::DELETED);
    storage->asyncSetRow(storage::StateStorage::SYS_TABLES, table2Name, entry2,
        [](Error::UniquePtr error) { BOOST_CHECK_EQUAL(error.get(), nullptr); });

    for (size_t i = 0; i < tableEntries; ++i)
    {
        auto entry1 = table1->newEntry();
        auto key1 = "key" + boost::lexical_cast<std::string>(i);
        entry1.setStatus(Entry::DELETED);
        storage2->asyncSetRow(table1Name, key1, entry1,
            [](Error::UniquePtr error) { BOOST_CHECK_EQUAL(error.get(), nullptr); });

        auto entry2 = table2->newEntry();
        auto key2 = "key" + boost::lexical_cast<std::string>(i);
        entry2.setStatus(Entry::DELETED);
        storage3->asyncSetRow(table2Name, key2, entry2,
            [](Error::UniquePtr error) { BOOST_CHECK_EQUAL(error.get(), nullptr); });
    }
    // check if the data is deleted
    storage->asyncGetPrimaryKeys(table1Name, std::optional<storage::Condition const>(),
        [](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(keys.size(), 0);
        });
    // check if the data is deleted
    storage->asyncGetPrimaryKeys(table2Name, std::optional<storage::Condition const>(),
        [](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(keys.size(), 0);
        });

    cleanupTestTableData();
}

BOOST_AUTO_TEST_CASE(singleStorageRollback)
{
    size_t tableEntries = 101;
    auto table1Name = "table1";
    storage->asyncGetPrimaryKeys(table1Name, std::optional<storage::Condition const>(),
        [&](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(keys.size(), 0);
        });
    auto stateStorage = std::make_shared<bcos::storage::StateStorage>(storage);
    BOOST_CHECK_EQUAL(
        stateStorage->createTable(table1Name, "value1,value2,value3").has_value(), true);
    auto table1 = stateStorage->openTable(table1Name);
    BOOST_CHECK_EQUAL(table1.has_value(), true);
    for (size_t i = 0; i < tableEntries; ++i)
    {
        auto entry = table1->newEntry();
        auto key1 = "key" + boost::lexical_cast<std::string>(i);
        entry.setField(0, "hello world!" + boost::lexical_cast<std::string>(i));
        table1->setRow(key1, entry);
    }
    auto params1 = bcos::storage::TransactionalStorageInterface::TwoPCParams();
    params1.number = 100;
    params1.primaryTableName = table1Name;
    params1.primaryTableKey = "key0";
    storage->asyncPrepare(params1, *stateStorage, [&](Error::Ptr error, uint64_t ts) {
        BOOST_CHECK_EQUAL(error.get(), nullptr);
        BOOST_CHECK_NE(ts, 0);
    });
    storage->asyncRollback(
        params1, [&](Error::Ptr error) { BOOST_CHECK_EQUAL(error.get(), nullptr); });
    storage->asyncGetPrimaryKeys(table1Name, std::optional<storage::Condition const>(),
        [&](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(keys.size(), 0);
        });
}

BOOST_AUTO_TEST_CASE(multiStorageRollback)
{
    size_t tableEntries = 101;
    auto storage2 = std::make_shared<TiKVStorage>(m_cluster);
    auto storage3 = std::make_shared<TiKVStorage>(m_cluster);
    auto hashImpl = std::make_shared<Header256Hash>();
    auto stateStorage = std::make_shared<bcos::storage::StateStorage>(storage);
    auto testTable = stateStorage->openTable(testTableName);
    BOOST_CHECK_EQUAL(testTable.has_value(), true);
    for (size_t i = 0; i < total; ++i)
    {
        std::string key = "key" + boost::lexical_cast<std::string>(i);
        Entry entry(testTableInfo);
        entry.importFields({"value_" + boost::lexical_cast<std::string>(i)});
        testTable->setRow(key, std::move(entry));
    }
    auto stateStorage2 = std::make_shared<bcos::storage::StateStorage>(storage2);
    auto stateStorage3 = std::make_shared<bcos::storage::StateStorage>(storage3);
    auto table1Name = "table1";
    auto table2Name = "table2";
    BOOST_CHECK_EQUAL(
        stateStorage2->createTable(table1Name, "value1,value2,value3").has_value(), true);
    BOOST_CHECK_EQUAL(
        stateStorage3->createTable(table2Name, "value1,value2,value3,value4,value5").has_value(),
        true);
    auto table1 = stateStorage2->openTable(table1Name);
    auto table2 = stateStorage3->openTable(table2Name);

    BOOST_CHECK_EQUAL(table1.has_value(), true);
    BOOST_CHECK_EQUAL(table2.has_value(), true);

    std::vector<std::string> table1Keys;
    std::vector<std::string> table2Keys;

    for (size_t i = 0; i < tableEntries; ++i)
    {
        auto entry = table1->newEntry();
        auto key1 = "key" + boost::lexical_cast<std::string>(i);
        entry.setField(0, "hello world!" + boost::lexical_cast<std::string>(i));
        table1->setRow(key1, entry);
        table1Keys.push_back(key1);

        auto entry2 = table2->newEntry();
        auto key2 = "key" + boost::lexical_cast<std::string>(i);
        entry2.setField(0, "hello world!" + boost::lexical_cast<std::string>(i));
        table2->setRow(key2, entry2);
        table2Keys.push_back(key2);
    }
    auto params1 = bcos::storage::TransactionalStorageInterface::TwoPCParams();
    params1.number = 100;
    params1.primaryTableName = testTableName;
    params1.primaryTableKey = "key0";
    auto stateStorage0 = std::make_shared<bcos::storage::StateStorage>(storage);
    // check empty storage error
    storage->asyncPrepare(params1, *stateStorage0, [&](Error::Ptr error, uint64_t ts) {
        BOOST_CHECK_NE(error.get(), nullptr);
        BOOST_CHECK_EQUAL(ts, 0);
    });
    // prewrite
    storage->asyncPrepare(params1, *stateStorage, [&](Error::Ptr error, uint64_t ts) {
        BOOST_CHECK_EQUAL(error.get(), nullptr);
        BOOST_CHECK_NE(ts, 0);
        params1.startTS = ts;
        storage2->asyncPrepare(params1, *stateStorage2, [&](Error::Ptr error, uint64_t ts) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(ts, 0);
        });
    });
    // all storage call asyncRollback
    storage->asyncRollback(
        params1, [&](Error::Ptr error) { BOOST_CHECK_EQUAL(error.get(), nullptr); });
    storage2->asyncRollback(
        params1, [&](Error::Ptr error) { BOOST_CHECK_EQUAL(error.get(), nullptr); });

    // std::this_thread::sleep_for(chrono::seconds(20));

    // check commit failed
    storage->asyncGetPrimaryKeys(table1Name, std::optional<storage::Condition const>(),
        [&](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(keys.size(), 0);
        });
    storage->asyncGetPrimaryKeys(table2Name, std::optional<storage::Condition const>(),
        [](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(keys.size(), 0);
        });
    storage->asyncGetPrimaryKeys(testTableName, std::optional<storage::Condition const>(),
        [](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(keys.size(), 0);
        });
}

BOOST_AUTO_TEST_CASE(multiStorageScondaryCrash)
{
    size_t tableEntries = 101;
    auto storage2 = std::make_shared<TiKVStorage>(m_cluster);
    auto storage3 = std::make_shared<TiKVStorage>(m_cluster);
    auto hashImpl = std::make_shared<Header256Hash>();
    auto stateStorage = std::make_shared<bcos::storage::StateStorage>(storage);
    auto testTable = stateStorage->openTable(testTableName);
    BOOST_CHECK_EQUAL(testTable.has_value(), true);
    for (size_t i = 0; i < total; ++i)
    {
        std::string key = "key" + boost::lexical_cast<std::string>(i);
        Entry entry(testTableInfo);
        entry.importFields({"value_" + boost::lexical_cast<std::string>(i)});
        testTable->setRow(key, std::move(entry));
    }
    auto stateStorage2 = std::make_shared<bcos::storage::StateStorage>(storage2);
    auto stateStorage3 = std::make_shared<bcos::storage::StateStorage>(storage3);
    auto table1Name = "table1";
    auto table2Name = "table2";
    BOOST_CHECK_EQUAL(
        stateStorage2->createTable(table1Name, "value1,value2,value3").has_value(), true);
    BOOST_CHECK_EQUAL(
        stateStorage3->createTable(table2Name, "value1,value2,value3,value4,value5").has_value(),
        true);
    auto table1 = stateStorage2->openTable(table1Name);
    auto table2 = stateStorage3->openTable(table2Name);

    BOOST_CHECK_EQUAL(table1.has_value(), true);
    BOOST_CHECK_EQUAL(table2.has_value(), true);

    std::vector<std::string> table1Keys;
    std::vector<std::string> table2Keys;

    for (size_t i = 0; i < tableEntries; ++i)
    {
        auto entry = table1->newEntry();
        auto key1 = "key" + boost::lexical_cast<std::string>(i);
        entry.setField(0, "hello world!" + boost::lexical_cast<std::string>(i));
        table1->setRow(key1, entry);
        table1Keys.push_back(key1);

        auto entry2 = table2->newEntry();
        auto key2 = "key" + boost::lexical_cast<std::string>(i);
        entry2.setField(0, "hello world!" + boost::lexical_cast<std::string>(i));
        table2->setRow(key2, entry2);
        table2Keys.push_back(key2);
    }
    auto params1 = bcos::storage::TransactionalStorageInterface::TwoPCParams();
    params1.number = 100;
    params1.primaryTableName = testTableName;
    params1.primaryTableKey = "key0";
    auto stateStorage0 = std::make_shared<bcos::storage::StateStorage>(storage);
    // check empty storage error
    storage->asyncPrepare(params1, *stateStorage0, [&](Error::Ptr error, uint64_t ts) {
        BOOST_CHECK_NE(error.get(), nullptr);
        BOOST_CHECK_EQUAL(ts, 0);
    });
    // prewrite
    storage->asyncPrepare(params1, *stateStorage, [&](Error::Ptr error, uint64_t ts) {
        BOOST_CHECK_EQUAL(error.get(), nullptr);
        BOOST_CHECK_NE(ts, 0);
        params1.startTS = ts;
        storage2->asyncPrepare(params1, *stateStorage2, [&](Error::Ptr error, uint64_t ts) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(ts, 0);
        });
    });
    // all storage call asyncRollback
    storage->asyncRollback(
        params1, [&](Error::Ptr error) { BOOST_CHECK_EQUAL(error.get(), nullptr); });
    storage2->asyncRollback(
        params1, [&](Error::Ptr error) { BOOST_CHECK_EQUAL(error.get(), nullptr); });

    // check commit failed
    storage->asyncGetPrimaryKeys(table1Name, std::optional<storage::Condition const>(),
        [&](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(keys.size(), 0);
        });
    storage->asyncGetPrimaryKeys(table2Name, std::optional<storage::Condition const>(),
        [](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(keys.size(), 0);
        });
    storage->asyncGetPrimaryKeys(testTableName, std::optional<storage::Condition const>(),
        [](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(keys.size(), 0);
        });

    // recall prewrite
    params1.startTS = 0;
    storage->asyncPrepare(params1, *stateStorage, [&](Error::Ptr error, uint64_t ts) {
        BOOST_CHECK_EQUAL(error.get(), nullptr);
        BOOST_CHECK_NE(ts, 0);
        params1.startTS = ts;
        storage2->asyncPrepare(params1, *stateStorage2, [&](Error::Ptr error, uint64_t ts) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(ts, 0);
        });
        storage3->asyncPrepare(params1, *stateStorage3, [&](Error::Ptr error, uint64_t ts) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(ts, 0);
        });
    });
    // only storage call asyncCommit
    storage->asyncCommit(bcos::storage::TransactionalStorageInterface::TwoPCParams(),
        [&](Error::Ptr error) { BOOST_CHECK_EQUAL(error, nullptr); });
    // check commit success
    storage->asyncGetPrimaryKeys(table1->tableInfo()->name(),
        std::optional<storage::Condition const>(),
        [&](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(keys.size(), tableEntries);

            std::sort(table1Keys.begin(), table1Keys.end());
            BOOST_CHECK_EQUAL_COLLECTIONS(
                table1Keys.begin(), table1Keys.end(), keys.begin(), keys.end());

            storage->asyncGetRows(table1->tableInfo()->name(), table1Keys,
                [&](Error::UniquePtr error, std::vector<std::optional<Entry>> entries) {
                    BOOST_CHECK_EQUAL(error.get(), nullptr);
                    BOOST_CHECK_EQUAL(entries.size(), tableEntries);

                    for (size_t i = 0; i < tableEntries; ++i)
                    {
                        BOOST_CHECK_EQUAL(entries[i]->getField(0),
                            std::string("hello world!") + table1Keys[i].substr(3));
                    }
                });
        });

    storage->asyncGetPrimaryKeys(table2->tableInfo()->name(),
        std::optional<storage::Condition const>(),
        [&](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(keys.size(), tableEntries);

            std::sort(table2Keys.begin(), table2Keys.end());
            BOOST_CHECK_EQUAL_COLLECTIONS(
                table2Keys.begin(), table2Keys.end(), keys.begin(), keys.end());

            storage->asyncGetRows(table2->tableInfo()->name(), table2Keys,
                [&](Error::UniquePtr error, std::vector<std::optional<Entry>> entries) {
                    BOOST_CHECK_EQUAL(error.get(), nullptr);
                    BOOST_CHECK_EQUAL(entries.size(), tableEntries);

                    for (size_t i = 0; i < tableEntries; ++i)
                    {
                        BOOST_CHECK_EQUAL(entries[i]->getField(0),
                            std::string("hello world!") + table2Keys[i].substr(3));
                    }
                });
        });

    storage->asyncGetPrimaryKeys(testTableName, std::optional<storage::Condition const>(),
        [&](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(keys.size(), total);
            std::sort(keys.begin(), keys.end());
            storage->asyncGetRows(testTableName, keys,
                [&](Error::UniquePtr error, std::vector<std::optional<Entry>> entries) {
                    BOOST_CHECK_EQUAL(error.get(), nullptr);
                    BOOST_CHECK_EQUAL(entries.size(), total);
                    for (size_t i = 0; i < total; ++i)
                    {
                        auto value = "value_" + keys[i].substr(3);
                        BOOST_CHECK_EQUAL(entries[i]->getField(0), value);
                    }
                });
        });

    // clean data
    auto entry1 = Entry();
    entry1.setStatus(Entry::DELETED);
    storage->asyncSetRow(storage::StateStorage::SYS_TABLES, table1Name, entry1,
        [](Error::UniquePtr error) { BOOST_CHECK_EQUAL(error.get(), nullptr); });
    auto entry2 = Entry();
    entry2.setStatus(Entry::DELETED);
    storage->asyncSetRow(storage::StateStorage::SYS_TABLES, table2Name, entry2,
        [](Error::UniquePtr error) { BOOST_CHECK_EQUAL(error.get(), nullptr); });

    for (size_t i = 0; i < tableEntries; ++i)
    {
        auto entry1 = table1->newEntry();
        auto key1 = "key" + boost::lexical_cast<std::string>(i);
        entry1.setStatus(Entry::DELETED);
        storage2->asyncSetRow(table1Name, key1, entry1,
            [](Error::UniquePtr error) { BOOST_CHECK_EQUAL(error.get(), nullptr); });

        auto entry2 = table2->newEntry();
        auto key2 = "key" + boost::lexical_cast<std::string>(i);
        entry2.setStatus(Entry::DELETED);
        storage3->asyncSetRow(table2Name, key2, entry2,
            [](Error::UniquePtr error) { BOOST_CHECK_EQUAL(error.get(), nullptr); });
    }
    // check if the data is deleted
    storage->asyncGetPrimaryKeys(table1Name, std::optional<storage::Condition const>(),
        [](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(keys.size(), 0);
        });
    // check if the data is deleted
    storage->asyncGetPrimaryKeys(table2Name, std::optional<storage::Condition const>(),
        [](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(keys.size(), 0);
        });

    cleanupTestTableData();
}

BOOST_AUTO_TEST_CASE(multiStoragePrimaryCrash)
{
    size_t tableEntries = 101;
    auto storage2 = std::make_shared<TiKVStorage>(m_cluster);
    auto storage3 = std::make_shared<TiKVStorage>(m_cluster);
    auto hashImpl = std::make_shared<Header256Hash>();
    auto stateStorage = std::make_shared<bcos::storage::StateStorage>(storage);
    auto testTable = stateStorage->openTable(testTableName);
    BOOST_CHECK_EQUAL(testTable.has_value(), true);
    for (size_t i = 0; i < total; ++i)
    {
        std::string key = "key" + boost::lexical_cast<std::string>(i);
        Entry entry(testTableInfo);
        entry.importFields({"value_" + boost::lexical_cast<std::string>(i)});
        testTable->setRow(key, std::move(entry));
    }
    auto stateStorage2 = std::make_shared<bcos::storage::StateStorage>(storage2);
    auto stateStorage3 = std::make_shared<bcos::storage::StateStorage>(storage3);
    auto table1Name = "table1";
    auto table2Name = "table2";
    BOOST_CHECK_EQUAL(
        stateStorage2->createTable(table1Name, "value1,value2,value3").has_value(), true);
    BOOST_CHECK_EQUAL(
        stateStorage3->createTable(table2Name, "value1,value2,value3,value4,value5").has_value(),
        true);
    auto table1 = stateStorage2->openTable(table1Name);
    auto table2 = stateStorage3->openTable(table2Name);

    BOOST_CHECK_EQUAL(table1.has_value(), true);
    BOOST_CHECK_EQUAL(table2.has_value(), true);

    std::vector<std::string> table1Keys;
    std::vector<std::string> table2Keys;

    for (size_t i = 0; i < tableEntries; ++i)
    {
        auto entry = table1->newEntry();
        auto key1 = "key" + boost::lexical_cast<std::string>(i);
        entry.setField(0, "hello world!" + boost::lexical_cast<std::string>(i));
        table1->setRow(key1, entry);
        table1Keys.push_back(key1);

        auto entry2 = table2->newEntry();
        auto key2 = "key" + boost::lexical_cast<std::string>(i);
        entry2.setField(0, "hello world!" + boost::lexical_cast<std::string>(i));
        table2->setRow(key2, entry2);
        table2Keys.push_back(key2);
    }
    auto params1 = bcos::storage::TransactionalStorageInterface::TwoPCParams();
    params1.number = 100;
    params1.primaryTableName = testTableName;
    params1.primaryTableKey = "key0";
    auto stateStorage0 = std::make_shared<bcos::storage::StateStorage>(storage);
    // check empty storage error
    storage->asyncPrepare(params1, *stateStorage0, [&](Error::Ptr error, uint64_t ts) {
        BOOST_CHECK_NE(error.get(), nullptr);
        BOOST_CHECK_EQUAL(ts, 0);
    });
    // prewrite
    storage->asyncPrepare(params1, *stateStorage, [&](Error::Ptr error, uint64_t ts) {
        BOOST_CHECK_EQUAL(error.get(), nullptr);
        BOOST_CHECK_NE(ts, 0);
        params1.startTS = ts;
        storage2->asyncPrepare(params1, *stateStorage2, [&](Error::Ptr error, uint64_t ts) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(ts, 0);
        });
    });
    // just recommit prewrite
    storage = std::make_shared<TiKVStorage>(m_cluster);
    auto storage4 = std::make_shared<TiKVStorage>(m_cluster);
    auto stateStorage4 = std::make_shared<bcos::storage::StateStorage>(storage3);
    params1.startTS = 0;
    storage->asyncPrepare(params1, *stateStorage, [&](Error::Ptr error, uint64_t ts) {
        BOOST_CHECK_EQUAL(error.get(), nullptr);
        BOOST_CHECK_NE(ts, 0);
        params1.startTS = ts;
        storage2->asyncPrepare(params1, *stateStorage2, [&](Error::Ptr error, uint64_t ts) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(ts, 0);
        });
        storage3->asyncPrepare(params1, *stateStorage3, [&](Error::Ptr error, uint64_t ts) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(ts, 0);
        });
        // secondary storage accept empty stateStorage
        storage4->asyncPrepare(params1, *stateStorage4, [&](Error::Ptr error, uint64_t ts) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(ts, 0);
        });
    });
    // only storage call asyncCommit
    storage->asyncCommit(bcos::storage::TransactionalStorageInterface::TwoPCParams(),
        [&](Error::Ptr error) { BOOST_CHECK_EQUAL(error, nullptr); });
    // check commit success
    storage->asyncGetPrimaryKeys(table1->tableInfo()->name(),
        std::optional<storage::Condition const>(),
        [&](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(keys.size(), tableEntries);

            std::sort(table1Keys.begin(), table1Keys.end());
            BOOST_CHECK_EQUAL_COLLECTIONS(
                table1Keys.begin(), table1Keys.end(), keys.begin(), keys.end());

            storage->asyncGetRows(table1->tableInfo()->name(), table1Keys,
                [&](Error::UniquePtr error, std::vector<std::optional<Entry>> entries) {
                    BOOST_CHECK_EQUAL(error.get(), nullptr);
                    BOOST_CHECK_EQUAL(entries.size(), tableEntries);

                    for (size_t i = 0; i < tableEntries; ++i)
                    {
                        BOOST_CHECK_EQUAL(entries[i]->getField(0),
                            std::string("hello world!") + table1Keys[i].substr(3));
                    }
                });
        });

    storage->asyncGetPrimaryKeys(table2->tableInfo()->name(),
        std::optional<storage::Condition const>(),
        [&](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(keys.size(), tableEntries);

            std::sort(table2Keys.begin(), table2Keys.end());
            BOOST_CHECK_EQUAL_COLLECTIONS(
                table2Keys.begin(), table2Keys.end(), keys.begin(), keys.end());

            storage->asyncGetRows(table2->tableInfo()->name(), table2Keys,
                [&](Error::UniquePtr error, std::vector<std::optional<Entry>> entries) {
                    BOOST_CHECK_EQUAL(error.get(), nullptr);
                    BOOST_CHECK_EQUAL(entries.size(), tableEntries);

                    for (size_t i = 0; i < tableEntries; ++i)
                    {
                        BOOST_CHECK_EQUAL(entries[i]->getField(0),
                            std::string("hello world!") + table2Keys[i].substr(3));
                    }
                });
        });

    storage->asyncGetPrimaryKeys(testTableName, std::optional<storage::Condition const>(),
        [&](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(keys.size(), total);
            storage->asyncGetRows(testTableName, keys,
                [&](Error::UniquePtr error, std::vector<std::optional<Entry>> entries) {
                    BOOST_CHECK_EQUAL(error.get(), nullptr);
                    BOOST_CHECK_EQUAL(entries.size(), total);
                    for (size_t i = 0; i < total; ++i)
                    {
                        auto value = "value_" + keys[i].substr(3);
                        BOOST_CHECK_EQUAL(entries[i]->getField(0), value);
                    }
                });
        });

    // clean data
    auto entry1 = Entry();
    entry1.setStatus(Entry::DELETED);
    storage->asyncSetRow(storage::StateStorage::SYS_TABLES, table1Name, entry1,
        [](Error::UniquePtr error) { BOOST_CHECK_EQUAL(error.get(), nullptr); });
    storage->asyncGetRow(storage::StateStorage::SYS_TABLES, table1Name,
        [](Error::UniquePtr error, std::optional<Entry> entry) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(entry.has_value(), false);
        });

    auto entry2 = Entry();
    entry2.setStatus(Entry::DELETED);
    storage->asyncSetRow(storage::StateStorage::SYS_TABLES, table2Name, entry2,
        [](Error::UniquePtr error) { BOOST_CHECK_EQUAL(error.get(), nullptr); });
    storage->asyncGetRow(storage::StateStorage::SYS_TABLES, table2Name,
        [](Error::UniquePtr error, std::optional<Entry> entry) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(entry.has_value(), false);
        });
    for (size_t i = 0; i < tableEntries; ++i)
    {
        auto entry1 = table1->newEntry();
        auto key1 = "key" + boost::lexical_cast<std::string>(i);
        entry1.setStatus(Entry::DELETED);
        storage2->asyncSetRow(table1Name, key1, entry1,
            [](Error::UniquePtr error) { BOOST_CHECK_EQUAL(error.get(), nullptr); });

        auto entry2 = table2->newEntry();
        auto key2 = "key" + boost::lexical_cast<std::string>(i);
        entry2.setStatus(Entry::DELETED);
        storage3->asyncSetRow(table2Name, key2, entry2,
            [](Error::UniquePtr error) { BOOST_CHECK_EQUAL(error.get(), nullptr); });
    }
    // check if the data is deleted
    storage->asyncGetPrimaryKeys(table1Name, std::optional<storage::Condition const>(),
        [&](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            // BOOST_CHECK_EQUAL(keys.size(), 0);
            storage->asyncGetRows(table1Name, keys,
                [&](Error::UniquePtr error, std::vector<std::optional<Entry>> entries) {
                    BOOST_CHECK_EQUAL(error.get(), nullptr);
                    BOOST_CHECK_EQUAL(entries.size(), 0);
                    // for (size_t i = 0; i < tableEntries; ++i)
                    // {
                    //     BOOST_CHECK_EQUAL(keys[i], "");
                    //     BOOST_CHECK_EQUAL(entries[i]->getField(0), "");
                    // }
                });
        });
    // check if the data is deleted
    storage->asyncGetPrimaryKeys(table2Name, std::optional<storage::Condition const>(),
        [](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_CHECK_EQUAL(error.get(), nullptr);
            BOOST_CHECK_EQUAL(keys.size(), 0);
        });

    cleanupTestTableData();
}

BOOST_AUTO_TEST_CASE(writeReadDelete_1Table)
{
    writeReadDeleteSingleTable(1000);
    writeReadDeleteSingleTable(5000);
    writeReadDeleteSingleTable(10000);
    writeReadDeleteSingleTable(20000);
    writeReadDeleteSingleTable(50000);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
