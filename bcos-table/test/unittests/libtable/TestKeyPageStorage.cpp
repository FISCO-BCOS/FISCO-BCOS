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
#include "bcos-framework/storage/StorageInterface.h"
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
#include <unordered_map>

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
        boost::log::core::get()->set_logging_enabled(false);
        hashImpl = make_shared<Header256Hash>();
        auto stateStorage = make_shared<StateStorage>(nullptr);
        stateStorage->setEnableTraverse(true);
        memoryStorage = stateStorage;
        BOOST_REQUIRE(memoryStorage != nullptr);
        tableFactory = make_shared<KeyPageStorage>(memoryStorage);
        BOOST_REQUIRE(tableFactory != nullptr);
        c.limit(0, 100);
    }

    ~KeyPageStorageFixture() { boost::log::core::get()->set_logging_enabled(true); }
    std::optional<Table> createDefaultTable()
    {
        std::promise<std::optional<Table>> createPromise;
        tableFactory->asyncCreateTable(
            testTableName, valueField, [&](auto&& error, std::optional<Table> table) {
                BOOST_REQUIRE(!error);
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

    BOOST_REQUIRE(!table);
    auto ret = tableFactory->createTable(tableName, valueField);
    BOOST_REQUIRE(ret);

    table = tableFactory->openTable(tableName);
    BOOST_REQUIRE(table);

    BOOST_REQUIRE_THROW(tableFactory->createTable(tableName, valueField), bcos::Error);
}

BOOST_AUTO_TEST_CASE(rollback)
{
    auto ret = createDefaultTable();
    BOOST_REQUIRE(ret);
    auto table = tableFactory->openTable(testTableName);

    auto deleteEntry = table->newEntry();
    deleteEntry.setStatus(Entry::DELETED);
    BOOST_REQUIRE_NO_THROW(table->setRow("name", deleteEntry));

    auto hash = tableFactory->hash(hashImpl);

    // delete not exist entry will cause hash mismatch
#if defined(__APPLE__)
    BOOST_CHECK_EQUAL(hash.hex(),
        crypto::HashType("ab98649ca506b076000000000000000000000000000000000000000000000001").hex());
#endif
    auto entry = std::make_optional(table->newEntry());
    BOOST_REQUIRE_NO_THROW(entry->setField(0, "Lili"));
    BOOST_REQUIRE_NO_THROW(table->setRow("name", *entry));

    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_CHECK_EQUAL(hash.hex(),
        crypto::HashType("c18354d205471d61000000000000000000000000000000000000000000000000").hex());
#endif
    entry = table->getRow("name");
    BOOST_REQUIRE(entry.has_value());
    BOOST_REQUIRE(entry->dirty() == true);
    BOOST_REQUIRE(entry->getField(0) == "Lili");
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_CHECK_EQUAL(hash.hex(),
        crypto::HashType("c18354d205471d61000000000000000000000000000000000000000000000000").hex());
#endif
    auto savePoint = std::make_shared<Recoder>();
    tableFactory->setRecoder(savePoint);

    entry = table->newEntry();
    entry->setField(0, "12345");
    table->setRow("id", *entry);
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_CHECK_EQUAL(hash.hex(),
        crypto::HashType("d26dbc9a92ed28b1000000000000000000000000000000000000000000000000").hex());
#endif
    entry = table->getRow("id");
    BOOST_REQUIRE(entry.has_value());
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_CHECK_EQUAL(hash.hex(),
        crypto::HashType("d26dbc9a92ed28b1000000000000000000000000000000000000000000000000").hex());
#endif
    entry = table->getRow("name");
    BOOST_REQUIRE(entry.has_value());
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_CHECK_EQUAL(hash.hex(),
        crypto::HashType("d26dbc9a92ed28b1000000000000000000000000000000000000000000000000").hex());
#endif
    auto savePoint1 = std::make_shared<Recoder>();
    tableFactory->setRecoder(savePoint1);

    entry = table->newEntry();
    entry->setField(0, "500");
    table->setRow("balance", *entry);
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_CHECK_EQUAL(hash.hex(),
        crypto::HashType("2b7be3797d97dcf7000000000000000000000000000000000000000000000000").hex());
#endif
    entry = table->getRow("balance");
    BOOST_REQUIRE(entry.has_value());
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_CHECK_EQUAL(hash.hex(),
        crypto::HashType("2b7be3797d97dcf7000000000000000000000000000000000000000000000000").hex());
#endif
    entry = table->getRow("name");
    BOOST_REQUIRE(entry.has_value());
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_CHECK_EQUAL(hash.hex(),
        crypto::HashType("2b7be3797d97dcf7000000000000000000000000000000000000000000000000").hex());
#endif
    auto savePoint2 = std::make_shared<Recoder>();
    tableFactory->setRecoder(savePoint2);

    auto deleteEntry2 = std::make_optional(table->newDeletedEntry());
    table->setRow("name", *deleteEntry2);
    hash = tableFactory->hash(hashImpl);

// delete entry will cause hash mismatch
#if defined(__APPLE__)
    BOOST_CHECK_EQUAL(hash.hex(),
        crypto::HashType("4160d337ddd671e0000000000000000000000000000000000000000000000001").hex());
#endif
    entry = table->getRow("name");
    BOOST_REQUIRE(!entry);
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_CHECK_EQUAL(hash.hex(),
        crypto::HashType("4160d337ddd671e0000000000000000000000000000000000000000000000001").hex());
#endif
    std::cout << "Try remove balance" << std::endl;
    tableFactory->rollback(*savePoint2);
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_CHECK_EQUAL(hash.hex(),
        crypto::HashType("2b7be3797d97dcf7000000000000000000000000000000000000000000000000").hex());
#endif
    entry = table->getRow("name");
    BOOST_REQUIRE_NE(entry->status(), Entry::DELETED);
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_CHECK_EQUAL(hash.hex(),
        crypto::HashType("2b7be3797d97dcf7000000000000000000000000000000000000000000000000").hex());
#endif
    tableFactory->rollback(*savePoint1);
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_CHECK_EQUAL(hash.hex(),
        crypto::HashType("d26dbc9a92ed28b1000000000000000000000000000000000000000000000000").hex());
#endif
    entry = table->getRow("name");
    BOOST_REQUIRE(entry.has_value());
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_CHECK_EQUAL(hash.hex(),
        crypto::HashType("d26dbc9a92ed28b1000000000000000000000000000000000000000000000000").hex());
#endif
    entry = table->getRow("balance");
    BOOST_REQUIRE(!entry);
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_CHECK_EQUAL(hash.hex(),
        crypto::HashType("d26dbc9a92ed28b1000000000000000000000000000000000000000000000000").hex());
#endif

    tableFactory->rollback(*savePoint);
    entry = table->getRow("name");
    BOOST_REQUIRE(entry.has_value());
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_CHECK_EQUAL(hash.hex(),
        crypto::HashType("c18354d205471d61000000000000000000000000000000000000000000000000").hex());
#endif
    entry = table->getRow("balance");
    BOOST_REQUIRE(!entry);
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_CHECK_EQUAL(hash.hex(),
        crypto::HashType("c18354d205471d61000000000000000000000000000000000000000000000000").hex());
#endif
    entry = table->getRow("id");
    BOOST_REQUIRE(!entry);
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_CHECK_EQUAL(hash.hex(),
        crypto::HashType("c18354d205471d61000000000000000000000000000000000000000000000000").hex());
#endif

    // insert without version
    entry = table->newEntry();
    entry->setField(0, "new record");
    BOOST_REQUIRE_NO_THROW(table->setRow("id", *entry));
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_CHECK_EQUAL(hash.hex(),
        crypto::HashType("2c14904fc33bbbae000000000000000000000000000000000000000000000000").hex());
#endif

    entry = table->newDeletedEntry();
    BOOST_REQUIRE_NO_THROW(table->setRow("id", *entry));
    hash = tableFactory->hash(hashImpl);
    // delete entry will cause hash mismatch
#if defined(__APPLE__)
    BOOST_CHECK_EQUAL(hash.hex(),
        crypto::HashType("159ca8eb7641c2c1000000000000000000000000000000000000000000000001").hex());
#endif
}

BOOST_AUTO_TEST_CASE(rollback2)
{
    auto hash0 = tableFactory->hash(hashImpl);
    // auto savePoint0 = tableFactory->savepoint();
    auto savePoint0 = std::make_shared<Recoder>();
    tableFactory->setRecoder(savePoint0);
    BOOST_REQUIRE(hash0 == crypto::HashType(0));
    auto ret = createDefaultTable();
    BOOST_REQUIRE(ret);
    auto table = tableFactory->openTable(testTableName);

    auto deleteEntry = table->newDeletedEntry();
    table->setRow("name", deleteEntry);
    auto hash = tableFactory->hash(hashImpl);
// delete not exist entry will cause hash mismatch
#if defined(__APPLE__)
    BOOST_CHECK_EQUAL(hash.hex(),
        crypto::HashType("ab98649ca506b076000000000000000000000000000000000000000000000001").hex());
#endif
    auto entry = std::make_optional(table->newEntry());
    // entry->setField("key", "name");
    entry->setField(0, "Lili");
    table->setRow("name", *entry);
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_CHECK_EQUAL(hash.hex(),
        crypto::HashType("c18354d205471d61000000000000000000000000000000000000000000000000").hex());
#endif
    entry = table->getRow("name");
    BOOST_REQUIRE(entry.has_value());
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_CHECK_EQUAL(hash.hex(),
        crypto::HashType("c18354d205471d61000000000000000000000000000000000000000000000000").hex());
#endif
    // BOOST_REQUIRE(table->dirty() == true);
    BOOST_REQUIRE(entry->dirty() == true);
    BOOST_REQUIRE(entry->getField(0) == "Lili");
    // auto savePoint = tableFactory->savepoint();
    auto savePoint = std::make_shared<Recoder>();
    tableFactory->setRecoder(savePoint);

    entry = table->newEntry();
    // entry->setField("key", "id");
    entry->setField(0, "12345");
    table->setRow("id", *entry);
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_CHECK_EQUAL(hash.hex(),
        crypto::HashType("d26dbc9a92ed28b1000000000000000000000000000000000000000000000000").hex());
#endif
    entry = table->getRow("id");
    BOOST_REQUIRE(entry.has_value());
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_CHECK_EQUAL(hash.hex(),
        crypto::HashType("d26dbc9a92ed28b1000000000000000000000000000000000000000000000000").hex());
#endif
    entry = table->getRow("name");
    BOOST_REQUIRE(entry.has_value());
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_CHECK_EQUAL(hash.hex(),
        crypto::HashType("d26dbc9a92ed28b1000000000000000000000000000000000000000000000000").hex());
#endif
    // BOOST_REQUIRE(table->dirty() == true);

    tableFactory->rollback(*savePoint);

    entry = table->getRow("name");
    BOOST_REQUIRE(entry.has_value());
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_CHECK_EQUAL(hash.hex(),
        crypto::HashType("c18354d205471d61000000000000000000000000000000000000000000000000").hex());
#endif
    entry = table->getRow("balance");
    BOOST_REQUIRE(!entry);
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_CHECK_EQUAL(hash.hex(),
        crypto::HashType("c18354d205471d61000000000000000000000000000000000000000000000000").hex());
#endif
    entry = table->getRow("id");
    BOOST_REQUIRE(!entry);
    hash = tableFactory->hash(hashImpl);
#if defined(__APPLE__)
    BOOST_CHECK_EQUAL(hash.hex(),
        crypto::HashType("c18354d205471d61000000000000000000000000000000000000000000000000").hex());
#endif

    // BOOST_REQUIRE(table->dirty() == true);
    tableFactory->rollback(*savePoint0);
    hash = tableFactory->hash(hashImpl);
    BOOST_REQUIRE(hash.hex() == crypto::HashType("").hex());
    entry = table->getRow("name");
    BOOST_REQUIRE(!entry);

    auto hash00 = tableFactory->hash(hashImpl);
    BOOST_REQUIRE(hash00 == crypto::HashType(0));

    BOOST_REQUIRE_EQUAL_COLLECTIONS(hash0.begin(), hash0.end(), hash00.begin(), hash00.end());
    BOOST_REQUIRE(hash00 == hash0);
    table = tableFactory->openTable(testTableName);
    BOOST_REQUIRE(!table);
}

BOOST_AUTO_TEST_CASE(rollback3)
{
    // Test rollback multi state storage
}

BOOST_AUTO_TEST_CASE(hash)
{
    auto ret = createDefaultTable();
    BOOST_REQUIRE(ret);

    auto table = tableFactory->openTable(testTableName);
    auto entry = std::make_optional(table->newEntry());
    // entry->setField("key", "name");
    entry->setField(0, "Lili");
    BOOST_REQUIRE_NO_THROW(table->setRow("name", *entry));
    entry = table->getRow("name");
    BOOST_REQUIRE(entry.has_value());
    auto tableFactory0 = make_shared<KeyPageStorage>(tableFactory);

    entry = std::make_optional(table->newEntry());
    // entry->setField("key", "id");
    entry->setField(0, "12345");
    BOOST_REQUIRE_NO_THROW(table->setRow("id", *entry));
    entry = table->getRow("id");
    BOOST_REQUIRE(entry.has_value());
    entry = table->getRow("name");
    BOOST_REQUIRE(entry.has_value());
    // BOOST_REQUIRE(table->dirty() == true);
    auto keys = table->getPrimaryKeys({c});
    BOOST_REQUIRE(keys.size() == 2);

    auto entries = table->getRows(keys);
    BOOST_REQUIRE(entries.size() == 2);

    auto dbHash1 = tableFactory->hash(hashImpl);

    auto savePoint = std::make_shared<Recoder>();
    tableFactory->setRecoder(savePoint);
    auto idEntry = table->getRow("id");

    auto deletedEntry = std::make_optional(table->newDeletedEntry());
    BOOST_REQUIRE_NO_THROW(table->setRow("id", *deletedEntry));
    entry = table->getRow("id");
    BOOST_REQUIRE(!entry);

    tableFactory->rollback(*savePoint);
    entry = table->getRow("name");
    BOOST_REQUIRE(entry.has_value());
    entry = table->getRow("balance");
    BOOST_REQUIRE(!entry);
    // BOOST_REQUIRE(table->dirty() == true);

    auto dbHash2 = tableFactory->hash(hashImpl);
    BOOST_REQUIRE_EQUAL(dbHash1.hex(), dbHash2.hex());

    // getPrimaryKeys and getRows
    entry = table->newEntry();
    // entry->setField("key", "id");
    entry->setField(0, "12345");
    BOOST_REQUIRE_NO_THROW(table->setRow("id", *entry));
    entry = table->getRow("name");
    entry->setField(0, "Wang");
    BOOST_REQUIRE_NO_THROW(table->setRow("name", *entry));
    entry = table->newEntry();
    // entry->setField("key", "balance");
    entry->setField(0, "12345");
    BOOST_REQUIRE_NO_THROW(table->setRow("balance", *entry));
    BOOST_REQUIRE(entry.has_value());
    keys = table->getPrimaryKeys({c});
    BOOST_REQUIRE(keys.size() == 3);

    entries = table->getRows(keys);
    BOOST_REQUIRE(entries.size() == 3);
    entry = table->getRow("name");
    BOOST_REQUIRE(entry.has_value());
    entry = table->getRow("balance");
    BOOST_REQUIRE(entry.has_value());
    entry = table->getRow("balance1");
    BOOST_REQUIRE(!entry);

    auto nameEntry = table->getRow("name");
    auto deletedEntry2 = std::make_optional(table->newDeletedEntry());
    BOOST_REQUIRE_NO_THROW(table->setRow("name", *deletedEntry2));
    entry = table->getRow("name");
    BOOST_REQUIRE(!entry);
    // BOOST_REQUIRE_EQUAL(entry->status(), Entry::DELETED);
    keys = table->getPrimaryKeys({c});
    BOOST_REQUIRE(keys.size() == 2);

    entries = table->getRows(keys);
    BOOST_REQUIRE(entries.size() == 2);

    auto idEntry2 = table->getRow("id");
    auto deletedEntry3 = std::make_optional(table->newDeletedEntry());
    BOOST_REQUIRE_NO_THROW(table->setRow("id", *deletedEntry3));
    entry = table->getRow("id");
    BOOST_REQUIRE(!entry);
    // BOOST_REQUIRE_EQUAL(entry->status(), Entry::DELETED);
    keys = table->getPrimaryKeys({c});
    BOOST_REQUIRE(keys.size() == 1);

    entries = table->getRows(keys);
    BOOST_REQUIRE(entries.size() == 1);
    // tableFactory->asyncCommit([](Error::Ptr, size_t) {});
}

BOOST_AUTO_TEST_CASE(open_sysTables)
{
    auto table = tableFactory->openTable(StorageInterface::SYS_TABLES);
    BOOST_REQUIRE(table);
}

BOOST_AUTO_TEST_CASE(openAndCommit)
{
    auto hashImpl2 = make_shared<Header256Hash>();
    auto memoryStorage2 = make_shared<StateStorage>(nullptr);
    auto tableFactory2 = make_shared<KeyPageStorage>(memoryStorage2);

    for (int i = 10; i < 20; ++i)
    {
        BOOST_REQUIRE(tableFactory2 != nullptr);

        std::string tableName = "testTable" + boost::lexical_cast<std::string>(i);
        auto key = "testKey" + boost::lexical_cast<std::string>(i);
        tableFactory2->createTable(tableName, "value");
        auto table = tableFactory2->openTable(tableName);

        auto entry = std::make_optional(table->newEntry());
        entry->setField(0, "hello world!");
        table->setRow(key, *entry);

        std::promise<bool> getRow;
        table->asyncGetRow(key, [&](auto&& error, auto&& result) {
            BOOST_REQUIRE(!error);
            BOOST_REQUIRE_EQUAL(result->getField(0), "hello world!");

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
        prev->setReadOnly(true);
        auto tableStorage = std::make_shared<KeyPageStorage>(prev);
        for (int j = 0; j < 10; ++j)
        {
            auto tableName = "table_" + boost::lexical_cast<std::string>(i) + "_" +
                             boost::lexical_cast<std::string>(j);
            BOOST_REQUIRE(tableStorage->createTable(tableName, valueFields));

            auto table = tableStorage->openTable(tableName);
            BOOST_REQUIRE(table);

            for (int k = 0; k < 10; ++k)
            {
                auto entry = std::make_optional(table->newEntry());
                auto key =
                    boost::lexical_cast<std::string>(i) + boost::lexical_cast<std::string>(k);
                entry->setField(0, boost::lexical_cast<std::string>(i));
                BOOST_REQUIRE_NO_THROW(table->setRow(key, *entry));
            }
        }

        prev = tableStorage;
        storages.push_back(tableStorage);
    }

    for (int index = 0; index < 10; ++index)
    {
        auto storage = storages[index];
        storage->setReadOnly(false);
        // Data count must be 10 * 10 + 10
        std::atomic<size_t> totalCount = 0;
        storage->parallelTraverse(false, [&](auto&&, auto&&, auto&&) {
            ++totalCount;
            return true;
        });
        BOOST_REQUIRE_EQUAL(totalCount, (9 + 1 + 1) * 10 + 10);  // 10 for s_tables, every table has
                                                                 // 1 page(10 entry) and 1 meta and
                                                                 // 9 invalid key

        // Dirty data count must be 10 * 10 + 10
        std::atomic<size_t> dirtyCount = 0;
        storage->parallelTraverse(true, [&](auto&&, auto&&, auto&&) {
            ++dirtyCount;
            return true;
        });

        BOOST_REQUIRE_EQUAL(dirtyCount, (9 + 1 + 1) * 10 + 10);  // 10 for s_tables, every table has
                                                                 // 1 page(10 entry) and 1 meta

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
                    BOOST_REQUIRE(!table);
                }
                else
                {
                    BOOST_REQUIRE(table);

                    for (int k = 0; k < 10; ++k)
                    {
                        auto key = boost::lexical_cast<std::string>(i) +
                                   boost::lexical_cast<std::string>(k);
                        BCOS_LOG(INFO)
                            << LOG_DESC("get") << LOG_KV("tableName", tableName)
                            << LOG_KV("key", key) << LOG_KV("index", index) << LOG_KV("i", i);
                        auto entry = table->getRow(key);
                        if (i > index)
                        {
                            BOOST_REQUIRE(!entry);
                        }
                        else
                        {
                            BOOST_REQUIRE(entry.has_value());

                            BOOST_REQUIRE_GT(entry->size(), 0);
                            if (i == index)
                            {
                                BOOST_REQUIRE_EQUAL(entry->dirty(), true);
                            }
                            else
                            {
                                BOOST_REQUIRE_EQUAL(entry->dirty(), false);
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
                // BOOST_REQUIRE_NE(tableInfo, nullptr);
                if (table != "s_tables" && !key.empty() && entry.status() != Entry::Status::DELETED)
                {
                    auto l = entry.getField(0).size();
                    BOOST_REQUIRE_GE(l, 10);
                }
            });

            ++totalCount;
            return true;
        });

        for (auto& it : checks)
        {
            it();
        }

        BOOST_REQUIRE_EQUAL(totalCount, 120 + 30 * index);

        checks.clear();
        dirtyCount = 0;
        storage->parallelTraverse(true, [&](auto&& table, auto&& key, auto&& entry) {
            checks.push_back([table, key, entry]() {
                // BOOST_REQUIRE_NE(tableInfo, nullptr);
                if (table != "s_tables" && !key.empty() && entry.status() != Entry::Status::DELETED)
                {
                    auto l = entry.getField(0).size();
                    BOOST_REQUIRE_GE(l, 10);
                    BOOST_REQUIRE_EQUAL(entry.dirty(), true);
                }
            });

            ++dirtyCount;
            return true;
        });

        for (auto& it : checks)
        {
            it();
        }
        BOOST_REQUIRE_EQUAL(dirtyCount, 120);
        storage->setReadOnly(true);
    }
}

BOOST_AUTO_TEST_CASE(getRows)
{
    std::vector<KeyPageStorage::Ptr> storages;
    auto valueFields = "value1,value2,value3";

    auto stateStorage = make_shared<StateStorage>(nullptr);
    auto prev = std::make_shared<KeyPageStorage>(stateStorage);
    auto tableStorage = std::make_shared<KeyPageStorage>(prev);

    BOOST_REQUIRE(prev->createTable("t_test", valueFields));

    auto table = prev->openTable("t_test");
    BOOST_REQUIRE(table);

    for (size_t i = 0; i < 100; ++i)
    {
        auto entry = table->newEntry();
        entry.importFields({"data" + boost::lexical_cast<std::string>(i)});
        table->setRow("key" + boost::lexical_cast<std::string>(i), entry);
    }
    prev->setReadOnly(true);

    // query 50-150
    std::vector<std::string> keys;
    for (size_t i = 50; i < 150; ++i)
    {
        keys.push_back("key" + boost::lexical_cast<std::string>(i));
    }
    auto queryTable = tableStorage->openTable("t_test");
    BOOST_REQUIRE(queryTable);

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
            BOOST_REQUIRE(entry.has_value());
            BOOST_REQUIRE_EQUAL(entry->dirty(), false);
            BOOST_REQUIRE_GT(entry->size(), 0);
        }
        else
        {
            BOOST_REQUIRE(!entry);
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
            BOOST_REQUIRE(entry.has_value());
            BOOST_REQUIRE_EQUAL(entry->dirty(), true);
        }
        else
        {
            BOOST_REQUIRE(entry.has_value());
            BOOST_REQUIRE_EQUAL(entry->dirty(), false);
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
            BOOST_REQUIRE(!values2[i]);
        }
        else
        {
            BOOST_REQUIRE(values2[i]);
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
        BOOST_REQUIRE(it);
        BOOST_REQUIRE_EQUAL(it->getField(0), "ddd1");
        BOOST_REQUIRE_EQUAL(it->dirty(), true);
    }

    tableStorage->rollback(*recoder);

    auto values4 = queryTable->getRows(keys);
    size_t count = 70;
    for (auto& it : values4)
    {
        BOOST_REQUIRE(it);
        BOOST_REQUIRE_EQUAL(it->getField(0), "data" + boost::lexical_cast<std::string>(count));
        BOOST_REQUIRE_EQUAL(it->dirty(), false);
        ++count;
    }
}

BOOST_AUTO_TEST_CASE(checkVersion)
{
    BOOST_REQUIRE_NO_THROW(tableFactory->createTable("testTable", "value1, value2, value3"));
    auto table = tableFactory->openTable("testTable");

    Entry value1;
    value1.importFields({"v1"});
    table->setRow("abc", std::move(value1));

    Entry value2;
    value2.importFields({"v2"});
    BOOST_REQUIRE_NO_THROW(table->setRow("abc", std::move(value2)));

    Entry value3;
    value3.importFields({"v3"});
    BOOST_REQUIRE_NO_THROW(table->setRow("abc", std::move(value3)));
}

BOOST_AUTO_TEST_CASE(deleteAndGetRows)
{
    KeyPageStorage::Ptr storage1 =
        std::make_shared<KeyPageStorage>(make_shared<StateStorage>(nullptr));

    storage1->asyncCreateTable(
        "table", "value", [](Error::UniquePtr error, std::optional<Table> table) {
            BOOST_REQUIRE(!error);
            BOOST_REQUIRE(table);
        });

    Entry entry1;
    entry1.importFields({"value1"});
    storage1->asyncSetRow(
        "table", "key1", std::move(entry1), [](Error::UniquePtr error) { BOOST_REQUIRE(!error); });

    Entry entry2;
    entry2.importFields({"value2"});
    storage1->asyncSetRow(
        "table", "key2", std::move(entry2), [](Error::UniquePtr error) { BOOST_REQUIRE(!error); });

    storage1->setReadOnly(true);
    KeyPageStorage::Ptr storage2 = std::make_shared<KeyPageStorage>(storage1);
    Entry deleteEntry;
    deleteEntry.setStatus(Entry::DELETED);
    storage2->asyncSetRow("table", "key2", std::move(deleteEntry),
        [](Error::UniquePtr error) { BOOST_REQUIRE(!error); });

    storage2->setReadOnly(true);
    KeyPageStorage::Ptr storage3 = std::make_shared<KeyPageStorage>(storage2);
    storage3->asyncGetPrimaryKeys(
        "table", c, [](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_REQUIRE(!error);
            BOOST_REQUIRE_EQUAL(keys.size(), 1);
            BOOST_REQUIRE_EQUAL(keys[0], "key1");
        });
}

BOOST_AUTO_TEST_CASE(deletedAndGetRow)
{
    KeyPageStorage::Ptr storage1 =
        std::make_shared<KeyPageStorage>(make_shared<StateStorage>(nullptr));

    storage1->asyncCreateTable(
        "table", "value", [](Error::UniquePtr error, std::optional<Table> table) {
            BOOST_REQUIRE(!error);
            BOOST_REQUIRE(table);
        });

    Entry entry1;
    entry1.importFields({"value1"});
    storage1->asyncSetRow(
        "table", "key1", std::move(entry1), [](Error::UniquePtr error) { BOOST_REQUIRE(!error); });

    storage1->setReadOnly(true);
    KeyPageStorage::Ptr storage2 = std::make_shared<KeyPageStorage>(storage1);
    Entry deleteEntry;
    deleteEntry.setStatus(Entry::DELETED);
    storage2->asyncSetRow("table", "key1", std::move(deleteEntry),
        [](Error::UniquePtr error) { BOOST_REQUIRE(!error); });

    storage2->asyncGetRow("table", "key1", [](Error::UniquePtr error, std::optional<Entry> entry) {
        BOOST_REQUIRE(!error);
        BOOST_REQUIRE(!entry);
    });

    storage2->asyncGetRow("table", "key1", [](Error::UniquePtr error, std::optional<Entry> entry) {
        BOOST_REQUIRE(!error);
        BOOST_REQUIRE(!entry);
    });
}

BOOST_AUTO_TEST_CASE(deletedAndGetRows)
{
    KeyPageStorage::Ptr storage1 =
        std::make_shared<KeyPageStorage>(make_shared<StateStorage>(nullptr));

    storage1->asyncCreateTable(
        "table", "value", [](Error::UniquePtr error, std::optional<Table> table) {
            BOOST_REQUIRE(!error);
            BOOST_REQUIRE(table);
        });

    Entry entry1;
    entry1.importFields({"value1"});
    storage1->asyncSetRow(
        "table", "key1", std::move(entry1), [](Error::UniquePtr error) { BOOST_REQUIRE(!error); });

    storage1->setReadOnly(true);
    KeyPageStorage::Ptr storage2 = std::make_shared<KeyPageStorage>(storage1);
    Entry deleteEntry;
    deleteEntry.setStatus(Entry::DELETED);
    storage2->asyncSetRow("table", "key1", std::move(deleteEntry),
        [](Error::UniquePtr error) { BOOST_REQUIRE(!error); });

    std::string_view keys[] = {"key1"};
    storage2->asyncGetRows(
        "table", keys, [](Error::UniquePtr error, std::vector<std::optional<Entry>> entry) {
            BOOST_REQUIRE(!error);
            BOOST_REQUIRE_EQUAL(entry.size(), 1);
            BOOST_REQUIRE(!entry[0]);
        });
}

BOOST_AUTO_TEST_CASE(rollbackAndGetRow)
{
    KeyPageStorage::Ptr storage1 =
        std::make_shared<KeyPageStorage>(make_shared<StateStorage>(nullptr));

    storage1->asyncCreateTable(
        "table", "value", [](Error::UniquePtr error, std::optional<Table> table) {
            BOOST_REQUIRE(!error);
            BOOST_REQUIRE(table);
        });

    Entry entry1;
    entry1.importFields({"value1"});
    storage1->asyncSetRow(
        "table", "key1", std::move(entry1), [](Error::UniquePtr error) { BOOST_REQUIRE(!error); });

    storage1->setReadOnly(true);
    KeyPageStorage::Ptr storage2 = std::make_shared<KeyPageStorage>(storage1);
    auto recoder = std::make_shared<Recoder>();
    storage2->setRecoder(recoder);

    Entry entry2;
    entry2.importFields({"value2"});
    storage2->asyncSetRow(
        "table", "key1", std::move(entry2), [](Error::UniquePtr error) { BOOST_REQUIRE(!error); });

    storage2->asyncGetRow("table", "key1", [](Error::UniquePtr error, std::optional<Entry> entry) {
        BOOST_REQUIRE(!error);
        BOOST_REQUIRE(entry.has_value());
        BOOST_REQUIRE_EQUAL(entry->getField(0), "value2");
    });

    storage2->rollback(*recoder);

    storage2->asyncGetRow("table", "key1", [](Error::UniquePtr error, std::optional<Entry> entry) {
        BOOST_REQUIRE(!error);
        BOOST_REQUIRE(entry.has_value());
        BOOST_REQUIRE_EQUAL(entry->getField(0), "value1");
    });
}

BOOST_AUTO_TEST_CASE(rollbackAndGetRows)
{
    KeyPageStorage::Ptr storage1 =
        std::make_shared<KeyPageStorage>(make_shared<StateStorage>(nullptr));

    storage1->asyncCreateTable(
        "table", "value", [](Error::UniquePtr error, std::optional<Table> table) {
            BOOST_REQUIRE(!error);
            BOOST_REQUIRE(table);
        });

    Entry entry1;
    entry1.importFields({"value1"});
    storage1->asyncSetRow(
        "table", "key1", std::move(entry1), [](Error::UniquePtr error) { BOOST_REQUIRE(!error); });

    storage1->setReadOnly(true);
    KeyPageStorage::Ptr storage2 = std::make_shared<KeyPageStorage>(storage1);
    auto recoder = std::make_shared<Recoder>();
    storage2->setRecoder(recoder);

    Entry entry2;
    entry2.importFields({"value2"});
    storage2->asyncSetRow(
        "table", "key1", std::move(entry2), [](Error::UniquePtr error) { BOOST_REQUIRE(!error); });

    std::string_view keys[] = {"key1"};
    storage2->asyncGetRows(
        "table", keys, [](Error::UniquePtr error, std::vector<std::optional<Entry>> entry) {
            BOOST_REQUIRE(!error);
            BOOST_REQUIRE_EQUAL(entry.size(), 1);
            BOOST_REQUIRE_EQUAL(entry[0].value().getField(0), "value2");
        });

    storage2->rollback(*recoder);

    storage2->asyncGetRows(
        "table", keys, [](Error::UniquePtr error, std::vector<std::optional<Entry>> entry) {
            BOOST_REQUIRE(!error);
            BOOST_REQUIRE_EQUAL(entry.size(), 1);
            BOOST_REQUIRE_EQUAL(entry[0].value().getField(0), "value1");
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
        StateStorageInterface::Ptr prev = make_shared<StateStorage>(nullptr);
        for (size_t i = 0; i < 10; ++i)
        {
            KeyPageStorage::Ptr storage = std::make_shared<KeyPageStorage>(prev);

            for (auto& it : rwSet)
            {
                auto& [write, table, key, value] = it;

                storage->asyncOpenTable(table, [storage, tableName = table](Error::UniquePtr error,
                                                   std::optional<Table> tableItem) {
                    BOOST_REQUIRE(!error);
                    if (!tableItem)
                    {
                        storage->asyncCreateTable(tableName, "value",
                            [](Error::UniquePtr error, std::optional<Table> tableItem) {
                                BOOST_REQUIRE(!error);
                                BOOST_REQUIRE(tableItem);
                            });
                    }
                });

                if (write)
                {
                    Entry entry;
                    entry.importFields({value});
                    storage->asyncSetRow(table, key, std::move(entry),
                        [](Error::UniquePtr error) { BOOST_REQUIRE(!error); });
                }
                else
                {
                    storage->asyncGetRow(
                        table, key, [](Error::UniquePtr error, std::optional<Entry>) {
                            BOOST_REQUIRE(!error);
                        });
                }
            }

            hashes.push_back(storage->hash(hashImpl));
            storage->setReadOnly(false);
            storage->setReadOnly(true);
            prev = storage;
        }

        if (!prevHashes.empty())
        {
            BOOST_REQUIRE_EQUAL_COLLECTIONS(
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
    BOOST_REQUIRE(data.emplace(it, EntryKey("table", std::string_view("key")), 100));

    decltype(data)::const_accessor findIt;
    BOOST_REQUIRE(data.find(findIt, EntryKey("table", std::string_view("key"))));
}

BOOST_AUTO_TEST_CASE(pageMerge)
{
    auto valueFields = "value1";

    auto stateStorage = make_shared<StateStorage>(nullptr);
    StateStorageInterface::Ptr prev = stateStorage;

    auto tableStorage = std::make_shared<KeyPageStorage>(prev, 1024);
    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        BOOST_REQUIRE(tableStorage->createTable(tableName, valueFields));

        auto table = tableStorage->openTable(tableName);
        BOOST_REQUIRE(table);

        for (int k = 0; k < 1000; ++k)
        {
            auto entry = std::make_optional(table->newEntry());
            auto key =
                boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
            entry->setField(0, boost::lexical_cast<std::string>(k));
            BOOST_REQUIRE_NO_THROW(table->setRow(key, *entry));
        }
    }
    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        auto table = tableStorage->openTable(tableName);
        BOOST_REQUIRE(table);

        for (int k = 0; k < 1000; ++k)
        {
            if (k % 5 < 4)
            {
                auto entry = std::make_optional(table->newDeletedEntry());
                auto key =
                    boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
                // BCOS_LOG(TRACE) << LOG_DESC("delete") << LOG_KV("tableName", tableName)
                //                 << LOG_KV("key", key);
                BOOST_REQUIRE_NO_THROW(table->setRow(key, *entry));
            }
        }
    }
    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        auto table = tableStorage->openTable(tableName);
        BOOST_REQUIRE(table);

        for (int k = 0; k < 1000; ++k)
        {
            auto key =
                boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
            auto entry = table->getRow(key);
            // BCOS_LOG(TRACE) << LOG_DESC("getRow") << LOG_KV("tableName", tableName)
            //                 << LOG_KV("key", key);
            if (k % 5 < 4)
            {
                BOOST_REQUIRE(!entry);
            }
            else
            {
                BOOST_REQUIRE(entry.has_value());
                BOOST_REQUIRE(entry->get() == boost::lexical_cast<std::string>(k));
            }
        }
    }
    std::atomic<size_t> totalCount = 0;
    tableStorage->parallelTraverse(false, [&](auto&&, auto&&, auto&&) {
        ++totalCount;
        return true;
    });
    BOOST_REQUIRE_EQUAL(totalCount, 3290);  // meta + 5page + s_table
}

BOOST_AUTO_TEST_CASE(pageMergeRandom)
{
    auto valueFields = "value1";

    auto stateStorage = make_shared<StateStorage>(nullptr);
    StateStorageInterface::Ptr prev = stateStorage;

    auto tableStorage = std::make_shared<KeyPageStorage>(prev, 1024);
    auto entryCount = 1000;
    auto tableCount = 100;
#if defined(__APPLE__)
#pragma omp parallel for
#endif
    for (int j = 0; j < tableCount; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        BOOST_REQUIRE(tableStorage->createTable(tableName, valueFields));

        auto table = tableStorage->openTable(tableName);
        BOOST_REQUIRE(table);

        for (int k = 0; k < entryCount; ++k)
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
    for (int j = 0; j < tableCount; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        auto table = tableStorage->openTable(tableName);
        BOOST_REQUIRE(table);

        for (int k = 0; k < entryCount; ++k)
        {
            if (k % 5 < 4)
            {
                auto entry = std::make_optional(table->newDeletedEntry());
                auto key =
                    boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
                BCOS_LOG(TRACE) << LOG_DESC("delete") << LOG_KV("tableName", tableName)
                                << LOG_KV("key", key);
                // BOOST_REQUIRE_NO_THROW(table->setRow(key, *entry));
                table->setRow(key, *entry);
            }
        }
    }
#if defined(__APPLE__)
#pragma omp parallel for
#endif
    for (int j = 0; j < tableCount; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        auto table = tableStorage->openTable(tableName);
        BOOST_REQUIRE(table);

        for (int k = 0; k < entryCount; ++k)
        {
            auto key =
                boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
            auto entry = table->getRow(key);
            BCOS_LOG(TRACE) << LOG_DESC("getRow") << LOG_KV("tableName", tableName)
                            << LOG_KV("key", key);
            if (k % 5 < 4)
            {
                BOOST_REQUIRE(!entry);
            }
            else
            {
                BOOST_REQUIRE(entry.has_value());
                BOOST_REQUIRE(entry->get() == boost::lexical_cast<std::string>(k));
            }
        }
    }
    std::atomic<size_t> totalCount = 0;
    tableStorage->parallelTraverse(false, [&](auto&&, auto&&, auto&&) {
        ++totalCount;
        return true;
    });
    BOOST_REQUIRE_EQUAL(totalCount, 3290);  // meta + 5page + s_table
}

BOOST_AUTO_TEST_CASE(pageMergeParallelRandom)
{
    auto valueFields = "value1";

    auto stateStorage = make_shared<StateStorage>(nullptr);
    StateStorageInterface::Ptr prev = stateStorage;

    auto tableStorage = std::make_shared<KeyPageStorage>(prev, 1024);
    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        BOOST_REQUIRE(tableStorage->createTable(tableName, valueFields));

        auto table = tableStorage->openTable(tableName);
        BOOST_REQUIRE(table);

#if defined(__APPLE__)
#pragma omp parallel for
#endif
        for (int k = 0; k < 1000; ++k)
        {
            auto entry = std::make_optional(table->newEntry());
            auto key =
                boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
            entry->setField(0, boost::lexical_cast<std::string>(k));
            BOOST_REQUIRE_NO_THROW(table->setRow(key, *entry));
        }
    }
    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        auto table = tableStorage->openTable(tableName);
        BOOST_REQUIRE(table);

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
                BOOST_REQUIRE_NO_THROW(table->setRow(key, *entry));
            }
        }
    }

    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        auto table = tableStorage->openTable(tableName);
        BOOST_REQUIRE(table);
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
                BOOST_REQUIRE(!entry);
            }
            else
            {
                BOOST_REQUIRE(entry.has_value());
                BOOST_REQUIRE(entry->get() == boost::lexical_cast<std::string>(k));
            }
        }
    }
    // std::atomic<size_t> totalCount = 0;
    // tableStorage->parallelTraverse(false, [&](auto&&, auto&&, auto&&) {
    //     ++totalCount;
    //     return true;
    // });
    // BOOST_REQUIRE_EQUAL(totalCount  , 393);  // meta + 5page + s_table
}

BOOST_AUTO_TEST_CASE(parallelMix)
{
    auto valueFields = "value1";

    auto stateStorage = make_shared<StateStorage>(nullptr);
    StateStorageInterface::Ptr prev = stateStorage;

    auto tableStorage = std::make_shared<KeyPageStorage>(prev, 1024);
    // #pragma omp parallel for
    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        BOOST_REQUIRE(tableStorage->createTable(tableName, valueFields));

        auto table = tableStorage->openTable(tableName);
        BOOST_REQUIRE(table);
#if defined(__APPLE__)
#pragma omp parallel for
#endif
        for (int k = 0; k < 1000; ++k)
        {
            auto entry = std::make_optional(table->newEntry());
            auto key =
                boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
            entry->setField(0, boost::lexical_cast<std::string>(k));
            BOOST_REQUIRE_NO_THROW(table->setRow(key, *entry));
        }
    }
    // #pragma omp parallel for
    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        auto table = tableStorage->openTable(tableName);
        BOOST_REQUIRE(table);
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
                BOOST_REQUIRE_NO_THROW(table->setRow(key, *entry));
            }
            else
            {
                auto key =
                    boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
                auto entry = table->getRow(key);
                auto value = boost::lexical_cast<std::string>(k);
                BOOST_REQUIRE(entry.has_value());
                BOOST_REQUIRE(entry->get() == value);
                auto deleteEntry = std::make_optional(table->newDeletedEntry());
                // BCOS_LOG(TRACE) << LOG_DESC("delete") << LOG_KV("tableName", tableName)
                //                 << LOG_KV("key", key);
                BOOST_REQUIRE_NO_THROW(table->setRow(key, *deleteEntry));
            }
        }
    }
    // #pragma omp parallel for
    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        auto table = tableStorage->openTable(tableName);
        BOOST_REQUIRE(table);
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
                BOOST_REQUIRE(!entry);
            }
            else
            {
                BOOST_REQUIRE(entry.has_value());
                BOOST_REQUIRE(entry->get() == boost::lexical_cast<std::string>(k + 1));
            }
        }
    }
    // std::atomic<size_t> totalCount = 0;
    // tableStorage->parallelTraverse(false, [&](auto&&, auto&&, auto&&) {
    //     ++totalCount;
    //     return true;
    // });
    // BOOST_REQUIRE_EQUAL(totalCount  , 392);  // meta + 5page + s_table
}

BOOST_AUTO_TEST_CASE(pageSplit)
{
    auto valueFields = "value1";

    auto stateStorage = make_shared<StateStorage>(nullptr);
    StateStorageInterface::Ptr prev = stateStorage;

    auto tableStorage = std::make_shared<KeyPageStorage>(prev, 1024);
    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        BOOST_REQUIRE(tableStorage->createTable(tableName, valueFields));

        auto table = tableStorage->openTable(tableName);
        BOOST_REQUIRE(table);

        for (int k = 0; k < 1000; ++k)
        {
            auto entry = std::make_optional(table->newEntry());
            auto key =
                boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
            entry->setField(0, boost::lexical_cast<std::string>(k));
            BOOST_REQUIRE_NO_THROW(table->setRow(key, *entry));
        }
    }
    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        auto table = tableStorage->openTable(tableName);
        BOOST_REQUIRE(table);

        for (int k = 0; k < 1000; ++k)
        {
            auto key =
                boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
            auto entry = table->getRow(key);
            // BCOS_LOG(TRACE) << LOG_DESC("getRow") << LOG_KV("tableName", tableName)
            //                 << LOG_KV("key", key);
            BOOST_REQUIRE(entry.has_value());
            BOOST_REQUIRE(entry->get() == boost::lexical_cast<std::string>(k));
        }
    }
    std::atomic<size_t> totalCount = 0;
    tableStorage->parallelTraverse(false, [&](auto&&, auto&&, auto&&) {
        ++totalCount;
        return true;
    });
    BOOST_REQUIRE_EQUAL(totalCount, 3290);  // meta + 5page + s_table
}

BOOST_AUTO_TEST_CASE(pageSplitRandom)
{
    auto valueFields = "value1";

    auto stateStorage = make_shared<StateStorage>(nullptr);
    StateStorageInterface::Ptr prev = stateStorage;

    auto tableStorage = std::make_shared<KeyPageStorage>(prev, 256);
#if defined(__APPLE__)
#pragma omp parallel for
#endif
    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        BOOST_REQUIRE(tableStorage->createTable(tableName, valueFields));

        auto table = tableStorage->openTable(tableName);
        BOOST_REQUIRE(table);

        for (int k = 0; k < 5000; ++k)
        {
            auto entry = std::make_optional(table->newEntry());
            auto key =
                boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
            entry->setField(0, boost::lexical_cast<std::string>(k));
            BOOST_REQUIRE_NO_THROW(table->setRow(key, *entry));
        }
    }
#if defined(__APPLE__)
#pragma omp parallel for
#endif
    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        auto table = tableStorage->openTable(tableName);
        BOOST_REQUIRE(table);

        for (int k = 0; k < 5000; ++k)
        {
            auto key =
                boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
            auto entry = table->getRow(key);
            // BCOS_LOG(TRACE) << LOG_DESC("getRow") << LOG_KV("tableName", tableName)
            //                 << LOG_KV("key", key);
            BOOST_REQUIRE(entry.has_value());
            BOOST_REQUIRE(entry->get() == boost::lexical_cast<std::string>(k));
        }
    }
    std::atomic<size_t> totalCount = 0;
    tableStorage->parallelTraverse(false, [&](auto&&, auto&&, auto&&) {
        ++totalCount;
        return true;
    });
    BOOST_REQUIRE_EQUAL(totalCount, 8830);  // meta + 5page + s_table
}

BOOST_AUTO_TEST_CASE(pageSplitParallelRandom)
{
    auto valueFields = "value1";

    auto stateStorage = make_shared<StateStorage>(nullptr);
    StateStorageInterface::Ptr prev = stateStorage;

    auto tableStorage = std::make_shared<KeyPageStorage>(prev, 256);
    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        BOOST_REQUIRE(tableStorage->createTable(tableName, valueFields));

        auto table = tableStorage->openTable(tableName);
        BOOST_REQUIRE(table);
#if defined(__APPLE__)
#pragma omp parallel for
#endif
        for (int k = 0; k < 500; ++k)
        {
            auto entry = std::make_optional(table->newEntry());
            auto key =
                boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
            entry->setField(0, boost::lexical_cast<std::string>(k));
            BOOST_REQUIRE_NO_THROW(table->setRow(key, *entry));
        }
    }

    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        auto table = tableStorage->openTable(tableName);
        BOOST_REQUIRE(table);
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
            BOOST_REQUIRE(entry.has_value());
            BOOST_REQUIRE(entry->get() == boost::lexical_cast<std::string>(k));
        }
    }
    // std::atomic<size_t> totalCount = 0;
    // tableStorage->parallelTraverse(false, [&](auto&&, auto&&, auto&&) {
    //     ++totalCount;
    //     return true;
    // });
    // BOOST_REQUIRE_EQUAL(totalCount  , 999);  // meta + 5page + s_table
}


BOOST_AUTO_TEST_CASE(asyncGetPrimaryKeys)
{  // TODO: add ut for asyncGetPrimaryKeys and condition
    auto valueFields = "value1";

    auto stateStorage = make_shared<StateStorage>(nullptr);
    StateStorageInterface::Ptr prev = stateStorage;

    auto tableStorage = std::make_shared<KeyPageStorage>(prev, 256);
    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        BOOST_REQUIRE(tableStorage->createTable(tableName, valueFields));

        auto table = tableStorage->openTable(tableName);
        BOOST_REQUIRE(table);
#if defined(__APPLE__)
#pragma omp parallel for
#endif
        for (int k = 0; k < 1000; ++k)
        {
            auto entry = std::make_optional(table->newEntry());
            auto key = boost::lexical_cast<std::string>(k);
            entry->setField(0, boost::lexical_cast<std::string>(k));
            BOOST_REQUIRE_NO_THROW(table->setRow(key, *entry));
        }
    }

    for (int j = 0; j < 100; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        auto table = tableStorage->openTable(tableName);
        BOOST_REQUIRE(table);
#if defined(__APPLE__)
#pragma omp parallel for
#endif
        for (int k = 0; k < 1000; ++k)
        {
            Condition c;
            c.limit(0, 200);
            auto keys = table->getPrimaryKeys(c);
            BOOST_REQUIRE(keys.size() == 200);
            c.limit(200, 300);
            keys = table->getPrimaryKeys(c);
            BOOST_REQUIRE(keys.size() == 300);
            c.limit(900, 200);
            keys = table->getPrimaryKeys(c);
            BOOST_REQUIRE(keys.size() == 100);
            c.GE("900");
            BOOST_REQUIRE(keys.size() == 100);
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
    BOOST_REQUIRE(keys.size() == 170);
    Condition c2;
    c2.limit(0, 500);
    c2.LT("250");
    auto keys2 = table->getPrimaryKeys(c2);
    BCOS_LOG(TRACE) << "LT 250:" << printVector(keys2);
    BOOST_REQUIRE(keys2.size() == 169);
    Condition c3;
    c3.limit(750, 500);
    c3.GT("250");
    auto keys3 = table->getPrimaryKeys(c3);
    BCOS_LOG(TRACE) << "GT 250:" << printVector(keys3);
    BOOST_REQUIRE(keys3.size() == 80);
    Condition c4;
    c4.limit(750, 500);
    c4.GE("250");
    auto keys4 = table->getPrimaryKeys(c4);
    BCOS_LOG(TRACE) << "GE 250:" << printVector(keys4);
    BOOST_REQUIRE(keys4.size() == 81);
    Condition c5;
    c5.limit(500, 500);
    c5.NE("250");
    auto keys5 = table->getPrimaryKeys(c5);
    BCOS_LOG(TRACE) << "NE 250:" << printVector(keys5);
    BOOST_REQUIRE(keys5.size() == 499);
}


BOOST_AUTO_TEST_CASE(BigTableAdd)
{
    auto valueFields = "value1";
    auto cacheSize = 256 * 1024 * 1024;
    auto pageSize = 512;
    auto stateStorage0 = make_shared<LRUStateStorage>(nullptr);
    stateStorage0->setMaxCapacity(cacheSize);
    StateStorageInterface::Ptr prev0 = stateStorage0;

    auto tableName = "table_0";
    BOOST_REQUIRE(prev0->createTable(tableName, valueFields));

    auto stateStorage1 = make_shared<LRUStateStorage>(nullptr);
    stateStorage1->setMaxCapacity(cacheSize);
    StateStorageInterface::Ptr prev1 = stateStorage1;

    BOOST_REQUIRE(prev1->createTable(tableName, valueFields));

    size_t count = 100;
    auto keyCount = 100;
    auto index = 0;
    srand(time(NULL));
    for (size_t i = 0; i < count; ++i)
    {
        auto tableStorage0 = std::make_shared<KeyPageStorage>(prev0, pageSize);
        auto table0 = tableStorage0->openTable(tableName);
        BOOST_REQUIRE(table0);

        auto tableStorage1 = std::make_shared<KeyPageStorage>(prev1, pageSize);
        auto table1 = tableStorage1->openTable(tableName);
        BOOST_REQUIRE(table1);

#if defined(__APPLE__)
#pragma omp parallel for
#endif
        for (int k = 0; k < keyCount; ++k)
        {
            auto v = rand() + index + k;
            auto key = boost::lexical_cast<std::string>(v);
            auto value =
                boost::lexical_cast<std::string>(i) + "_" + boost::lexical_cast<std::string>(v);
            auto entry0 = std::make_optional(table0->newEntry());
            entry0->setField(0, value);
            BOOST_REQUIRE_NO_THROW(table0->setRow(key, *entry0));

            auto entry1 = std::make_optional(table1->newEntry());
            entry1->setField(0, value);
            BOOST_REQUIRE_NO_THROW(table1->setRow(key, *entry1));
        }
        auto hash0 = tableStorage0->hash(hashImpl);
        BCOS_LOG(DEBUG) << LOG_DESC(">>>>>>>>>>>>KeyPageStorage0") << LOG_KV("i", i)
                        << LOG_KV("hash", hash0.hex());
        BOOST_REQUIRE(hash0.hex() != crypto::HashType(0).hex());
        auto hash1 = tableStorage1->hash(hashImpl);
        BCOS_LOG(DEBUG) << LOG_DESC(">>>>>>>>>>>>KeyPageStorage1") << LOG_KV("i", i)
                        << LOG_KV("hash", hash1.hex());
        BOOST_REQUIRE(hash1.hex() != crypto::HashType(0).hex());
        BOOST_TEST(hash0.hex() != crypto::HashType(0).hex());
        BOOST_TEST(hash0.hex() == hash1.hex());
        BOOST_REQUIRE(hash0.hex() == hash1.hex());
        tableStorage0->setReadOnly(true);
        tableStorage1->setReadOnly(true);
        stateStorage0->merge(true, *tableStorage0);
        stateStorage1->merge(true, *tableStorage1);
        hash0 = stateStorage0->hash(hashImpl);
        BCOS_LOG(DEBUG) << LOG_DESC(">>>>>>>>>>>>stateStorage0") << LOG_KV("i", i)
                        << LOG_KV("hash", hash0.hex());
        hash1 = stateStorage1->hash(hashImpl);
        BCOS_LOG(DEBUG) << LOG_DESC(">>>>>>>>>>>>stateStorage1") << LOG_KV("i", i)
                        << LOG_KV("hash", hash1.hex());
        // BOOST_TEST(hash0.hex() == hash1.hex());
        // BOOST_REQUIRE(hash0.hex() == hash1.hex());
        index += keyCount;
        BCOS_LOG(DEBUG) << LOG_DESC("<<<<<<<<<<<<<<<<<<<<<<\n\n\n\n\n\n\n\n\n");
    }
}

BOOST_AUTO_TEST_CASE(BigTableAddSerialize)
{
    auto valueFields = "value1";
    auto cacheSize = 256 * 1024 * 1024;
    auto pageSize = 512;
    auto stateStorage0 = make_shared<LRUStateStorage>(nullptr);
    stateStorage0->setMaxCapacity(cacheSize);
    StateStorageInterface::Ptr prev0 = stateStorage0;

    auto tableName = "table_0";
    BOOST_REQUIRE(prev0->createTable(tableName, valueFields));

    auto stateStorage1 = make_shared<LRUStateStorage>(nullptr);
    stateStorage1->setMaxCapacity(cacheSize);
    StateStorageInterface::Ptr prev1 = stateStorage1;

    BOOST_REQUIRE(prev1->createTable(tableName, valueFields));

    size_t count = 1;
    auto keyCount = 100;
    auto index = 0;
    srand(time(NULL));
    for (size_t i = 0; i < count; ++i)
    {
        auto tableStorage0 = std::make_shared<KeyPageStorage>(prev0, pageSize);
        auto table0 = tableStorage0->openTable(tableName);
        BOOST_REQUIRE(table0);

        auto tableStorage1 = std::make_shared<KeyPageStorage>(prev1, pageSize);
        auto table1 = tableStorage1->openTable(tableName);
        BOOST_REQUIRE(table1);

        for (int k = 0; k < keyCount; ++k)
        {
            auto v = rand() + index + k;
            auto key = boost::lexical_cast<std::string>(v);
            auto value =
                boost::lexical_cast<std::string>(i) + "_" + boost::lexical_cast<std::string>(v);
            auto entry0 = std::make_optional(table0->newEntry());
            entry0->setField(0, value);
            BOOST_REQUIRE_NO_THROW(table0->setRow(key, *entry0));

            auto entry1 = std::make_optional(table1->newEntry());
            entry1->setField(0, value);
            BOOST_REQUIRE_NO_THROW(table1->setRow(key, *entry1));
        }
        auto hash0 = tableStorage0->hash(hashImpl);
        BCOS_LOG(DEBUG) << LOG_DESC(">>>>>>>>>>>>KeyPageStorage0") << LOG_KV("i", i)
                        << LOG_KV("hash", hash0.hex());
        BOOST_REQUIRE(hash0.hex() != crypto::HashType(0).hex());
        auto hash1 = tableStorage1->hash(hashImpl);
        BCOS_LOG(DEBUG) << LOG_DESC(">>>>>>>>>>>>KeyPageStorage1") << LOG_KV("i", i)
                        << LOG_KV("hash", hash1.hex());
        BOOST_REQUIRE(hash0.hex() != crypto::HashType(0).hex());
        BOOST_TEST(hash0.hex() == hash1.hex());
        BOOST_REQUIRE(hash0.hex() == hash1.hex());
        tableStorage0->setReadOnly(true);
        tableStorage1->setReadOnly(true);
        stateStorage0->merge(true, *tableStorage0);
        stateStorage1->merge(true, *tableStorage1);
        hash0 = stateStorage0->hash(hashImpl);
        BCOS_LOG(DEBUG) << LOG_DESC(">>>>>>>>>>>>stateStorage0") << LOG_KV("i", i)
                        << LOG_KV("hash", hash0.hex());
        hash1 = stateStorage1->hash(hashImpl);
        BCOS_LOG(DEBUG) << LOG_DESC(">>>>>>>>>>>>stateStorage1") << LOG_KV("i", i)
                        << LOG_KV("hash", hash1.hex());
        BOOST_TEST(hash0.hex() == hash1.hex());
        BOOST_REQUIRE(hash0.hex() == hash1.hex());
        index += keyCount;
        BCOS_LOG(DEBUG) << LOG_DESC("<<<<<<<<<<<<<<<<<<<<<<\n\n\n\n\n\n\n\n\n");
    }
}

BOOST_AUTO_TEST_CASE(mockCommitProcess)
{
    auto valueFields = "value1";
    auto cacheSize = 256 * 1024 * 1024;
    auto pageSize = 512;
    auto stateStorage0 = make_shared<LRUStateStorage>(nullptr);
    stateStorage0->setMaxCapacity(cacheSize);
    StateStorageInterface::Ptr prev0 = stateStorage0;

    auto tableName = "table_0";
    BOOST_REQUIRE(prev0->createTable(tableName, valueFields));

    auto stateStorage1 = make_shared<LRUStateStorage>(nullptr);
    stateStorage1->setMaxCapacity(cacheSize);
    StateStorageInterface::Ptr prev1 = stateStorage1;
    BOOST_REQUIRE(prev1->createTable(tableName, valueFields));

    auto stateStorage2 = make_shared<LRUStateStorage>(nullptr);
    stateStorage2->setMaxCapacity(cacheSize);
    StateStorageInterface::Ptr prev2 = stateStorage2;
    BOOST_REQUIRE(prev2->createTable(tableName, valueFields));


    size_t count = 100;
    auto keyCount = 1000;
    auto index = 0;
    srand(time(NULL));
    // std::list<KeyPageStorage::Ptr> keypages0;
    std::list<KeyPageStorage::Ptr> keypages1;
    std::list<KeyPageStorage::Ptr> keypages2;
    for (size_t i = 0; i < count; ++i)
    {
        auto tableStorage0 = std::make_shared<KeyPageStorage>(prev0, pageSize);
        auto table0 = tableStorage0->openTable(tableName);
        BOOST_REQUIRE(table0);

        auto tableStorage1 = std::make_shared<KeyPageStorage>(prev1, pageSize);
        auto table1 = tableStorage1->openTable(tableName);
        BOOST_REQUIRE(table1);

        auto tableStorage2 = std::make_shared<KeyPageStorage>(prev2, pageSize);
        auto table2 = tableStorage2->openTable(tableName);
        BOOST_REQUIRE(table2);

        for (int k = 0; k < keyCount; ++k)
        {
            auto v = rand() + index + k;
            auto key = boost::lexical_cast<std::string>(v);
            auto value =
                boost::lexical_cast<std::string>(i) + "_" + boost::lexical_cast<std::string>(v);
            auto getKey = boost::lexical_cast<std::string>(index + k);
            auto entry0 = std::make_optional(table0->newEntry());
            entry0->setField(0, value);
            BOOST_REQUIRE_NO_THROW(table0->setRow(key, *entry0));
            entry0 = table0->getRow(key);
            BOOST_REQUIRE(entry0);
            entry0 = table0->getRow(getKey);

            auto entry1 = std::make_optional(table1->newEntry());
            entry1->setField(0, value);
            BOOST_REQUIRE_NO_THROW(table1->setRow(key, *entry1));
            entry1 = table1->getRow(key);
            BOOST_REQUIRE(entry1);
            entry1 = table1->getRow(getKey);

            auto entry2 = std::make_optional(table2->newEntry());
            entry2->setField(0, value);
            BOOST_REQUIRE_NO_THROW(table2->setRow(key, *entry2));
            entry2 = table2->getRow(key);
            BOOST_REQUIRE(entry2);
            entry2 = table2->getRow(getKey);
        }
        auto hash0 = tableStorage0->hash(hashImpl);
        BCOS_LOG(DEBUG) << LOG_DESC(">>>>>>>>>>>>KeyPageStorage0") << LOG_KV("i", i)
                        << LOG_KV("hash", hash0.hex());
        BOOST_REQUIRE(hash0.hex() != crypto::HashType(0).hex());
        auto hash1 = tableStorage1->hash(hashImpl);
        BCOS_LOG(DEBUG) << LOG_DESC(">>>>>>>>>>>>KeyPageStorage1") << LOG_KV("i", i)
                        << LOG_KV("hash", hash1.hex());
        BOOST_REQUIRE(hash1.hex() != crypto::HashType(0).hex());
        auto hash2 = tableStorage2->hash(hashImpl);
        BCOS_LOG(DEBUG) << LOG_DESC(">>>>>>>>>>>>KeyPageStorage2") << LOG_KV("i", i)
                        << LOG_KV("hash", hash2.hex());
        BOOST_TEST(hash0.hex() == hash1.hex());
        BOOST_TEST(hash2.hex() == hash1.hex());
        BOOST_REQUIRE(hash0.hex() == hash1.hex());
        BOOST_REQUIRE(hash2.hex() == hash1.hex());

        tableStorage0->setReadOnly(true);
        tableStorage1->setReadOnly(true);
        tableStorage2->setReadOnly(true);
        keypages1.push_back(tableStorage1);
        keypages2.push_back(tableStorage2);
        prev1 = tableStorage1;
        prev2 = tableStorage2;
        // 0 always commit
        stateStorage0->merge(true, *tableStorage0);
        if (i % 2 == 0)
        {  // 50% commit
            auto s = keypages1.front();
            stateStorage1->merge(true, *s);
            keypages1.pop_front();
        }

        if (rand() % 3 == 0)
        {  // random commit
            auto s = keypages2.front();
            stateStorage2->merge(true, *s);
            keypages2.pop_front();
        }
        index += keyCount;
        BCOS_LOG(DEBUG) << LOG_DESC("<<<<<<<<<<<<<<<<<<<<<<\n\n\n\n\n\n\n\n\n");
    }
}

BOOST_AUTO_TEST_CASE(mockCommitProcessParallel)
{
    auto valueFields = "value1";
    auto cacheSize = 256 * 1024 * 1024;
    auto pageSize = 512;
    auto stateStorage0 = make_shared<LRUStateStorage>(nullptr);
    stateStorage0->setMaxCapacity(cacheSize);
    StateStorageInterface::Ptr prev0 = stateStorage0;

    auto tableName = "table_0";
    BOOST_REQUIRE(prev0->createTable(tableName, valueFields));

    auto stateStorage1 = make_shared<LRUStateStorage>(nullptr);
    stateStorage1->setMaxCapacity(cacheSize);
    StateStorageInterface::Ptr prev1 = stateStorage1;
    BOOST_REQUIRE(prev1->createTable(tableName, valueFields));

    auto stateStorage2 = make_shared<LRUStateStorage>(nullptr);
    stateStorage2->setMaxCapacity(cacheSize);
    StateStorageInterface::Ptr prev2 = stateStorage2;
    BOOST_REQUIRE(prev2->createTable(tableName, valueFields));


    size_t count = 10;
    auto keyCount = 1000;
    auto index = 0;
    srand(time(NULL));
    // std::list<KeyPageStorage::Ptr> keypages0;
    std::list<KeyPageStorage::Ptr> keypages1;
    std::list<KeyPageStorage::Ptr> keypages2;
    for (size_t i = 0; i < count; ++i)
    {
        auto tableStorage0 = std::make_shared<KeyPageStorage>(prev0, pageSize);
        auto table0 = tableStorage0->openTable(tableName);
        BOOST_REQUIRE(table0);

        auto tableStorage1 = std::make_shared<KeyPageStorage>(prev1, pageSize);
        auto table1 = tableStorage1->openTable(tableName);
        BOOST_REQUIRE(table1);

        auto tableStorage2 = std::make_shared<KeyPageStorage>(prev2, pageSize);
        auto table2 = tableStorage2->openTable(tableName);
        BOOST_REQUIRE(table2);

#if defined(__APPLE__)
#pragma omp parallel for
#endif
        for (int k = 0; k < keyCount; ++k)
        {
            auto v = rand() + index + k;
            auto key = boost::lexical_cast<std::string>(v);
            auto value =
                boost::lexical_cast<std::string>(i) + "_" + boost::lexical_cast<std::string>(v);
            auto getKey = boost::lexical_cast<std::string>(index + k);
            auto entry0 = std::make_optional(table0->newEntry());
            entry0->setField(0, value);
            BOOST_REQUIRE_NO_THROW(table0->setRow(key, *entry0));
            entry0 = table0->getRow(key);
            BOOST_REQUIRE(entry0);
            entry0 = table0->getRow(getKey);

            auto entry1 = std::make_optional(table1->newEntry());
            entry1->setField(0, value);
            BOOST_REQUIRE_NO_THROW(table1->setRow(key, *entry1));
            entry1 = table1->getRow(key);
            BOOST_REQUIRE(entry1);
            entry1 = table1->getRow(getKey);

            auto entry2 = std::make_optional(table2->newEntry());
            entry2->setField(0, value);
            BOOST_REQUIRE_NO_THROW(table2->setRow(key, *entry2));
            entry2 = table2->getRow(key);
            BOOST_REQUIRE(entry2);
            entry2 = table2->getRow(getKey);
        }
        auto hash0 = tableStorage0->hash(hashImpl);
        BCOS_LOG(DEBUG) << LOG_DESC(">>>>>>>>>>>>KeyPageStorage0") << LOG_KV("i", i)
                        << LOG_KV("hash", hash0.hex());
        BOOST_REQUIRE(hash0.hex() != crypto::HashType(0).hex());
        auto hash1 = tableStorage1->hash(hashImpl);
        BCOS_LOG(DEBUG) << LOG_DESC(">>>>>>>>>>>>KeyPageStorage1") << LOG_KV("i", i)
                        << LOG_KV("hash", hash1.hex());
        BOOST_REQUIRE(hash1.hex() != crypto::HashType(0).hex());
        auto hash2 = tableStorage2->hash(hashImpl);
        BCOS_LOG(DEBUG) << LOG_DESC(">>>>>>>>>>>>KeyPageStorage2") << LOG_KV("i", i)
                        << LOG_KV("hash", hash2.hex());
        BOOST_TEST(hash0.hex() == hash1.hex());
        BOOST_TEST(hash2.hex() == hash1.hex());
        BOOST_REQUIRE(hash0.hex() == hash1.hex());
        BOOST_REQUIRE(hash2.hex() == hash1.hex());

        tableStorage0->setReadOnly(true);
        tableStorage1->setReadOnly(true);
        tableStorage2->setReadOnly(true);
        keypages1.push_back(tableStorage1);
        keypages2.push_back(tableStorage2);
        prev1 = tableStorage1;
        prev2 = tableStorage2;
        // 0 always commit
        stateStorage0->merge(true, *tableStorage0);
        if (i % 2 == 0)
        {  // 50% commit
            auto s = keypages1.front();
            stateStorage1->merge(true, *s);
            keypages1.pop_front();
        }

        if (rand() % 3 == 0)
        {  // random commit
            auto s = keypages2.front();
            stateStorage2->merge(true, *s);
            keypages2.pop_front();
        }
        index += keyCount;
        BCOS_LOG(DEBUG) << LOG_DESC("<<<<<<<<<<<<<<<<<<<<<<\n\n\n\n\n\n\n\n\n");
    }
}

BOOST_AUTO_TEST_CASE(pageMergeBig)
{
    auto valueFields = "value1";

    auto stateStorage = make_shared<StateStorage>(nullptr);
    StateStorageInterface::Ptr prev = stateStorage;

    auto tableStorage = std::make_shared<KeyPageStorage>(prev, 1024);
    auto tableCount = 5;
    auto rowCount = 20000;
    srand(time(NULL));
    std::vector<std::unordered_map<int, bool>> keys;
    keys.resize(tableCount);
#if defined(__APPLE__)
#pragma omp parallel for
#endif
    for (int j = 0; j < tableCount; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        BOOST_REQUIRE(tableStorage->createTable(tableName, valueFields));

        auto table = tableStorage->openTable(tableName);
        BOOST_REQUIRE(table);

        for (int k = 0; k < rowCount; ++k)
        {
            keys[j][k] = true;
            auto entry = std::make_optional(table->newEntry());
            auto key =
                boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
            entry->setField(0, boost::lexical_cast<std::string>(k));
            BOOST_REQUIRE_NO_THROW(table->setRow(key, *entry));
        }
    }

#if defined(__APPLE__)
#pragma omp parallel for
#endif
    for (int j = 0; j < tableCount; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        auto table = tableStorage->openTable(tableName);
        BOOST_REQUIRE(table);
        for (int k = rowCount - 1; k >= 0; --k)
        {
            if (k % 5 < 4)
            {
                keys[j][k] = false;
                auto entry = std::make_optional(table->newDeletedEntry());
                auto key =
                    boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
                // BCOS_LOG(TRACE) << LOG_DESC("delete") << LOG_KV("tableName", tableName)
                //                 << LOG_KV("key", key);
                BOOST_REQUIRE_NO_THROW(table->setRow(key, *entry));
            }
        }

        for (int k = rowCount - 1; k >= 0; --k)
        {
            if (rand() % 2 == 0)
            {
                keys[j][k] = true;
                auto entry = std::make_optional(table->newEntry());
                auto key =
                    boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
                entry->setField(0, boost::lexical_cast<std::string>(k));
                BOOST_REQUIRE_NO_THROW(table->setRow(key, *entry));
            }
        }

        for (int k = 0; k < rowCount; ++k)
        {
            if (rand() % 3 == 0)
            {
                keys[j][k] = true;
                auto entry = std::make_optional(table->newEntry());
                auto key =
                    boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
                entry->setField(0, boost::lexical_cast<std::string>(k));
                BOOST_REQUIRE_NO_THROW(table->setRow(key, *entry));
            }
        }

        for (int k = rowCount - 1; k >= 0; --k)
        {
            if (k % 5 < 4)
            {
                keys[j][k] = false;
                auto entry = std::make_optional(table->newDeletedEntry());
                auto key =
                    boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
                // BCOS_LOG(TRACE) << LOG_DESC("delete") << LOG_KV("tableName", tableName)
                //                 << LOG_KV("key", key);
                BOOST_REQUIRE_NO_THROW(table->setRow(key, *entry));
            }
        }

        for (int k = 0; k < rowCount; ++k)
        {
            if (rand() % 3 == 0)
            {
                keys[j][k] = true;
                auto entry = std::make_optional(table->newEntry());
                auto key =
                    boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
                entry->setField(0, boost::lexical_cast<std::string>(k));
                BOOST_REQUIRE_NO_THROW(table->setRow(key, *entry));
            }
        }
        for (int k = 0; k < rowCount; ++k)
        {
            if (rand() % 3 == 0)
            {
                keys[j][k] = false;
                auto entry = std::make_optional(table->newDeletedEntry());
                auto key =
                    boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
                // BCOS_LOG(TRACE) << LOG_DESC("delete") << LOG_KV("tableName", tableName)
                //                 << LOG_KV("key", key);
                BOOST_REQUIRE_NO_THROW(table->setRow(key, *entry));
            }
        }
    }

    for (int j = 0; j < tableCount; ++j)
    {
        auto tableName = "table_" + boost::lexical_cast<std::string>(j);
        auto table = tableStorage->openTable(tableName);
        BOOST_REQUIRE(table);

        for (int k = 0; k < rowCount; ++k)
        {
            auto key =
                boost::lexical_cast<std::string>(j) + "_" + boost::lexical_cast<std::string>(k);
            auto entry = table->getRow(key);
            // BCOS_LOG(TRACE) << LOG_DESC("getRow") << LOG_KV("tableName", tableName)
            //                 << LOG_KV("key", key);
            if (keys[j][k])
            {
                BOOST_REQUIRE(entry.has_value());
                BOOST_REQUIRE(entry->get() == boost::lexical_cast<std::string>(k));
            }
        }
    }
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
