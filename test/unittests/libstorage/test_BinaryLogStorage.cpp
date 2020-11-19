/*
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
 */

#include "MemoryStorage2.h"
#include "libstorage/BinLogHandler.h"
#include "libstorage/BinaryLogStorage.h"
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::storage;

namespace test_BinaryLogStorage
{
struct StorageFixture
{
    StorageFixture()
    {
        binlogStorage = std::make_shared<BinaryLogStorage>();
        auto memStorage = std::make_shared<MemoryStorage2>();
        binlogStorage->setBackend(memStorage);
        binlogStorage->setBinaryLogger(nullptr);
    }
    Entries::Ptr getEntries()
    {
        Entries::Ptr entries = std::make_shared<Entries>();
        Entry::Ptr entry = std::make_shared<Entry>();
        entry->setField("Name", "LiSi");
        entry->setField("id", "2");
        entries->addEntry(entry);
        return entries;
    }
    std::shared_ptr<BinaryLogStorage> binlogStorage;
};

BOOST_FIXTURE_TEST_SUITE(TestBinaryLogStorage, StorageFixture)

BOOST_AUTO_TEST_CASE(empty_select)
{
    h256 h(0x01);
    int num = 1;
    std::string table("t_test");
    std::string key("id");

    auto tableInfo = std::make_shared<TableInfo>();
    tableInfo->name = table;
    Entries::Ptr entries =
        binlogStorage->select(num, tableInfo, key, std::make_shared<Condition>());
    BOOST_CHECK_EQUAL(entries->size(), 0u);
}

BOOST_AUTO_TEST_CASE(commit)
{
    h256 h(0x01);
    int num = 1;
    h256 blockHash(0x11231);
    std::vector<bcos::storage::TableData::Ptr> datas;
    bcos::storage::TableData::Ptr tableData = std::make_shared<bcos::storage::TableData>();
    tableData->info->name = "t_test";
    tableData->info->key = "Name";
    tableData->info->fields.push_back("id");
    Entries::Ptr entries = getEntries();
    tableData->newEntries = entries;
    datas.push_back(tableData);
    size_t c = binlogStorage->commit(num, datas);
    BOOST_CHECK_EQUAL(c, 1u);
    std::string table("t_test");
    std::string key("LiSi");

    auto tableInfo = std::make_shared<TableInfo>();
    tableInfo->name = table;
    entries = binlogStorage->select(num, tableInfo, key, std::make_shared<Condition>());
    BOOST_CHECK_EQUAL(entries->size(), 1u);
    binlogStorage->stop();
}


BOOST_AUTO_TEST_CASE(exception)
{
    binlogStorage->setBackend(nullptr);
    binlogStorage->setBinaryLogger(nullptr);

    int num = 1;
    std::vector<bcos::storage::TableData::Ptr> datas;
    bcos::storage::TableData::Ptr tableData = std::make_shared<bcos::storage::TableData>();
    tableData->info->name = "e";
    tableData->info->key = "Name";
    tableData->info->fields.push_back("id");
    Entries::Ptr entries = getEntries();
    entries->get(0)->setField("Name", "Exception");
    tableData->newEntries = entries;
    datas.push_back(tableData);
    BOOST_CHECK_THROW(binlogStorage->commit(num, datas), boost::exception);
    std::string table("e");
    std::string key("Exception");

    auto tableInfo = std::make_shared<TableInfo>();
    tableInfo->name = table;
    BOOST_CHECK_THROW(binlogStorage->select(num, tableInfo, key, std::make_shared<Condition>()),
        boost::exception);
}

BOOST_AUTO_TEST_SUITE_END()


}  // namespace test_BinaryLogStorage
