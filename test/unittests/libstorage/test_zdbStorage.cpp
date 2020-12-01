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
 * @brief
 *
 * @file test_zdbStorage.cpp
 * @author: darrenyin
 * @date 2019-05-15
 */

#include "boost/test/unit_test.hpp"
#include <libstorage/SQLBasicAccess.h>
#include <libstorage/StorageException.h>
#include <libstorage/Table.h>
#include <libstorage/ZdbStorage.h>
#include <libutilities/FixedBytes.h>
using namespace bcos;
using namespace bcos::storage;
using namespace boost;

namespace test_zdbStorage
{
class MockSQLBasicAccess : public bcos::storage::SQLBasicAccess
{
public:
    int Select(int64_t num, const std::string& table, const std::string& key,
        Condition::Ptr condition, std::vector<std::map<std::string, std::string>>& values) override
    {
        std::cout << ", num:" << num << ", key: " << key << std::endl;
        if (key == "_empty_key_" || !condition)
        {
            values.resize(0);
            return 0;
        }
        else
        {
            if (table == "e")
            {
                return -1;
            }
            else
            {
                std::map<std::string, std::string> value;
                value["id"] = "1000000";
                value["name"] = "darrenyin";
                value["_id_"] = "10";
                value["_num_"] = "100";
                value["_status_"] = "0";
                values.push_back(value);
            }
        }
        return 0;
    }
    int Commit(int64_t num, const std::vector<TableData::Ptr>& datas) override
    {
        std::cout << "num:" << num << std::endl;
        return datas.size();
    }
    void ExecuteSql(const std::string& _sql) override { printf("sql:%s\n", _sql.c_str()); }
};

struct zdbStorageFixture
{
    zdbStorageFixture()
    {
        zdbStorage = std::make_shared<bcos::storage::ZdbStorage>();
        std::shared_ptr<MockSQLBasicAccess> mockSqlBasicAccess =
            std::make_shared<MockSQLBasicAccess>();
        zdbStorage->SetSqlAccess(mockSqlBasicAccess);

        zdbStorage->initSysTables();
    }


    Entries::Ptr getEntries()
    {
        Entries::Ptr entries = std::make_shared<Entries>();
        Entry::Ptr entry = std::make_shared<Entry>();
        entry->setField("Name", "darrenyin");
        entry->setField("id", "1000000");
        entry->setField("_id_", "10");
        entry->setField("_num_", "100");
        entry->setField("_status_", "0");

        entries->addEntry(entry);
        return entries;
    }

    bcos::storage::ZdbStorage::Ptr zdbStorage;
};

BOOST_FIXTURE_TEST_SUITE(ZdbStorageTest, zdbStorageFixture)

BOOST_AUTO_TEST_CASE(onlyDirty)
{
    BOOST_CHECK_EQUAL(zdbStorage->onlyCommitDirty(), true);
}
BOOST_AUTO_TEST_CASE(empty_select)
{
    h256 h(0x01);
    int num = 1;
    std::string table("t_test");
    std::string key("_empty_key_");

    auto tableInfo = std::make_shared<TableInfo>();
    tableInfo->name = table;
    Entries::Ptr entries = zdbStorage->select(num, tableInfo, key, std::make_shared<Condition>());
    BOOST_CHECK_EQUAL(entries->size(), 0u);
}

BOOST_AUTO_TEST_CASE(select_condition)
{
    h256 h(0x01);
    int num = 1;
    std::string table("t_test");
    auto condition = std::make_shared<Condition>();
    condition->EQ("id", "2");

    auto tableInfo = std::make_shared<TableInfo>();
    tableInfo->name = table;
    condition = std::make_shared<Condition>();
    condition->EQ("id", "1000000");
    Entries::Ptr entries = zdbStorage->select(num, tableInfo, "darrenyin", condition);
    BOOST_CHECK_EQUAL(entries->size(), 1u);
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
    size_t c = zdbStorage->commit(num, datas);
    BOOST_CHECK_EQUAL(c, 1u);
}

BOOST_AUTO_TEST_CASE(exception) {}
BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_zdbStorage
