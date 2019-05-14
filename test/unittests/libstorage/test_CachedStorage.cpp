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
 * @file test_CachedStorage.cpp
 * @author: monan
 * @date 2019-04-13
 */

#include <libdevcore/FixedHash.h>
#include <libstorage/CachedStorage.h>
#include <libstorage/StorageException.h>
#include <libstorage/Table.h>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <thread>

using namespace dev;
using namespace dev::storage;

namespace test_CachedStorage
{
class MockStorage : public Storage
{
public:
    Entries::Ptr select(h256 hash, int num, TableInfo::Ptr tableInfo, const std::string& key,
        Condition::Ptr condition) override
    {
        (void)hash;
        (void)num;

        if (commited)
        {
            BOOST_TEST(false);
            return nullptr;
        }

        auto entries = std::make_shared<Entries>();

        BOOST_TEST(condition != nullptr);

        if (tableInfo->name == SYS_CURRENT_STATE && key == SYS_KEY_CURRENT_ID)
        {
            Entry::Ptr entry = std::make_shared<Entry>();
            entry->setID(1);
            entry->setNum(100);
            entry->setField(SYS_KEY, SYS_KEY_CURRENT_ID);
            entry->setField(SYS_VALUE, "100");
            entry->setStatus(0);

            entries->addEntry(entry);
            return entries;
        }

        if (tableInfo->name == "t_test")
        {
            if (key == "LiSi")
            {
                Entry::Ptr entry = std::make_shared<Entry>();
                entry->setID(1);
                entry->setNum(100);
                entry->setField("Name", "LiSi");
                entry->setField("id", "1");
                entry->setStatus(0);

                entries->addEntry(entry);
            }
        }

        return entries;
    }

    size_t commit(h256 hash, int64_t num, const std::vector<TableData::Ptr>& datas) override
    {
        (void)datas;

        BOOST_CHECK(hash == h256());
        BOOST_CHECK(num == commitNum);

        BOOST_CHECK(datas.size() == 2);
        for (auto it : datas)
        {
            if (it->info->name == "t_test")
            {
                BOOST_CHECK(it->newEntries->size() == 1);
            }
            else if (it->info->name == SYS_CURRENT_STATE)
            {
                BOOST_CHECK(it->dirtyEntries->size() == 1);
            }
            else
            {
                BOOST_CHECK(false);
            }
        }

        commited = true;
        return 0;
    }

    bool onlyDirty() override { return true; }

    bool commited = false;
    int64_t commitNum = 50;
};

class MockStorageParallel : public Storage
{
public:
    MockStorageParallel()
    {
        for (size_t i = 100; i < 200; ++i)
        {
            auto tableName = "t_test" + boost::lexical_cast<std::string>(i);

            Entry::Ptr entry = std::make_shared<Entry>();
            entry->setField("Name", "LiSi1");
            entry->setField("id", boost::lexical_cast<std::string>(i));
            entry->setID(1000 + i);
            tableKey2Entry.insert(std::make_pair(tableName + entry->getField("Name"), entry));

            entry = std::make_shared<Entry>();
            entry->setField("Name", "LiSi2");
            entry->setField("id", boost::lexical_cast<std::string>(i + 1));
            entry->setID(1001 + i);
            tableKey2Entry.insert(std::make_pair(tableName + entry->getField("Name"), entry));

            entry = std::make_shared<Entry>();
            entry->setField("Name", "LiSi3");
            entry->setField("id", boost::lexical_cast<std::string>(i + 2));
            entry->setID(1002 + i);
            tableKey2Entry.insert(std::make_pair(tableName + entry->getField("Name"), entry));
        }
    }

    Entries::Ptr select(h256 hash, int num, TableInfo::Ptr tableInfo, const std::string& key,
        Condition::Ptr condition) override
    {
        (void)hash;
        (void)num;
        (void)condition;

        auto tableKey = tableInfo->name + key;
        auto it = tableKey2Entry.find(tableKey);

        auto entries = std::make_shared<Entries>();
        entries->addEntry(it->second);
        return entries;
    }

    size_t commit(h256 hash, int64_t num, const std::vector<TableData::Ptr>& datas) override
    {
        BOOST_CHECK(hash == h256());
        BOOST_CHECK(num == commitNum);

        BOOST_CHECK(datas.size() == 101);
        for (size_t i = 0; i < 100; ++i)
        {
            auto tableData = datas[i];
            BOOST_TEST(
                tableData->info->name == "t_test" + boost::lexical_cast<std::string>(100 + i));
            BOOST_TEST(tableData->dirtyEntries->size() == 3);
            BOOST_TEST(tableData->newEntries->size() == 3);

            for (size_t j = 0; j < 3; ++j)
            {
                auto entry = tableData->dirtyEntries->get(j);
                BOOST_TEST(entry->getID() == 1000 + 100 + i + j);
                BOOST_TEST(entry->getField("id") == boost::lexical_cast<std::string>(i + 100 + j));
                BOOST_TEST(
                    entry->getField("Name") == "LiSi" + boost::lexical_cast<std::string>(j + 1));

                auto tableKey = tableData->info->name + entry->getField(tableData->info->key);
                auto it = tableKey2Entry.find(tableKey);
                BOOST_TEST(entry->getField("Name") == it->second->getField("Name"));
                BOOST_TEST(entry->getField("id") == it->second->getField("id"));
                BOOST_TEST(entry->getID() == it->second->getID());

                entry = tableData->newEntries->get(j);
                BOOST_TEST(
                    entry->getField("id") == boost::lexical_cast<std::string>(i + 100 + 100 + j));
                BOOST_TEST(entry->getID() == i * 3 + j + 2);
                BOOST_TEST(entry->getField("Name") == "ZhangSan");
            }
        }

        commited = true;
        return 0;
    }

    bool onlyDirty() override { return true; }

    bool commited = false;
    int64_t commitNum = 50;

    tbb::concurrent_unordered_map<std::string, Entry::Ptr> tableKey2Entry;
};

struct CachedStorageFixture
{
    CachedStorageFixture()
    {
        cachedStorage = std::make_shared<CachedStorage>();
        mockStorage = std::make_shared<MockStorage>();

        cachedStorage->setBackend(mockStorage);
        cachedStorage->setMaxForwardBlock(51);
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

    dev::storage::CachedStorage::Ptr cachedStorage;
    std::shared_ptr<MockStorage> mockStorage;
};

BOOST_FIXTURE_TEST_SUITE(CachedStorageTest, CachedStorageFixture)


BOOST_AUTO_TEST_CASE(onlyDirty)
{
    BOOST_CHECK_EQUAL(cachedStorage->onlyDirty(), true);
}

BOOST_AUTO_TEST_CASE(setBackend)
{
    auto backend = Storage::Ptr();
    cachedStorage->setBackend(backend);
}

BOOST_AUTO_TEST_CASE(init)
{
    cachedStorage->init();
    BOOST_TEST(cachedStorage->ID() == 100);
}

BOOST_AUTO_TEST_CASE(empty_select)
{
    cachedStorage->init();
    h256 h(0x01);
    int num = 1;
    std::string table("t_test");
    std::string key("id");

    auto tableInfo = std::make_shared<TableInfo>();
    tableInfo->name = table;
    Entries::Ptr entries =
        cachedStorage->select(h, num, tableInfo, key, std::make_shared<Condition>());
    BOOST_CHECK_EQUAL(entries->size(), 0u);
}

BOOST_AUTO_TEST_CASE(select_condition)
{
    // query from backend
    h256 h(0x01);
    int num = 1;
    std::string table("t_test");
    auto condition = std::make_shared<Condition>();
    condition->EQ("id", "2");

    auto tableInfo = std::make_shared<TableInfo>();
    tableInfo->name = table;
    Entries::Ptr entries = cachedStorage->select(h, num, tableInfo, "LiSi", condition);
    BOOST_CHECK_EQUAL(entries->size(), 0u);

    // query from cache
    mockStorage->commited = true;
    condition = std::make_shared<Condition>();
    condition->EQ("id", "1");

    tableInfo = std::make_shared<TableInfo>();
    tableInfo->name = table;
    entries = cachedStorage->select(h, num, tableInfo, "LiSi", condition);
    BOOST_CHECK_EQUAL(entries->size(), 1u);
}

BOOST_AUTO_TEST_CASE(commit_single_data)
{
    h256 h;
    int64_t num = 50;
    cachedStorage->setMaxForwardBlock(100);
    std::vector<dev::storage::TableData::Ptr> datas;
    dev::storage::TableData::Ptr tableData = std::make_shared<dev::storage::TableData>();
    tableData->info->name = "t_test";
    tableData->info->key = "Name";
    tableData->info->fields.push_back("id");
    Entries::Ptr entries = getEntries();
    tableData->newEntries = entries;
    datas.push_back(tableData);

    BOOST_TEST(cachedStorage->syncNum() == 0);
    mockStorage->commitNum = 50;
    size_t c = cachedStorage->commit(h, num, datas);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    BOOST_TEST(cachedStorage->syncNum() == 50);

    BOOST_CHECK_EQUAL(c, 1u);
    std::string table("t_test");
    std::string key("LiSi");

    auto tableInfo = std::make_shared<TableInfo>();
    tableInfo->name = table;
    tableInfo->key = "Name";
    entries = cachedStorage->select(h, num, tableInfo, key, std::make_shared<Condition>());
    BOOST_CHECK_EQUAL(entries->size(), 2u);

    for (size_t i = 0; i < entries->size(); ++i)
    {
        auto entry = entries->get(i);
        if (entry->getField("id") == "1")
        {
            BOOST_TEST(entry->getID() == 1);
        }
        else if (entry->getField("id") == "2")
        {
            BOOST_TEST(entry->getID() == 2);
        }
        else
        {
            BOOST_TEST(false);
        }
    }
}

BOOST_AUTO_TEST_CASE(commit_multi_data)
{
    h256 h;
    int64_t num = 50;
    cachedStorage->setMaxForwardBlock(100);
    std::vector<dev::storage::TableData::Ptr> datas;
    dev::storage::TableData::Ptr tableData = std::make_shared<dev::storage::TableData>();
    tableData->info->name = "t_test";
    tableData->info->key = "Name";
    tableData->info->fields.push_back("id");
    Entries::Ptr entries = getEntries();
    tableData->newEntries = entries;
    datas.push_back(tableData);

    BOOST_TEST(cachedStorage->syncNum() == 0);
    mockStorage->commitNum = 50;
    size_t c = cachedStorage->commit(h, num, datas);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    BOOST_TEST(cachedStorage->syncNum() == 50);

    BOOST_CHECK_EQUAL(c, 1u);
    std::string table("t_test");
    std::string key("LiSi");

    auto tableInfo = std::make_shared<TableInfo>();
    tableInfo->name = table;
    tableInfo->key = "Name";
    entries = cachedStorage->select(h, num, tableInfo, key, std::make_shared<Condition>());
    BOOST_CHECK_EQUAL(entries->size(), 2u);

    for (size_t i = 0; i < entries->size(); ++i)
    {
        auto entry = entries->get(i);
        if (entry->getField("id") == "1")
        {
            BOOST_TEST(entry->getID() == 1);
        }
        else if (entry->getField("id") == "2")
        {
            BOOST_TEST(entry->getID() == 2);
        }
        else
        {
            BOOST_TEST(false);
        }
    }
}

BOOST_AUTO_TEST_CASE(parllel_commit)
{
    h256 h;
    int64_t num = 50;
    std::vector<dev::storage::TableData::Ptr> datas;
    cachedStorage->setMaxForwardBlock(100);

    for (size_t i = 100; i < 200; ++i)
    {
        dev::storage::TableData::Ptr tableData = std::make_shared<dev::storage::TableData>();
        tableData->info->name = "t_test" + boost::lexical_cast<std::string>(i);
        tableData->info->key = "Name";
        tableData->info->fields.push_back("id");

        Entries::Ptr entries = std::make_shared<Entries>();

        Entry::Ptr entry = std::make_shared<Entry>();
        entry->setField("Name", "LiSi1");
        entry->setField("id", boost::lexical_cast<std::string>(i));
        entry->setID(1000 + i);
        entries->addEntry(entry);

        entry = std::make_shared<Entry>();
        entry->setField("Name", "LiSi2");
        entry->setField("id", boost::lexical_cast<std::string>(i + 1));
        entry->setID(1001 + i);
        entries->addEntry(entry);

        entry = std::make_shared<Entry>();
        entry->setField("Name", "LiSi3");
        entry->setField("id", boost::lexical_cast<std::string>(i + 2));
        entry->setID(1002 + i);
        entries->addEntry(entry);
        tableData->dirtyEntries = entries;

        entries = std::make_shared<Entries>();
        entry = std::make_shared<Entry>();
        entry->setField("Name", "ZhangSan");
        entry->setField("id", boost::lexical_cast<std::string>(i + 100));
        entry->setForce(true);
        entries->addEntry(entry);

        entry = std::make_shared<Entry>();
        entry->setField("Name", "ZhangSan");
        entry->setField("id", boost::lexical_cast<std::string>(i + 101));
        entry->setForce(true);
        entries->addEntry(entry);

        entry = std::make_shared<Entry>();
        entry->setField("Name", "ZhangSan");
        entry->setField("id", boost::lexical_cast<std::string>(i + 102));
        entry->setForce(true);
        entries->addEntry(entry);

        tableData->newEntries = entries;
        datas.push_back(tableData);
    }

    cachedStorage->setBackend(std::make_shared<MockStorageParallel>());

    BOOST_TEST(cachedStorage->syncNum() == 0);
    mockStorage->commitNum = 50;
    size_t c = cachedStorage->commit(h, num, datas);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    BOOST_TEST(cachedStorage->syncNum() == 50);

    BOOST_CHECK_EQUAL(c, 600u);


#if 0
	std::string table("t_test");
	std::string key("LiSi");
	auto tableInfo = std::make_shared<TableInfo>();
	tableInfo->name = table;
	entries = cachedStorage->select(h, num, tableInfo, key, std::make_shared<Condition>());
	BOOST_CHECK_EQUAL(entries->size(), 2u);

	for (size_t i = 0; i < entries->size(); ++i)
	{
		auto entry = entries->get(i);
		if (entry->getField("id") == "1")
		{
			BOOST_TEST(entry->getID() == 1);
		}
		else if (entry->getField("id") == "2")
		{
			BOOST_TEST(entry->getID() == 2);
		}
		else
		{
			BOOST_TEST(false);
		}
	}
#endif
}

BOOST_AUTO_TEST_CASE(parallel_samekey_commit)
{
#if 0
	cachedStorage->init();

	auto tableInfo = std::make_shared<TableInfo>();
	tableInfo->name = "t_test";
	tableInfo->key = "key";
	tableInfo->fields.push_back("value");

	auto entry = std::make_shared<Entry>();
	entry->setField("key", "1");
	entry->setField("value", "2");

	auto data = std::make_shared<dev::storage::TableData>();
	data->newEntries->addEntry(entry);
	data->info = tableInfo;

	std::vector<dev::storage::TableData::Ptr> datas;
	datas.push_back(data);
	cachedStorage->commit(dev::h256(0), 99, datas);

	for(size_t i=0; i < 100; ++i) {
		Caches::Ptr caches = cachedStorage->selectNoCondition(dev::h256(0), 0, tableInfo, "1", dev::storage::Condition::Ptr());
		BOOST_TEST(caches->key() == "key");
		BOOST_TEST(caches->num() == 99);

		auto entries = caches->entries();
		BOOST_TEST(entries->size() == 1);

		auto cacheEntry = entries->get(0);
		BOOST_TEST(cacheEntry != entry);

		BOOST_TEST(cacheEntry->getField("key") == entry->getField("key"));
		BOOST_TEST(cacheEntry->getField("value") == entry->getField("value"));

		cacheEntry->setID(0);
		cacheEntry->setField("value", boost::lexical_cast<std::string>(i));

		auto newData = std::make_shared<dev::storage::TableData>();
		newData->newEntries->addEntry(cacheEntry);
		newData->info = tableInfo;

		std::vector<dev::storage::TableData::Ptr> newDatas;
		newDatas.push_back(data);

		cachedStorage->commit(dev::h256(0), 100 + i, newDatas);
	}

	Caches::Ptr result = cachedStorage->selectNoCondition(dev::h256(0), 0, tableInfo, "1", dev::storage::Condition::Ptr());
	BOOST_TEST(result->num() == 199);
	auto resultEntries = result->entries();
	BOOST_TEST(resultEntries->size() == 100);
#endif
}

BOOST_AUTO_TEST_CASE(checkAndClear) {}

BOOST_AUTO_TEST_CASE(exception)
{
#if 0
    h256 h(0x01);
    int num = 1;
    h256 blockHash(0x11231);
    std::vector<dev::storage::TableData::Ptr> datas;
    dev::storage::TableData::Ptr tableData = std::make_shared<dev::storage::TableData>();
    tableData->info->name = "e";
    tableData->info->key = "Name";
    tableData->info->fields.push_back("id");
    Entries::Ptr entries = getEntries();
    entries->get(0)->setField("Name", "Exception");
    tableData->entries = entries;
    datas.push_back(tableData);
    BOOST_CHECK_THROW(AMOP->commit(h, num, datas, blockHash), boost::exception);
    std::string table("e");
    std::string key("Exception");

    BOOST_CHECK_THROW(AMOP->select(h, num, table, key, std::make_shared<Condition>()), boost::exception);
#endif
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_CachedStorage
