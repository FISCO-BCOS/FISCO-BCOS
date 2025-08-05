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
 * @brief Unit tests for the Entry
 * @file Entry.cpp
 */

#include "bcos-framework/protocol/Protocol.h"
#include "bcos-framework/storage/Table.h"
#include "bcos-table/src/StateStorage.h"
#include <bcos-crypto/hash/SM3.h>
#include <bcos-utilities/Error.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/test/unit_test.hpp>
#include <iostream>
#include <string>

using namespace std;
using namespace bcos;
using namespace bcos::storage;

namespace bcos
{
namespace test
{
struct EntryFixture
{
    EntryFixture()
    {
        tableInfo = std::make_shared<TableInfo>("testTable", std::vector<std::string>{"key2"});
    }

    ~EntryFixture() {}

    std::shared_ptr<TableInfo> tableInfo;
};
BOOST_FIXTURE_TEST_SUITE(EntryTest, EntryFixture)

BOOST_AUTO_TEST_CASE(viewEqual)
{
    std::string a = "value";

    BOOST_CHECK_EQUAL(a, "value");
    BOOST_CHECK_EQUAL(std::string_view(a), "value");
}

BOOST_AUTO_TEST_CASE(copyFrom)
{
    auto entry1 = std::make_shared<Entry>();
    auto entry2 = std::make_shared<Entry>();
    BOOST_CHECK_EQUAL(entry1->dirty(), false);
    entry1->setField(0, "value");
    BOOST_TEST(entry1->dirty() == true);
    BOOST_TEST(entry1->size() == 5);

    *entry2 = *entry1;

    {
        auto entry3 = Entry(*entry1);

        entry3.setField(0, "i am key2");

        auto entry4(std::move(entry3));

        auto entry5(*entry2);

        auto entry6(std::move(entry5));
    }

    BOOST_CHECK_EQUAL(entry2->getField(0), "value"sv);

    entry2->setField(0, "value2");

    BOOST_CHECK_EQUAL(entry2->getField(0), "value2");
    BOOST_CHECK_EQUAL(entry1->getField(0), "value");

    entry2->setField(0, "value3");
    BOOST_TEST(entry2->size() == 6);
    BOOST_TEST(entry2->getField(0) == "value3");
    *entry2 = *entry2;
    BOOST_TEST(entry2->dirty() == true);
    // entry2->setDirty(false);
    entry2->setStatus(Entry::Status::NORMAL);
    BOOST_TEST(entry2->dirty() == false);
    // test setField lValue and rValue
    entry2->setField(0, string("value2"));
    BOOST_TEST(entry2->dirty() == true);
    BOOST_TEST(entry2->size() == 6);
    auto value2 = "value2";
    entry2->setField(0, value2);
}

BOOST_AUTO_TEST_CASE(functions)
{
    auto entry = std::make_shared<Entry>();
    BOOST_TEST(entry->dirty() == false);
    BOOST_TEST(entry->status() == Entry::Status::EMPTY);
    entry->setStatus(Entry::Status::DELETED);
    BOOST_TEST(entry->status() == Entry::Status::DELETED);
    BOOST_TEST(entry->dirty() == true);
}

BOOST_AUTO_TEST_CASE(BytesField)
{
    Entry entry;

    std::string value = "abcdefghijklmn";
    std::vector<char> data;
    data.assign(value.begin(), value.end());

    entry.importFields({std::string(value)});

    BOOST_CHECK_EQUAL(entry.getField(0), value);

    Entry entry2;
    entry2.importFields({data});

    BOOST_CHECK_EQUAL(entry2.getField(0), value);
}

BOOST_AUTO_TEST_CASE(capacity)
{
    Entry entry;

    entry.importFields({std::string("abc")});

    entry.setField(
        0, std::string("abdflsakdjflkasjdfoiqwueroi!!!!sdlkfjsldfbclsadflaksjdfpqweioruaaa"));

    BOOST_CHECK_LT(entry.size(), 100);
    BOOST_CHECK_GT(entry.size(), 0);
}

BOOST_AUTO_TEST_CASE(object)
{
    std::tuple<int, std::string, std::string> value = std::make_tuple(100, "hello", "world");

    Entry entry;
    entry.setObject(value);

    auto out = entry.getObject<std::tuple<int, std::string, std::string>>();

    BOOST_TEST(out == value);
}

BOOST_AUTO_TEST_CASE(largeObject)
{
    Entry entry;
    entry.setField(0, std::string(1024, 'a'));

    BOOST_CHECK_EQUAL(entry.getField(0), std::string(1024, 'a'));
}

BOOST_AUTO_TEST_CASE(stringView)
{
    Entry entry;
    std::string_view a(
        "Hello world! fisco bcos! fisco bcos! fisco bcos! fisco bcos! larger than 32");
    entry.set(a);

    Entry entry2 = entry;
    BOOST_CHECK_EQUAL(entry2.get(), a);
}

BOOST_AUTO_TEST_CASE(entryHash)
{
    auto data = "Hello world!"s;
    auto table = "table!"s;
    auto key = "key!"s;

    Entry entry;
    entry.setStatus(Entry::MODIFIED);
    entry.setField(0, data);

    auto sm3 = std::make_shared<bcos::crypto::SM3>();
    auto oldHash = entry.hash(table, key, *sm3, 0);
    auto oldExpect = sm3->hash(bytesConstRef((bcos::byte*)data.data(), data.size()));
    BOOST_CHECK_EQUAL(oldHash, oldExpect);

    entry.setStatus(Entry::DELETED);
    auto deletedHash =
        entry.hash(table, key, *sm3, (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION);

    auto hasher = sm3->hasher();
    hasher.update(table);
    hasher.update(key);

    bcos::crypto::HashType deletedExpect;
    hasher.final(deletedExpect);
    BOOST_CHECK_EQUAL(deletedHash, deletedExpect);

    entry.setStatus(Entry::MODIFIED);
    entry.setField(0, data);
    auto modifyHash =
        entry.hash(table, key, *sm3, (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION);
    hasher = sm3->hasher();
    hasher.update(table);
    hasher.update(key);
    hasher.update(data);

    bcos::crypto::HashType modifyExpect;
    hasher.final(modifyExpect);
    BOOST_CHECK_EQUAL(modifyHash, modifyExpect);

    entry.setStatus(Entry::NORMAL);
    auto normalHash =
        entry.hash(table, key, *sm3, (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION);
    BOOST_CHECK_EQUAL(normalHash, bcos::crypto::HashType{});
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
