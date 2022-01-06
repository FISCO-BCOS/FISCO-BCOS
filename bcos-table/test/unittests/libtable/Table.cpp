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
 * @brief Unit tests for the Table
 * @file Table.cpp
 */

#include "bcos-framework/interfaces/storage/Table.h"
#include "Hash.h"
#include "bcos-framework/interfaces/crypto/CommonType.h"
#include "bcos-framework/interfaces/storage/StorageInterface.h"
#include "bcos-table/StateStorage.h"
#include "bcos-utilities/ThreadPool.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <iostream>
#include <optional>
#include <string>

using namespace std;
using namespace bcos;
using namespace bcos::storage;
using namespace bcos::crypto;

namespace std
{
inline ostream& operator<<(ostream& os, const tuple<string, crypto::HashType>& item)
{
    os << get<0>(item) << " " << get<1>(item);
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
}  // namespace std

namespace bcos
{
namespace test
{
struct TableFixture
{
    TableFixture()
    {
        hashImpl = make_shared<Header256Hash>();
        memoryStorage = make_shared<StateStorage>(nullptr);
        tableFactory = make_shared<StateStorage>(memoryStorage);
    }

    ~TableFixture() {}
    std::shared_ptr<crypto::Hash> hashImpl = nullptr;
    std::shared_ptr<StorageInterface> memoryStorage = nullptr;
    protocol::BlockNumber m_blockNumber = 0;
    std::shared_ptr<StateStorage> tableFactory = nullptr;
};
BOOST_FIXTURE_TEST_SUITE(TableTest, TableFixture)

BOOST_AUTO_TEST_CASE(constructor)
{
    auto threadPool = ThreadPool("a", 1);
    auto table = std::make_shared<Table>(nullptr, nullptr);
    auto tableFactory = std::make_shared<StateStorage>(memoryStorage);
}

BOOST_AUTO_TEST_CASE(tableInfo)
{
    std::vector<std::string> fields = {"value9", "value8", "value7", "value6"};
    TableInfo tableInfo("test-table", fields);

    BOOST_CHECK_EQUAL_COLLECTIONS(
        fields.begin(), fields.end(), tableInfo.fields().begin(), tableInfo.fields().end());

    BOOST_CHECK_EQUAL(tableInfo.fieldIndex("value9"), 0);
    BOOST_CHECK_EQUAL(tableInfo.fieldIndex("value8"), 1);
    BOOST_CHECK_EQUAL(tableInfo.fieldIndex("value7"), 2);
    BOOST_CHECK_EQUAL(tableInfo.fieldIndex("value6"), 3);
}

BOOST_AUTO_TEST_CASE(dump_hash)
{
    std::string tableName("t_test");
    std::string keyField("key");
    std::string valueField("value");

    std::promise<std::optional<Table>> createPromise;
    tableFactory->asyncCreateTable(
        tableName, valueField, [&](auto&& error, std::optional<Table>&& table) {
            BOOST_CHECK(!error);
            createPromise.set_value(table);
        });

    BOOST_CHECK(createPromise.get_future().get());

    std::promise<std::optional<Table>> tablePromise;
    tableFactory->asyncOpenTable("t_test", [&](auto&& error, auto&& table) {
        BOOST_CHECK(!error);
        tablePromise.set_value(std::move(table));
    });
    auto table = tablePromise.get_future().get();
    BOOST_TEST(table);

    // BOOST_TEST(table->dirty() == false);
    auto entry = std::make_optional(table->newEntry());
    // entry->setField("key", "name");
    entry->setField(0, "Lili");
    table->setRow("name", *entry);
    auto tableinfo = table->tableInfo();
    BOOST_CHECK_EQUAL(tableinfo->name(), tableName);

    // BOOST_CHECK_EQUAL_COLLECTIONS(
    //     valueField.begin(), valueField.end(), tableinfo->fields.begin(),
    //     tableinfo->fields.end());

    // auto hash = tableFactory->hash(hashImpl);
    // BOOST_CHECK_EQUAL(hash.size, 32);

    // BOOST_CHECK_EQUAL(tableFactory.ex)

    // auto data = table->dump(m_blockNumber);
    // auto hash = table->hash();
    // BOOST_TEST(data->size() == 1);
    entry = table->newEntry();
    // entry->setField("key", "name2");
    entry->setField(0, "WW");
    BOOST_CHECK_NO_THROW(table->setRow("name2", *entry));

    // data = table->dump(m_blockNumber);
    // BOOST_TEST(data->size() == 2);
    // hash = table->hash();
    // BOOST_TEST(table->dirty() == true);
}

BOOST_AUTO_TEST_CASE(setRow)
{
    std::string tableName("t_test");
    std::string keyField("key");
    std::string valueField("value1,value2");

    std::promise<std::optional<Table>> createPromise;
    tableFactory->asyncCreateTable(
        tableName, valueField, [&](auto&& error, std::optional<Table>&& table) {
            BOOST_CHECK(!error);
            createPromise.set_value(std::move(table));
        });
    BOOST_CHECK(createPromise.get_future().get());

    std::promise<std::optional<Table>> tablePromise;
    tableFactory->asyncOpenTable("t_test", [&](auto&& error, auto&& table) {
        BOOST_CHECK(!error);
        tablePromise.set_value(std::move(table));
    });
    auto table = tablePromise.get_future().get();
    BOOST_TEST(table);

    // check fields order of t_test
    BOOST_TEST(table->tableInfo()->fields().size() == 2);
    BOOST_TEST(table->tableInfo()->fields()[0] == "value1");
    BOOST_TEST(table->tableInfo()->fields()[1] == "value2");
    // BOOST_TEST(table->tableInfo()->key == keyField);
    auto entry = std::make_optional(table->newEntry());
    // entry->setField("key", "name");
    // BOOST_CHECK_THROW(entry->setField(0, "Lili"), bcos::Error);
    BOOST_CHECK_NO_THROW(table->setRow("name", *entry));

    // check fields order of SYS_TABLE
    std::promise<std::optional<Table>> sysTablePromise;
    tableFactory->asyncOpenTable(StorageInterface::SYS_TABLES, [&](auto&& error, auto&& table) {
        BOOST_CHECK(!error);
        BOOST_TEST(table);
        sysTablePromise.set_value(std::move(table));
    });
    auto sysTable = sysTablePromise.get_future().get();
    BOOST_CHECK(sysTable);

    BOOST_TEST(sysTable->tableInfo()->fields().size() == 1);
    BOOST_TEST(sysTable->tableInfo()->fields()[0] == StateStorage::SYS_TABLE_VALUE_FIELDS);
    // BOOST_TEST(sysTable->tableInfo()->key == StateStorage::SYS_TABLE_KEY);
}

BOOST_AUTO_TEST_CASE(removeFromCache)
{
    std::string tableName("t_test");
    std::string keyField("key");
    std::string valueField("value1,value2");

    auto ret = tableFactory->createTable(tableName, valueField);
    BOOST_TEST(ret);
    auto table = tableFactory->openTable("t_test");
    BOOST_TEST(table);
    // check fields order of t_test
    BOOST_TEST(table->tableInfo()->fields().size() == 2);
    BOOST_TEST(table->tableInfo()->fields()[0] == "value1");
    BOOST_TEST(table->tableInfo()->fields()[1] == "value2");
    // BOOST_TEST(table->tableInfo()->key == keyField);
    auto entry = std::make_optional(table->newEntry());
    // entry->setField("key", "name");
    entry->setField(0, "hello world!");
    // BOOST_CHECK_THROW(entry->setField(0, "Lili"), bcos::Error);
    BOOST_CHECK_NO_THROW(table->setRow("name", *entry));

    auto deleteEntry = std::make_optional(table->newEntry());
    deleteEntry->setStatus(Entry::DELETED);
    BOOST_CHECK_NO_THROW(table->setRow("name", *deleteEntry));

    auto hashs = tableFactory->hash(hashImpl);

    auto tableFactory2 = std::make_shared<StateStorage>(nullptr);
    BOOST_CHECK(tableFactory2->createTable(tableName, valueField));
    auto table2 = tableFactory2->openTable(tableName);
    BOOST_TEST(table2);

    auto deleteEntry2 = std::make_optional(table2->newEntry());
    deleteEntry2->setStatus(Entry::DELETED);
    BOOST_CHECK_NO_THROW(table2->setRow("name", *deleteEntry2));
    auto hashs2 = tableFactory2->hash(hashImpl);

    BOOST_CHECK_EQUAL_COLLECTIONS(hashs.begin(), hashs.end(), hashs2.begin(), hashs2.end());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
