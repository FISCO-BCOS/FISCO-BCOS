/**
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
 * @brief Unit tests for KeyPageStorage
 * @file TestKeyPageStorage.cpp
 */

#include "Hash.h"
#include "bcos-framework/interfaces/storage/StorageInterface.h"
#include "bcos-table/src/KeyPageStorage.h"
#include "bcos-table/src/StateStorage.h"
#include "bcos-table/src/StateStorageInterface.h"
#include <bcos-utilities/Error.h>
#include <bcos-utilities/ThreadPool.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <tbb/concurrent_hash_map.h>
#include <tbb/concurrent_vector.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <exception>
#include <future>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <string>

using namespace std;
using namespace bcos;
using namespace bcos::storage;
using namespace bcos::crypto;

namespace std
{
inline ostream& operator<<(ostream& os, const std::optional<Entry>& entry)
{
    os << entry.has_value();
    return os;
}

inline ostream& operator<<(ostream& os, const std::optional<Table>& table)
{
    os << table.has_value();
    return os;
}

inline ostream& operator<<(ostream& os, const std::unique_ptr<Error>& error)
{
    os << error->what();
    return os;
}

inline ostream& operator<<(ostream& os, const std::tuple<std::string, crypto::HashType>& pair)
{
    os << std::get<0>(pair) << " " << std::get<1>(pair).hex();
    return os;
}
}  // namespace std

namespace bcos
{
namespace test
{
struct KeyPageStorageFixture
{
    KeyPageStorageFixture()
    {
        hashImpl = make_shared<Header256Hash>();
        auto stateStorage = make_shared<StateStorage>(nullptr);
        stateStorage->setEnableTraverse(true);
        memoryStorage = stateStorage;
        BOOST_TEST(memoryStorage != nullptr);
        tableFactory = make_shared<KeyPageStorage>(memoryStorage, false);
        BOOST_TEST(tableFactory != nullptr);
        c.limit(0, 100);
    }

    ~KeyPageStorageFixture() {}
    std::optional<Table> createDefaultTable()
    {
        std::promise<std::optional<Table>> createPromise;
        tableFactory->asyncCreateTable(
            testTableName, valueField, [&](auto&& error, std::optional<Table> table) {
                BOOST_CHECK(!error);
                createPromise.set_value(table);
            });
        return createPromise.get_future().get();
    }

    std::shared_ptr<crypto::Hash> hashImpl = nullptr;
    std::shared_ptr<StorageInterface> memoryStorage = nullptr;
    protocol::BlockNumber m_blockNumber = 0;
    std::shared_ptr<KeyPageStorage> tableFactory = nullptr;
    std::string testTableName = "t_test";
    std::string keyField = "key";
    std::string valueField = "value";
    Condition c;
};
BOOST_FIXTURE_TEST_SUITE(KeyPageStorageTest, KeyPageStorageFixture)

BOOST_AUTO_TEST_CASE(constructor)
{
    auto threadPool = ThreadPool("a", 1);
    auto tf = std::make_shared<KeyPageStorage>(memoryStorage);
}

BOOST_AUTO_TEST_CASE(create_Table)
{
    std::string tableName("t_test1");
    auto table = tableFactory->openTable(tableName);

    BOOST_TEST(!table);
    auto ret = tableFactory->createTable(tableName, valueField);
    BOOST_TEST(ret);

    table = tableFactory->openTable(tableName);
    BOOST_TEST(table);

    BOOST_CHECK_THROW(tableFactory->createTable(tableName, valueField), bcos::Error);
}

BOOST_AUTO_TEST_CASE(rollback)
{
    auto ret = createDefaultTable();
    BOOST_TEST(ret);
    auto table = tableFactory->openTable(testTableName);

    auto deleteEntry = table->newEntry();
    deleteEntry.setStatus(Entry::DELETED);
    BOOST_CHECK_NO_THROW(table->setRow("name", deleteEntry));

    auto hash = tableFactory->hash(hashImpl);

    // delete not exist entry will cause hash mismatch
#if defined(__APPLE__)
    BOOST_TEST(
        hash.hex() ==
        crypto::HashType("000000000000000000000000000000000000000000000000000000009c6498aa").hex());
#endif
    auto entry = std::make_optional(table->newEntry());
    BOOST_CHECK_NO_THROW(entry->setField(0, "Lili"));
    BOOST_CHECK_NO_THROW(table->setRow("name", *entry));

    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_TEST(
        hash.hex() ==
        crypto::HashType("00000000000000000000000000000000000000000000000000000000d25483c1").hex());
#endif
    entry = table->getRow("name");
    BOOST_TEST(entry);
    BOOST_TEST(entry->dirty() == true);
    BOOST_TEST(entry->getField(0) == "Lili");
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_TEST(
        hash.hex() ==
        crypto::HashType("00000000000000000000000000000000000000000000000000000000d25483c1").hex());
#endif
    auto savePoint = std::make_shared<Recoder>();
    tableFactory->setRecoder(savePoint);

    entry = table->newEntry();
    entry->setField(0, "12345");
    table->setRow("id", *entry);
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_TEST(
        hash.hex() ==
        crypto::HashType("000000000000000000000000000000000000000000000000000000009abc6dd2").hex());
#endif
    entry = table->getRow("id");
    BOOST_TEST(entry);
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_TEST(
        hash.hex() ==
        crypto::HashType("000000000000000000000000000000000000000000000000000000009abc6dd2").hex());
#endif
    entry = table->getRow("name");
    BOOST_TEST(entry);
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_TEST(
        hash.hex() ==
        crypto::HashType("000000000000000000000000000000000000000000000000000000009abc6dd2").hex());
#endif
    auto savePoint1 = std::make_shared<Recoder>();
    tableFactory->setRecoder(savePoint1);

    entry = table->newEntry();
    entry->setField(0, "500");
    table->setRow("balance", *entry);
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_TEST(
        hash.hex() ==
        crypto::HashType("0000000000000000000000000000000000000000000000000000000079e37b2b").hex());
#endif
    entry = table->getRow("balance");
    BOOST_TEST(entry);
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_TEST(
        hash.hex() ==
        crypto::HashType("0000000000000000000000000000000000000000000000000000000079e37b2b").hex());
#endif
    entry = table->getRow("name");
    BOOST_TEST(entry);
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_TEST(
        hash.hex() ==
        crypto::HashType("0000000000000000000000000000000000000000000000000000000079e37b2b").hex());
#endif
    auto savePoint2 = std::make_shared<Recoder>();
    tableFactory->setRecoder(savePoint2);

    auto deleteEntry2 = std::make_optional(table->newDeletedEntry());
    table->setRow("name", *deleteEntry2);
    hash = tableFactory->hash(hashImpl);

// delete entry will cause hash mismatch
#if defined(__APPLE__)
    BOOST_TEST(
        hash.hex() ==
        crypto::HashType("0000000000000000000000000000000000000000000000000000000037d36040").hex());
#endif
    entry = table->getRow("name");
    BOOST_CHECK(!entry);
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_TEST(
        hash.hex() ==
        crypto::HashType("0000000000000000000000000000000000000000000000000000000037d36040").hex());
#endif
    std::cout << "Try remove balance" << std::endl;
    tableFactory->rollback(*savePoint2);
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_TEST(
        hash.hex() ==
        crypto::HashType("0000000000000000000000000000000000000000000000000000000079e37b2b").hex());
#endif
    entry = table->getRow("name");
    BOOST_CHECK_NE(entry->status(), Entry::DELETED);
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_TEST(
        hash.hex() ==
        crypto::HashType("0000000000000000000000000000000000000000000000000000000079e37b2b").hex());
#endif
    tableFactory->rollback(*savePoint1);
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_TEST(
        hash.hex() ==
        crypto::HashType("000000000000000000000000000000000000000000000000000000009abc6dd2").hex());
#endif
    entry = table->getRow("name");
    BOOST_TEST(entry);
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_TEST(
        hash.hex() ==
        crypto::HashType("000000000000000000000000000000000000000000000000000000009abc6dd2").hex());
#endif
    entry = table->getRow("balance");
    BOOST_TEST(!entry);
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_TEST(
        hash.hex() ==
        crypto::HashType("000000000000000000000000000000000000000000000000000000009abc6dd2").hex());
#endif

    tableFactory->rollback(*savePoint);
    entry = table->getRow("name");
    BOOST_TEST(entry);
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_TEST(
        hash.hex() ==
        crypto::HashType("00000000000000000000000000000000000000000000000000000000d25483c1").hex());
#endif
    entry = table->getRow("balance");
    BOOST_TEST(!entry);
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_TEST(
        hash.hex() ==
        crypto::HashType("00000000000000000000000000000000000000000000000000000000d25483c1").hex());
#endif
    entry = table->getRow("id");
    BOOST_TEST(!entry);
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_TEST(
        hash.hex() ==
        crypto::HashType("00000000000000000000000000000000000000000000000000000000d25483c1").hex());
#endif

    // insert without version
    entry = table->newEntry();
    entry->setField(0, "new record");
    BOOST_CHECK_NO_THROW(table->setRow("id", *entry));
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_TEST(
        hash.hex() ==
        crypto::HashType("000000000000000000000000000000000000000000000000000000004f90142c").hex());
#endif

    entry = table->newDeletedEntry();
    BOOST_CHECK_NO_THROW(table->setRow("id", *entry));
    hash = tableFactory->hash(hashImpl);
    // delete entry will cause hash mismatch
#if defined(__APPLE__)
    BOOST_TEST(
        hash.hex() ==
        crypto::HashType("00000000000000000000000000000000000000000000000000000000eba89c14").hex());
#endif
}

BOOST_AUTO_TEST_CASE(rollback2)
{
    auto hash0 = tableFactory->hash(hashImpl);
    // auto savePoint0 = tableFactory->savepoint();
    auto savePoint0 = std::make_shared<Recoder>();
    tableFactory->setRecoder(savePoint0);
    BOOST_TEST(hash0 == crypto::HashType(0));
    auto ret = createDefaultTable();
    BOOST_TEST(ret);
    auto table = tableFactory->openTable(testTableName);

    auto deleteEntry = table->newDeletedEntry();
    table->setRow("name", deleteEntry);
    auto hash = tableFactory->hash(hashImpl);
// delete not exist entry will cause hash mismatch
#if defined(__APPLE__)
    BOOST_TEST(
        hash.hex() ==
        crypto::HashType("000000000000000000000000000000000000000000000000000000009c6498aa").hex());
#endif
    auto entry = std::make_optional(table->newEntry());
    // entry->setField("key", "name");
    entry->setField(0, "Lili");
    table->setRow("name", *entry);
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_TEST(
        hash.hex() ==
        crypto::HashType("00000000000000000000000000000000000000000000000000000000d25483c1").hex());
#endif
    entry = table->getRow("name");
    BOOST_TEST(entry);
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_TEST(
        hash.hex() ==
        crypto::HashType("00000000000000000000000000000000000000000000000000000000d25483c1").hex());
#endif
    // BOOST_TEST(table->dirty() == true);
    BOOST_TEST(entry->dirty() == true);
    BOOST_TEST(entry->getField(0) == "Lili");
    // auto savePoint = tableFactory->savepoint();
    auto savePoint = std::make_shared<Recoder>();
    tableFactory->setRecoder(savePoint);

    entry = table->newEntry();
    // entry->setField("key", "id");
    entry->setField(0, "12345");
    table->setRow("id", *entry);
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_TEST(
        hash.hex() ==
        crypto::HashType("000000000000000000000000000000000000000000000000000000009abc6dd2").hex());
#endif
    entry = table->getRow("id");
    BOOST_TEST(entry);
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_TEST(
        hash.hex() ==
        crypto::HashType("000000000000000000000000000000000000000000000000000000009abc6dd2").hex());
#endif
    entry = table->getRow("name");
    BOOST_TEST(entry);
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_TEST(
        hash.hex() ==
        crypto::HashType("000000000000000000000000000000000000000000000000000000009abc6dd2").hex());
#endif
    // BOOST_TEST(table->dirty() == true);

    tableFactory->rollback(*savePoint);

    entry = table->getRow("name");
    BOOST_TEST(entry);
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_TEST(
        hash.hex() ==
        crypto::HashType("00000000000000000000000000000000000000000000000000000000d25483c1").hex());
#endif
    entry = table->getRow("balance");
    BOOST_TEST(!entry);
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_TEST(
        hash.hex() ==
        crypto::HashType("00000000000000000000000000000000000000000000000000000000d25483c1").hex());
#endif
    entry = table->getRow("id");
    BOOST_TEST(!entry);
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_TEST(
        hash.hex() ==
        crypto::HashType("00000000000000000000000000000000000000000000000000000000d25483c1").hex());
#endif

    // BOOST_TEST(table->dirty() == true);
    tableFactory->rollback(*savePoint0);
    hash = tableFactory->hash(hashImpl);
    BOOST_TEST(hash.hex() == crypto::HashType("").hex());
    entry = table->getRow("name");
    BOOST_CHECK(!entry);

    auto hash00 = tableFactory->hash(hashImpl);
    BOOST_TEST(hash00 == crypto::HashType(0));

    BOOST_CHECK_EQUAL_COLLECTIONS(hash0.begin(), hash0.end(), hash00.begin(), hash00.end());
    BOOST_TEST(hash00 == hash0);
    table = tableFactory->openTable(testTableName);
    BOOST_TEST(!table);
}

BOOST_AUTO_TEST_CASE(rollback3)
{
    // Test rollback multi state storage
}

BOOST_AUTO_TEST_CASE(hash)
{
    auto ret = createDefaultTable();
    BOOST_TEST(ret);

    auto table = tableFactory->openTable(testTableName);
    auto entry = std::make_optional(table->newEntry());
    // entry->setField("key", "name");
    entry->setField(0, "Lili");
    BOOST_CHECK_NO_THROW(table->setRow("name", *entry));
    entry = table->getRow("name");
    BOOST_TEST(entry);
    auto tableFactory0 = make_shared<KeyPageStorage>(tableFactory);

    entry = std::make_optional(table->newEntry());
    // entry->setField("key", "id");
    entry->setField(0, "12345");
    BOOST_CHECK_NO_THROW(table->setRow("id", *entry));
    entry = table->getRow("id");
    BOOST_TEST(entry);
    entry = table->getRow("name");
    BOOST_TEST(entry);
    // BOOST_TEST(table->dirty() == true);
    auto keys = table->getPrimaryKeys({c});
    BOOST_TEST(keys.size() == 2);

    auto entries = table->getRows(keys);
    BOOST_TEST(entries.size() == 2);

    auto dbHash1 = tableFactory->hash(hashImpl);

    auto savePoint = std::make_shared<Recoder>();
    tableFactory->setRecoder(savePoint);
    auto idEntry = table->getRow("id");

    auto deletedEntry = std::make_optional(table->newDeletedEntry());
    BOOST_CHECK_NO_THROW(table->setRow("id", *deletedEntry));
    entry = table->getRow("id");
    BOOST_CHECK(!entry);

    tableFactory->rollback(*savePoint);
    entry = table->getRow("name");
    BOOST_TEST(entry);
    entry = table->getRow("balance");
    BOOST_TEST(!entry);
    // BOOST_TEST(table->dirty() == true);

    auto dbHash2 = tableFactory->hash(hashImpl);
    BOOST_CHECK_EQUAL(dbHash1.hex(), dbHash2.hex());

    // getPrimaryKeys and getRows
    entry = table->newEntry();
    // entry->setField("key", "id");
    entry->setField(0, "12345");
    BOOST_CHECK_NO_THROW(table->setRow("id", *entry));
    entry = table->getRow("name");
    entry->setField(0, "Wang");
    BOOST_CHECK_NO_THROW(table->setRow("name", *entry));
    entry = table->newEntry();
    // entry->setField("key", "balance");
    entry->setField(0, "12345");
    BOOST_CHECK_NO_THROW(table->setRow("balance", *entry));
    BOOST_TEST(entry);
    keys = table->getPrimaryKeys({c});
    BOOST_TEST(keys.size() == 3);

    entries = table->getRows(keys);
    BOOST_TEST(entries.size() == 3);
    entry = table->getRow("name");
    BOOST_TEST(entry);
    entry = table->getRow("balance");
    BOOST_TEST(entry);
    entry = table->getRow("balance1");
    BOOST_TEST(!entry);

    auto nameEntry = table->getRow("name");
    auto deletedEntry2 = std::make_optional(table->newDeletedEntry());
    BOOST_CHECK_NO_THROW(table->setRow("name", *deletedEntry2));
    entry = table->getRow("name");
    BOOST_CHECK(!entry);
    // BOOST_CHECK_EQUAL(entry->status(), Entry::DELETED);
    keys = table->getPrimaryKeys({c});
    BOOST_TEST(keys.size() == 2);

    entries = table->getRows(keys);
    BOOST_TEST(entries.size() == 2);

    auto idEntry2 = table->getRow("id");
    auto deletedEntry3 = std::make_optional(table->newDeletedEntry());
    BOOST_CHECK_NO_THROW(table->setRow("id", *deletedEntry3));
    entry = table->getRow("id");
    BOOST_CHECK(!entry);
    // BOOST_CHECK_EQUAL(entry->status(), Entry::DELETED);
    keys = table->getPrimaryKeys({c});
    BOOST_TEST(keys.size() == 1);

    entries = table->getRows(keys);
    BOOST_TEST(entries.size() == 1);
    // tableFactory->asyncCommit([](Error::Ptr, size_t) {});
}

BOOST_AUTO_TEST_CASE(open_sysTables)
{
    auto table = tableFactory->openTable(StorageInterface::SYS_TABLES);
    BOOST_TEST(table);
}

BOOST_AUTO_TEST_CASE(openAndCommit)
{
    auto hashImpl2 = make_shared<Header256Hash>();
    auto memoryStorage2 = make_shared<StateStorage>(nullptr);
    auto tableFactory2 = make_shared<KeyPageStorage>(memoryStorage2, false);

    for (int i = 10; i < 20; ++i)
    {
        BOOST_TEST(tableFactory2 != nullptr);

        std::string tableName = "testTable" + boost::lexical_cast<std::string>(i);
        auto key = "testKey" + boost::lexical_cast<std::string>(i);
        tableFactory2->createTable(tableName, "value");
        auto table = tableFactory2->openTable(tableName);

        auto entry = std::make_optional(table->newEntry());
        entry->setField(0, "hello world!");
        table->setRow(key, *entry);

        std::promise<bool> getRow;
        table->asyncGetRow(key, [&](auto&& error, auto&& result) {
            BOOST_CHECK(!error);
            BOOST_CHECK_EQUAL(result->getField(0), "hello world!");

            getRow.set_value(true);
        });

        getRow.get_future().get();
    }
}

BOOST_AUTO_TEST_CASE(chainLink)
{
    std::vector<KeyPageStorage::Ptr> storages;
    auto valueFields = "value1";

    auto stateStorage = make_shared<StateStorage>(nullptr);
    StateStorageInterface::Ptr prev = stateStorage;
    for (int i = 0; i < 10; ++i)
    {
        auto tableStorage = std::make_shared<KeyPageStorage>(prev, true);
        for (int j = 0; j < 10; ++j)
        {
            auto tableName = "table_" + boost::lexical_cast<std::string>(i) + "_" +
                             boost::lexical_cast<std::string>(j);
            BOOST_CHECK(tableStorage->createTable(tableName, valueFields));

            auto table = tableStorage->openTable(tableName);
            BOOST_TEST(table);

            for (int k = 0; k < 10; ++k)
            {
                auto entry = std::make_optional(table->newEntry());
                auto key =
                    boost::lexical_cast<std::string>(i) + boost::lexical_cast<std::string>(k);
                entry->setField(0, boost::lexical_cast<std::string>(i));
                BOOST_CHECK_NO_THROW(table->setRow(key, *entry));
            }
        }

        prev = tableStorage;
        storages.push_back(tableStorage);
    }

    for (int index = 0; index < 10; ++index)
    {
        auto storage = storages[index];
        storage->setReturnPage(false);
        // Data count must be 10 * 10 + 10
        std::atomic<size_t> totalCount = 0;
        storage->parallelTraverse(false, [&](auto&&, auto&&, auto&&) {
            ++totalCount;
            return true;
        });
        BOOST_CHECK_EQUAL(totalCount, 10 * 2 + 10);  // 10 for s_tables, every table has 1 page(10
                                                     // entry) and 1 meta

        // Dirty data count must be 10 * 10 + 10
        std::atomic<size_t> dirtyCount = 0;
        storage->parallelTraverse(true, [&](auto&&, auto&&, auto&&) {
            ++dirtyCount;
            return true;
        });

        BOOST_CHECK_EQUAL(dirtyCount, 10 * 2 + 10);  // 10 for s_tables, every table has 1 page(10
                                                     // entry) and 1 meta

        // Low level can't touch high level's data
        for (int i = 0; i < 10; ++i)
        {
            for (int j = 0; j < 10; ++j)
            {
                auto tableName = "table_" + boost::lexical_cast<std::string>(i) + "_" +
                                 boost::lexical_cast<std::string>(j);

                auto table = storage->openTable(tableName);
                if (i > index)
                {
                    BOOST_TEST(!table);
                }
                else
                {
                    BOOST_TEST(table);

                    for (int k = 0; k < 10; ++k)
                    {
                        auto key = boost::lexical_cast<std::string>(i) +
                                   boost::lexical_cast<std::string>(k);
                        BCOS_LOG(INFO)
                            << LOG_DESC("c") << LOG_KV("tableName", tableName) << LOG_KV("key", key)
                            << LOG_KV("index", index) << LOG_KV("i", i);
                        auto entry = table->getRow(key);
                        if (i > index)
                        {
                            BOOST_TEST(!entry);
                        }
                        else
                        {
                            BOOST_TEST(entry);

                            BOOST_CHECK_GT(entry->size(), 0);
                            if (i == index)
                            {
                                BOOST_CHECK_EQUAL(entry->dirty(), true);
                            }
                            else
                            {
                                BOOST_CHECK_EQUAL(entry->dirty(), false);
                            }
                        }
                    }
                }
            }
        }


        // After reading, current storage should include previous storage's data, previous data's
        // dirty should be false
        totalCount = 0;
        tbb::concurrent_vector<std::function<void()>> checks;
        storage->parallelTraverse(false, [&](auto&& table, auto&& key, auto&& entry) {
            checks.push_back([table, key, entry] {
                // BOOST_CHECK_NE(tableInfo, nullptr);
                if (table != "s_tables" && !key.empty())
                {
                    auto l = entry.getField(0).size();
                    BOOST_CHECK_GE(l, 10);
                }
            });

            ++totalCount;
            return true;
        });

        for (auto& it : checks)
        {
            it();
        }

        BOOST_CHECK_EQUAL(totalCount, (10 * 2 + 10) * (index + 1));

        checks.clear();
        dirtyCount = 0;
        storage->parallelTraverse(true, [&](auto&& table, auto&& key, auto&& entry) {
            checks.push_back([table, key, entry]() {
                // BOOST_CHECK_NE(tableInfo, nullptr);
                if (table != "s_tables" && !key.empty())
                {
                    auto l = entry.getField(0).size();
                    BOOST_CHECK_GE(l, 10);
                    BOOST_CHECK_EQUAL(entry.dirty(), true);
                }
            });

            ++dirtyCount;
            return true;
        });

        for (auto& it : checks)
        {
            it();
        }

        BOOST_CHECK_EQUAL(dirtyCount, 10 * 2 + 10);
        storage->setReturnPage(true);
    }
}

BOOST_AUTO_TEST_CASE(getRows)
{
    std::vector<KeyPageStorage::Ptr> storages;
    auto valueFields = "value1,value2,value3";

    auto stateStorage = make_shared<StateStorage>(nullptr);
    auto prev = std::make_shared<KeyPageStorage>(stateStorage, true);
    auto tableStorage = std::make_shared<KeyPageStorage>(prev, false);

    BOOST_CHECK(prev->createTable("t_test", valueFields));

    auto table = prev->openTable("t_test");
    BOOST_TEST(table);

    for (size_t i = 0; i < 100; ++i)
    {
        auto entry = table->newEntry();
        entry.importFields({"data" + boost::lexical_cast<std::string>(i)});
        table->setRow("key" + boost::lexical_cast<std::string>(i), entry);
    }

    // query 50-150
    std::vector<std::string> keys;
    for (size_t i = 50; i < 150; ++i)
    {
        keys.push_back("key" + boost::lexical_cast<std::string>(i));
    }
    auto queryTable = tableStorage->openTable("t_test");
    BOOST_TEST(queryTable);

    std::vector<std::string_view> views;
    for (auto& key : keys)
    {
        views.push_back(key);
    }
    auto values = queryTable->getRows(views);

    for (size_t i = 0; i < 100; ++i)
    {
        auto entry = values[i];
        if (i + 50 < 100)
        {
            BOOST_TEST(entry);
            BOOST_CHECK_EQUAL(entry->dirty(), false);
            BOOST_CHECK_GT(entry->size(), 0);
        }
        else
        {
            BOOST_TEST(!entry);
        }
    }

    for (size_t i = 0; i < 10; ++i)
    {
        auto entry = queryTable->newEntry();
        entry.importFields({"data" + boost::lexical_cast<std::string>(i)});
        queryTable->setRow("key" + boost::lexical_cast<std::string>(i), entry);
    }

    // Query 0-30 local(0-9) prev(10-29)
    keys.clear();
    for (size_t i = 0; i < 30; ++i)
    {
        keys.push_back("key" + boost::lexical_cast<std::string>(i));
    }

    views.clear();
    for (auto& key : keys)
    {
        views.push_back(key);
    }
    values = queryTable->getRows(views);

    for (size_t i = 0; i < 30; ++i)
    {
        auto entry = values[i];
        if (i < 10)
        {
            BOOST_TEST(entry);
            BOOST_CHECK_EQUAL(entry->dirty(), true);
        }
        else
        {
            BOOST_TEST(entry);
            BOOST_CHECK_EQUAL(entry->dirty(), false);
        }
    }

    // Test deleted entry
    for (size_t i = 10; i < 20; ++i)
    {
        queryTable->setRow(
            "key" + boost::lexical_cast<std::string>(i), queryTable->newDeletedEntry());
    }

    auto values2 = queryTable->getRows(keys);
    for (size_t i = 0; i < values2.size(); ++i)
    {
        if (i >= 10 && i < 20)
        {
            BOOST_CHECK(!values2[i]);
        }
        else
        {
            BOOST_CHECK(values2[i]);
        }
    }

    // Test rollback
    auto recoder = std::make_shared<Recoder>();
    tableStorage->setRecoder(recoder);
    for (size_t i = 70; i < 80; ++i)
    {
        Entry myEntry;
        myEntry.importFields({"ddd1"});
        queryTable->setRow("key" + boost::lexical_cast<std::string>(i), std::move(myEntry));
    }

    keys.clear();
    for (size_t i = 70; i < 80; ++i)
    {
        keys.push_back("key" + boost::lexical_cast<std::string>(i));
    }

    auto values3 = queryTable->getRows(keys);
    for (auto& it : values3)
    {
        BOOST_CHECK(it);
        BOOST_CHECK_EQUAL(it->getField(0), "ddd1");
        BOOST_CHECK_EQUAL(it->dirty(), true);
    }

    tableStorage->rollback(*recoder);

    auto values4 = queryTable->getRows(keys);
    size_t count = 70;
    for (auto& it : values4)
    {
        BOOST_CHECK(it);
        BOOST_CHECK_EQUAL(it->getField(0), "data" + boost::lexical_cast<std::string>(count));
        BOOST_CHECK_EQUAL(it->dirty(), false);
        ++count;
    }
}

BOOST_AUTO_TEST_CASE(checkVersion)
{
    BOOST_CHECK_NO_THROW(tableFactory->createTable("testTable", "value1, value2, value3"));
    auto table = tableFactory->openTable("testTable");

    Entry value1;
    value1.importFields({"v1"});
    table->setRow("abc", std::move(value1));

    Entry value2;
    value2.importFields({"v2"});
    BOOST_CHECK_NO_THROW(table->setRow("abc", std::move(value2)));

    Entry value3;
    value3.importFields({"v3"});
    BOOST_CHECK_NO_THROW(table->setRow("abc", std::move(value3)));
}

BOOST_AUTO_TEST_CASE(deleteAndGetRows)
{
    KeyPageStorage::Ptr storage1 =
        std::make_shared<KeyPageStorage>(make_shared<StateStorage>(nullptr));

    storage1->asyncCreateTable(
        "table", "value", [](Error::UniquePtr error, std::optional<Table> table) {
            BOOST_CHECK(!error);
            BOOST_CHECK(table);
        });

    Entry entry1;
    entry1.importFields({"value1"});
    storage1->asyncSetRow(
        "table", "key1", std::move(entry1), [](Error::UniquePtr error) { BOOST_CHECK(!error); });

    Entry entry2;
    entry2.importFields({"value2"});
    storage1->asyncSetRow(
        "table", "key2", std::move(entry2), [](Error::UniquePtr error) { BOOST_CHECK(!error); });

    storage1->setReturnPage(true);
    KeyPageStorage::Ptr storage2 = std::make_shared<KeyPageStorage>(storage1);
    Entry deleteEntry;
    deleteEntry.setStatus(Entry::DELETED);
    storage2->asyncSetRow("table", "key2", std::move(deleteEntry),
        [](Error::UniquePtr error) { BOOST_CHECK(!error); });

    storage2->setReturnPage(true);
    KeyPageStorage::Ptr storage3 = std::make_shared<KeyPageStorage>(storage2);
    storage3->asyncGetPrimaryKeys(
        "table", c, [](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_CHECK(!error);
            BOOST_CHECK_EQUAL(keys.size(), 1);
            BOOST_CHECK_EQUAL(keys[0], "key1");
        });
}

BOOST_AUTO_TEST_CASE(deletedAndGetRow)
{
    KeyPageStorage::Ptr storage1 =
        std::make_shared<KeyPageStorage>(make_shared<StateStorage>(nullptr));

    storage1->asyncCreateTable(
        "table", "value", [](Error::UniquePtr error, std::optional<Table> table) {
            BOOST_CHECK(!error);
            BOOST_CHECK(table);
        });

    Entry entry1;
    entry1.importFields({"value1"});
    storage1->asyncSetRow(
        "table", "key1", std::move(entry1), [](Error::UniquePtr error) { BOOST_CHECK(!error); });

    storage1->setReturnPage(true);
    KeyPageStorage::Ptr storage2 = std::make_shared<KeyPageStorage>(storage1);
    Entry deleteEntry;
    deleteEntry.setStatus(Entry::DELETED);
    storage2->asyncSetRow("table", "key1", std::move(deleteEntry),
        [](Error::UniquePtr error) { BOOST_CHECK(!error); });

    storage2->asyncGetRow("table", "key1", [](Error::UniquePtr error, std::optional<Entry> entry) {
        BOOST_CHECK(!error);
        BOOST_CHECK(!entry);
    });

    storage2->asyncGetRow("table", "key1", [](Error::UniquePtr error, std::optional<Entry> entry) {
        BOOST_CHECK(!error);
        BOOST_CHECK(!entry);
    });
}

BOOST_AUTO_TEST_CASE(deletedAndGetRows)
{
    KeyPageStorage::Ptr storage1 = std::make_shared<KeyPageStorage>(nullptr);

    storage1->asyncCreateTable(
        "table", "value", [](Error::UniquePtr error, std::optional<Table> table) {
            BOOST_CHECK(!error);
            BOOST_CHECK(table);
        });

    Entry entry1;
    entry1.importFields({"value1"});
    storage1->asyncSetRow(
        "table", "key1", std::move(entry1), [](Error::UniquePtr error) { BOOST_CHECK(!error); });

    storage1->setReturnPage(true);
    KeyPageStorage::Ptr storage2 = std::make_shared<KeyPageStorage>(storage1);
    Entry deleteEntry;
    deleteEntry.setStatus(Entry::DELETED);
    storage2->asyncSetRow("table", "key1", std::move(deleteEntry),
        [](Error::UniquePtr error) { BOOST_CHECK(!error); });

    std::string_view keys[] = {"key1"};
    storage2->asyncGetRows(
        "table", keys, [](Error::UniquePtr error, std::vector<std::optional<Entry>> entry) {
            BOOST_CHECK(!error);
            BOOST_CHECK_EQUAL(entry.size(), 1);
            BOOST_CHECK(!entry[0]);
        });
}

BOOST_AUTO_TEST_CASE(rollbackAndGetRow)
{
    KeyPageStorage::Ptr storage1 = std::make_shared<KeyPageStorage>(nullptr);

    storage1->asyncCreateTable(
        "table", "value", [](Error::UniquePtr error, std::optional<Table> table) {
            BOOST_CHECK(!error);
            BOOST_CHECK(table);
        });

    Entry entry1;
    entry1.importFields({"value1"});
    storage1->asyncSetRow(
        "table", "key1", std::move(entry1), [](Error::UniquePtr error) { BOOST_CHECK(!error); });

    storage1->setReturnPage(true);
    KeyPageStorage::Ptr storage2 = std::make_shared<KeyPageStorage>(storage1);
    auto recoder = std::make_shared<Recoder>();
    storage2->setRecoder(recoder);

    Entry entry2;
    entry2.importFields({"value2"});
    storage2->asyncSetRow(
        "table", "key1", std::move(entry2), [](Error::UniquePtr error) { BOOST_CHECK(!error); });

    storage2->asyncGetRow("table", "key1", [](Error::UniquePtr error, std::optional<Entry> entry) {
        BOOST_CHECK(!error);
        BOOST_CHECK(entry);
        BOOST_CHECK_EQUAL(entry->getField(0), "value2");
    });

    storage2->rollback(*recoder);

    storage2->asyncGetRow("table", "key1", [](Error::UniquePtr error, std::optional<Entry> entry) {
        BOOST_CHECK(!error);
        BOOST_CHECK(entry);
        BOOST_CHECK_EQUAL(entry->getField(0), "value1");
    });
}

BOOST_AUTO_TEST_CASE(rollbackAndGetRows)
{
    KeyPageStorage::Ptr storage1 = std::make_shared<KeyPageStorage>(nullptr);

    storage1->asyncCreateTable(
        "table", "value", [](Error::UniquePtr error, std::optional<Table> table) {
            BOOST_CHECK(!error);
            BOOST_CHECK(table);
        });

    Entry entry1;
    entry1.importFields({"value1"});
    storage1->asyncSetRow(
        "table", "key1", std::move(entry1), [](Error::UniquePtr error) { BOOST_CHECK(!error); });

    storage1->setReturnPage(true);
    KeyPageStorage::Ptr storage2 = std::make_shared<KeyPageStorage>(storage1);
    auto recoder = std::make_shared<Recoder>();
    storage2->setRecoder(recoder);

    Entry entry2;
    entry2.importFields({"value2"});
    storage2->asyncSetRow(
        "table", "key1", std::move(entry2), [](Error::UniquePtr error) { BOOST_CHECK(!error); });

    std::string_view keys[] = {"key1"};
    storage2->asyncGetRows(
        "table", keys, [](Error::UniquePtr error, std::vector<std::optional<Entry>> entry) {
            BOOST_CHECK(!error);
            BOOST_CHECK_EQUAL(entry.size(), 1);
            BOOST_CHECK_EQUAL(entry[0].value().getField(0), "value2");
        });

    storage2->rollback(*recoder);

    storage2->asyncGetRows(
        "table", keys, [](Error::UniquePtr error, std::vector<std::optional<Entry>> entry) {
            BOOST_CHECK(!error);
            BOOST_CHECK_EQUAL(entry.size(), 1);
            BOOST_CHECK_EQUAL(entry[0].value().getField(0), "value1");
        });
}

BOOST_AUTO_TEST_CASE(randomRWHash)
{
    std::vector<std::tuple<bool, std::string, std::string, std::string>> rwSet;

    std::random_device rd;
    for (size_t i = 0; i < 3; ++i)
    {
        auto keyNum = rd();
        bool write = keyNum % 2;

        std::string table;
        std::string key;
        std::string value;
        if (write || i == 0)
        {
            write = true;
            table = boost::lexical_cast<std::string>(keyNum % 10);
            key = boost::lexical_cast<std::string>(keyNum);
            value = boost::lexical_cast<std::string>(rd());
        }
        else
        {
            auto index = keyNum % i;
            table = std::get<1>(rwSet[index]);
            key = std::get<2>(rwSet[index]);
        }

        rwSet.emplace_back(std::make_tuple(write, table, key, value));
    }

    std::vector<bcos::crypto::HashType> prevHashes;
    for (size_t times = 0; times < 10; ++times)
    {
        std::vector<bcos::crypto::HashType> hashes;
        KeyPageStorage::Ptr prev = nullptr;
        for (size_t i = 0; i < 10; ++i)
        {
            KeyPageStorage::Ptr storage = std::make_shared<KeyPageStorage>(prev);

            for (auto& it : rwSet)
            {
                auto& [write, table, key, value] = it;

                storage->asyncOpenTable(table, [storage, tableName = table](Error::UniquePtr error,
                                                   std::optional<Table> tableItem) {
                    BOOST_CHECK(!error);
                    if (!tableItem)
                    {
                        storage->asyncCreateTable(tableName, "value",
                            [](Error::UniquePtr error, std::optional<Table> tableItem) {
                                BOOST_CHECK(!error);
                                BOOST_CHECK(tableItem);
                            });
                    }
                });

                if (write)
                {
                    Entry entry;
                    entry.importFields({value});
                    storage->asyncSetRow(table, key, std::move(entry),
                        [](Error::UniquePtr error) { BOOST_CHECK(!error); });
                }
                else
                {
                    storage->asyncGetRow(table, key,
                        [](Error::UniquePtr error, std::optional<Entry>) { BOOST_CHECK(!error); });
                }
            }

            hashes.push_back(storage->hash(hashImpl));
            storage->setReadOnly(false);
            storage->setReturnPage(true);
            prev = storage;
        }

        if (!prevHashes.empty())
        {
            BOOST_CHECK_EQUAL_COLLECTIONS(
                prevHashes.begin(), prevHashes.end(), hashes.begin(), hashes.end());
        }
        prevHashes.swap(hashes);
        hashes.clear();
    }
}

BOOST_AUTO_TEST_CASE(hash_map)
{
    class EntryKey
    {
    public:
        EntryKey() {}
        EntryKey(std::string_view table, std::string_view key) : m_table(table), m_key(key) {}
        EntryKey(std::string_view table, std::string key) : m_table(table), m_key(std::move(key)) {}

        EntryKey(const EntryKey&) = default;
        EntryKey& operator=(const EntryKey&) = default;
        EntryKey(EntryKey&&) noexcept = default;
        EntryKey& operator=(EntryKey&&) noexcept = default;

        std::string_view table() const { return m_table; }

        std::string_view key() const
        {
            std::string_view view;
            std::visit([&view](auto&& key) { view = key; }, m_key);

            return view;
        }

        bool operator==(const EntryKey& rhs) const
        {
            return m_table == rhs.m_table && key() == rhs.key();
        }

        bool operator<(const EntryKey& rhs) const
        {
            if (m_table != rhs.m_table)
            {
                return m_table < rhs.m_table;
            }

            return m_key < rhs.m_key;
        }

    private:
        std::string_view m_table;
        std::variant<std::string_view, std::string> m_key;
    };

    struct EntryKeyHasher
    {
        size_t hash(const EntryKey& dataKey) const
        {
            size_t seed = hashString(dataKey.table());
            boost::hash_combine(seed, hashString(dataKey.key()));

            return seed;
        }

        bool equal(const EntryKey& lhs, const EntryKey& rhs) const
        {
            return lhs.table() == rhs.table() && lhs.key() == rhs.key();
        }

        std::hash<std::string_view> hashString;
    };

    tbb::concurrent_hash_map<EntryKey, int, EntryKeyHasher> data;

    std::string tableName = "table";
    std::string key = "key";

    decltype(data)::const_accessor it;
    BOOST_CHECK(data.emplace(it, EntryKey("table", std::string_view("key")), 100));

    decltype(data)::const_accessor findIt;
    BOOST_CHECK(data.find(findIt, EntryKey("table", std::string_view("key"))));
}

BOOST_AUTO_TEST_CASE(pageMerge)
{
    auto valueFields = "value1";

    auto stateStorage = make_shared<StateStorage>(nullptr);
    StateStorageInterface::Ptr prev = stateStorage;

    auto tableStorage = std::make_shared<KeyPageStorage>(prev, false, 1024);
    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        BOOST_CHECK(tableStorage->createTable(tableName, valueFields));

        auto table = tableStorage->openTable(tableName);
        BOOST_TEST(table);

        for (int k = 0; k < 1000; ++k)
        {
            auto entry = std::make_optional(table->newEntry());
            auto key =
                boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
            entry->setField(0, boost::lexical_cast<std::string>(k));
            BOOST_CHECK_NO_THROW(table->setRow(key, *entry));
        }
    }
    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        auto table = tableStorage->openTable(tableName);
        BOOST_TEST(table);

        for (int k = 0; k < 1000; ++k)
        {
            if (k % 5 < 4)
            {
                auto entry = std::make_optional(table->newDeletedEntry());
                auto key =
                    boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
                // BCOS_LOG(TRACE) << LOG_DESC("delete") << LOG_KV("tableName", tableName)
                //                 << LOG_KV("key", key);
                BOOST_CHECK_NO_THROW(table->setRow(key, *entry));
            }
        }
    }
    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        auto table = tableStorage->openTable(tableName);
        BOOST_TEST(table);

        for (int k = 0; k < 1000; ++k)
        {
            auto key =
                boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
            auto entry = table->getRow(key);
            // BCOS_LOG(TRACE) << LOG_DESC("getRow") << LOG_KV("tableName", tableName)
            //                 << LOG_KV("key", key);
            if (k % 5 < 4)
            {
                BOOST_TEST(!entry);
            }
            else
            {
                BOOST_TEST(entry);
                BOOST_TEST(entry->get() == boost::lexical_cast<std::string>(k));
            }
        }
    }
    std::atomic<size_t> totalCount = 0;
    tableStorage->parallelTraverse(false, [&](auto&&, auto&&, auto&&) {
        ++totalCount;
        return true;
    });
    BOOST_TEST(totalCount == 500);  // meta + 5page + s_table
}

BOOST_AUTO_TEST_CASE(pageMergeRandom)
{
    auto valueFields = "value1";

    auto stateStorage = make_shared<StateStorage>(nullptr);
    StateStorageInterface::Ptr prev = stateStorage;

    auto tableStorage = std::make_shared<KeyPageStorage>(prev, false, 1024);
#if defined(__APPLE__)
    #pragma omp parallel for
#endif
    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        BOOST_CHECK(tableStorage->createTable(tableName, valueFields));

        auto table = tableStorage->openTable(tableName);
        BOOST_TEST(table);

        for (int k = 0; k < 1000; ++k)
        {
            auto entry = std::make_optional(table->newEntry());
            auto key =
                boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);

            entry->setField(0, boost::lexical_cast<std::string>(k));
            table->setRow(key, *entry);
            // BOOST_REQUIRE_NO_THROW(table->setRow(key, *entry));
            // try
            // {
            //     table->setRow(key, *entry);
            // }
            // catch (std::exception& e)
            // {
            //     BCOS_LOG(TRACE) << LOG_DESC("set") << LOG_KV("tableName", tableName)
            //                     << LOG_KV("key", key)<< LOG_KV("what", e.what());
            // }
        }
    }
#if defined(__APPLE__)
    #pragma omp parallel for
#endif
    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        auto table = tableStorage->openTable(tableName);
        BOOST_TEST(table);

        for (int k = 0; k < 1000; ++k)
        {
            if (k % 5 < 4)
            {
                auto entry = std::make_optional(table->newDeletedEntry());
                auto key =
                    boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
                // BCOS_LOG(TRACE) << LOG_DESC("delete") << LOG_KV("tableName", tableName)
                //                 << LOG_KV("key", key);
                // BOOST_REQUIRE_NO_THROW(table->setRow(key, *entry));
                table->setRow(key, *entry);
            }
        }
    }
#if defined(__APPLE__)
    #pragma omp parallel for
#endif
    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        auto table = tableStorage->openTable(tableName);
        BOOST_TEST(table);

        for (int k = 0; k < 1000; ++k)
        {
            auto key =
                boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
            auto entry = table->getRow(key);
            // BCOS_LOG(TRACE) << LOG_DESC("getRow") << LOG_KV("tableName", tableName)
            //                 << LOG_KV("key", key);
            if (k % 5 < 4)
            {
                BOOST_TEST(!entry);
            }
            else
            {
                BOOST_TEST(entry);
                BOOST_TEST(entry->get() == boost::lexical_cast<std::string>(k));
            }
        }
    }
    std::atomic<size_t> totalCount = 0;
    tableStorage->parallelTraverse(false, [&](auto&&, auto&&, auto&&) {
        ++totalCount;
        return true;
    });
    BOOST_TEST(totalCount == 500);  // meta + 5page + s_table
}

BOOST_AUTO_TEST_CASE(pageMergeParallelRandom)
{
    auto valueFields = "value1";

    auto stateStorage = make_shared<StateStorage>(nullptr);
    StateStorageInterface::Ptr prev = stateStorage;

    auto tableStorage = std::make_shared<KeyPageStorage>(prev, false, 1024);
    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        BOOST_CHECK(tableStorage->createTable(tableName, valueFields));

        auto table = tableStorage->openTable(tableName);
        BOOST_TEST(table);

#if defined(__APPLE__)
    #pragma omp parallel for
#endif
        for (int k = 0; k < 1000; ++k)
        {
            auto entry = std::make_optional(table->newEntry());
            auto key =
                boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
            entry->setField(0, boost::lexical_cast<std::string>(k));
            BOOST_CHECK_NO_THROW(table->setRow(key, *entry));
        }
    }
    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        auto table = tableStorage->openTable(tableName);
        BOOST_TEST(table);

#if defined(__APPLE__)
    #pragma omp parallel for
#endif
        for (int k = 0; k < 1000; ++k)
        {
            if (k % 5 < 4)
            {
                auto entry = std::make_optional(table->newDeletedEntry());
                auto key =
                    boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
                // BCOS_LOG(TRACE) << LOG_DESC("delete") << LOG_KV("tableName", tableName)
                //                 << LOG_KV("key", key);
                BOOST_CHECK_NO_THROW(table->setRow(key, *entry));
            }
        }
    }

    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        auto table = tableStorage->openTable(tableName);
        BOOST_TEST(table);
#if defined(__APPLE__)
    #pragma omp parallel for
#endif
        for (int k = 0; k < 1000; ++k)
        {
            auto key =
                boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
            auto entry = table->getRow(key);
            // BCOS_LOG(TRACE) << LOG_DESC("getRow") << LOG_KV("tableName", tableName)
            //                 << LOG_KV("key", key);
            if (k % 5 < 4)
            {
                BOOST_TEST(!entry);
            }
            else
            {
                BOOST_TEST(entry);
                BOOST_TEST(entry->get() == boost::lexical_cast<std::string>(k));
            }
        }
    }
    // std::atomic<size_t> totalCount = 0;
    // tableStorage->parallelTraverse(false, [&](auto&&, auto&&, auto&&) {
    //     ++totalCount;
    //     return true;
    // });
    // BOOST_TEST(totalCount == 393);  // meta + 5page + s_table
}

BOOST_AUTO_TEST_CASE(parallelMix)
{
    auto valueFields = "value1";

    auto stateStorage = make_shared<StateStorage>(nullptr);
    StateStorageInterface::Ptr prev = stateStorage;

    auto tableStorage = std::make_shared<KeyPageStorage>(prev, false, 1024);
    // #pragma omp parallel for
    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        BOOST_CHECK(tableStorage->createTable(tableName, valueFields));

        auto table = tableStorage->openTable(tableName);
        BOOST_TEST(table);
#if defined(__APPLE__)
    #pragma omp parallel for
#endif
        for (int k = 0; k < 1000; ++k)
        {
            auto entry = std::make_optional(table->newEntry());
            auto key =
                boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
            entry->setField(0, boost::lexical_cast<std::string>(k));
            BOOST_CHECK_NO_THROW(table->setRow(key, *entry));
        }
    }
    // #pragma omp parallel for
    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        auto table = tableStorage->openTable(tableName);
        BOOST_TEST(table);
#if defined(__APPLE__)
    #pragma omp parallel for
#endif
        for (int k = 0; k < 1000; ++k)
        {
            if (k % 5 == 4)
            {
                auto entry = std::make_optional(table->newEntry());
                auto key =
                    boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
                entry->setField(0, boost::lexical_cast<std::string>(k + 1));
                BOOST_CHECK_NO_THROW(table->setRow(key, *entry));
            }
            else
            {
                auto key =
                    boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
                auto entry = table->getRow(key);
                auto value = boost::lexical_cast<std::string>(k);
                BOOST_TEST(entry);
                BOOST_TEST(entry->get() == value);
                auto deleteEntry = std::make_optional(table->newDeletedEntry());
                // BCOS_LOG(TRACE) << LOG_DESC("delete") << LOG_KV("tableName", tableName)
                //                 << LOG_KV("key", key);
                BOOST_CHECK_NO_THROW(table->setRow(key, *deleteEntry));
            }
        }
    }
    // #pragma omp parallel for
    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        auto table = tableStorage->openTable(tableName);
        BOOST_TEST(table);
#if defined(__APPLE__)
    #pragma omp parallel for
#endif
        for (int k = 0; k < 1000; ++k)
        {
            auto key =
                boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
            auto entry = table->getRow(key);
            // BCOS_LOG(TRACE) << LOG_DESC("getRow") << LOG_KV("tableName", tableName)
            //                 << LOG_KV("key", key);
            if (k % 5 < 4)
            {
                BOOST_TEST(!entry);
            }
            else
            {
                BOOST_TEST(entry);
                BOOST_TEST(entry->get() == boost::lexical_cast<std::string>(k + 1));
            }
        }
    }
    // std::atomic<size_t> totalCount = 0;
    // tableStorage->parallelTraverse(false, [&](auto&&, auto&&, auto&&) {
    //     ++totalCount;
    //     return true;
    // });
    // BOOST_TEST(totalCount == 392);  // meta + 5page + s_table
}

BOOST_AUTO_TEST_CASE(pageSplit)
{
    auto valueFields = "value1";

    auto stateStorage = make_shared<StateStorage>(nullptr);
    StateStorageInterface::Ptr prev = stateStorage;

    auto tableStorage = std::make_shared<KeyPageStorage>(prev, false, 1024);
    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        BOOST_CHECK(tableStorage->createTable(tableName, valueFields));

        auto table = tableStorage->openTable(tableName);
        BOOST_TEST(table);

        for (int k = 0; k < 1000; ++k)
        {
            auto entry = std::make_optional(table->newEntry());
            auto key =
                boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
            entry->setField(0, boost::lexical_cast<std::string>(k));
            BOOST_CHECK_NO_THROW(table->setRow(key, *entry));
        }
    }
    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        auto table = tableStorage->openTable(tableName);
        BOOST_TEST(table);

        for (int k = 0; k < 1000; ++k)
        {
            auto key =
                boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
            auto entry = table->getRow(key);
            // BCOS_LOG(TRACE) << LOG_DESC("getRow") << LOG_KV("tableName", tableName)
            //                 << LOG_KV("key", key);
            BOOST_TEST(entry);
            BOOST_TEST(entry->get() == boost::lexical_cast<std::string>(k));
        }
    }
    std::atomic<size_t> totalCount = 0;
    tableStorage->parallelTraverse(false, [&](auto&&, auto&&, auto&&) {
        ++totalCount;
        return true;
    });
    BOOST_TEST(totalCount == 100 * 6 + 100);  // meta + 5page + s_table
}

BOOST_AUTO_TEST_CASE(pageSplitRandom)
{
    auto valueFields = "value1";

    auto stateStorage = make_shared<StateStorage>(nullptr);
    StateStorageInterface::Ptr prev = stateStorage;

    auto tableStorage = std::make_shared<KeyPageStorage>(prev, false, 256);
#if defined(__APPLE__)
    #pragma omp parallel for
#endif
    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        BOOST_CHECK(tableStorage->createTable(tableName, valueFields));

        auto table = tableStorage->openTable(tableName);
        BOOST_TEST(table);

        for (int k = 0; k < 5000; ++k)
        {
            auto entry = std::make_optional(table->newEntry());
            auto key =
                boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
            entry->setField(0, boost::lexical_cast<std::string>(k));
            BOOST_CHECK_NO_THROW(table->setRow(key, *entry));
        }
    }
#if defined(__APPLE__)
    #pragma omp parallel for
#endif
    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        auto table = tableStorage->openTable(tableName);
        BOOST_TEST(table);

        for (int k = 0; k < 500; ++k)
        {
            auto key =
                boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
            auto entry = table->getRow(key);
            // BCOS_LOG(TRACE) << LOG_DESC("getRow") << LOG_KV("tableName", tableName)
            //                 << LOG_KV("key", key);
            BOOST_TEST(entry);
            BOOST_TEST(entry->get() == boost::lexical_cast<std::string>(k));
        }
    }
    std::atomic<size_t> totalCount = 0;
    tableStorage->parallelTraverse(false, [&](auto&&, auto&&, auto&&) {
        ++totalCount;
        return true;
    });
    BOOST_TEST(totalCount == 14400);  // meta + 5page + s_table
}

BOOST_AUTO_TEST_CASE(pageSplitParallelRandom)
{
    auto valueFields = "value1";

    auto stateStorage = make_shared<StateStorage>(nullptr);
    StateStorageInterface::Ptr prev = stateStorage;

    auto tableStorage = std::make_shared<KeyPageStorage>(prev, false, 256);
    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        BOOST_CHECK(tableStorage->createTable(tableName, valueFields));

        auto table = tableStorage->openTable(tableName);
        BOOST_TEST(table);
#if defined(__APPLE__)
    #pragma omp parallel for
#endif
        for (int k = 0; k < 500; ++k)
        {
            auto entry = std::make_optional(table->newEntry());
            auto key =
                boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
            entry->setField(0, boost::lexical_cast<std::string>(k));
            BOOST_CHECK_NO_THROW(table->setRow(key, *entry));
        }
    }

    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        auto table = tableStorage->openTable(tableName);
        BOOST_TEST(table);
#if defined(__APPLE__)
    #pragma omp parallel for
#endif
        for (int k = 0; k < 500; ++k)
        {
            auto key =
                boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
            auto entry = table->getRow(key);
            // BCOS_LOG(TRACE) << LOG_DESC("getRow") << LOG_KV("tableName", tableName)
            //                 << LOG_KV("key", key);
            BOOST_TEST(entry);
            BOOST_TEST(entry->get() == boost::lexical_cast<std::string>(k));
        }
    }
    // std::atomic<size_t> totalCount = 0;
    // tableStorage->parallelTraverse(false, [&](auto&&, auto&&, auto&&) {
    //     ++totalCount;
    //     return true;
    // });
    // BOOST_TEST(totalCount == 999);  // meta + 5page + s_table
}


BOOST_AUTO_TEST_CASE(asyncGetPrimaryKeys)
{  // TODO: add ut for asyncGetPrimaryKeys and condition
    auto valueFields = "value1";

    auto stateStorage = make_shared<StateStorage>(nullptr);
    StateStorageInterface::Ptr prev = stateStorage;

    auto tableStorage = std::make_shared<KeyPageStorage>(prev, false, 256);
    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        BOOST_CHECK(tableStorage->createTable(tableName, valueFields));

        auto table = tableStorage->openTable(tableName);
        BOOST_TEST(table);
#if defined(__APPLE__)
    #pragma omp parallel for
#endif
        for (int k = 0; k < 1000; ++k)
        {
            auto entry = std::make_optional(table->newEntry());
            auto key = boost::lexical_cast<std::string>(k);
            entry->setField(0, boost::lexical_cast<std::string>(k));
            BOOST_CHECK_NO_THROW(table->setRow(key, *entry));
        }
    }

    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        auto table = tableStorage->openTable(tableName);
        BOOST_TEST(table);
#if defined(__APPLE__)
    #pragma omp parallel for
#endif
        for (int k = 0; k < 1000; ++k)
        {
            Condition c;
            c.limit(0, 200);
            auto keys = table->getPrimaryKeys(c);
            BOOST_TEST(keys.size() == 200);
            c.limit(200, 300);
            keys = table->getPrimaryKeys(c);
            BOOST_TEST(keys.size() == 300);
            c.limit(900, 200);
            keys = table->getPrimaryKeys(c);
            BOOST_TEST(keys.size() == 100);
            c.GE("900");
            BOOST_TEST(keys.size() == 100);
        }
    }
    auto tableName = "table_" + boost::lexical_cast<std::string>(0);
    auto table = tableStorage->openTable(tableName);
    auto printVector = [](const std::vector<std::string>& vec) -> string {
        stringstream ss;
        for (auto& s : vec)
        {
            ss << s << " ";
        }
        ss << endl;
        return ss.str();
    };
    Condition c;
    c.limit(0, 500);
    c.LE("250");
    auto keys = table->getPrimaryKeys(c);
    BCOS_LOG(TRACE) << "LE 250:" << printVector(keys);
    BOOST_TEST(keys.size() == 170);
    Condition c2;
    c2.limit(0, 500);
    c2.LT("250");
    auto keys2 = table->getPrimaryKeys(c2);
    BCOS_LOG(TRACE) << "LT 250:" << printVector(keys2);
    BOOST_TEST(keys2.size() == 169);
    Condition c3;
    c3.limit(750, 500);
    c3.GT("250");
    auto keys3 = table->getPrimaryKeys(c3);
    BCOS_LOG(TRACE) << "GT 250:" << printVector(keys3);
    BOOST_TEST(keys3.size() == 80);
    Condition c4;
    c4.limit(750, 500);
    c4.GE("250");
    auto keys4 = table->getPrimaryKeys(c4);
    BCOS_LOG(TRACE) << "GE 250:" << printVector(keys4);
    BOOST_TEST(keys4.size() == 81);
    Condition c5;
    c5.limit(500, 500);
    c5.NE("250");
    auto keys5 = table->getPrimaryKeys(c5);
    BCOS_LOG(TRACE) << "NE 250:" << printVector(keys5);
    BOOST_TEST(keys5.size() == 499);
}


BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
