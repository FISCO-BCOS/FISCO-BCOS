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

#include "MemoryStorage2.h"
#include <libdevcore/FixedHash.h>
#include <libstorage/CachedStorage.h>
#include <libstorage/StorageException.h>
#include <libstorage/Table.h>
#include <tbb/parallel_for.h>
#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_int.hpp>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <mutex>
#include <thread>

using namespace dev;
using namespace dev::storage;

namespace test_CachedStorage
{
class MockStorage : public Storage
{
public:
    Entries::Ptr select(int64_t, TableInfo::Ptr tableInfo, const std::string& key,
        Condition::Ptr condition) override
    {
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

    size_t commit(int64_t num, const std::vector<TableData::Ptr>& datas) override
    {
        (void)datas;

        BOOST_CHECK(num == commitNum);

        BOOST_CHECK(datas.size() == 1);
        for (auto it : datas)
        {
            if (it->info->name == "t_test")
            {
                BOOST_CHECK(it->newEntries->size() == 1);
            }
            else if (it->info->name == SYS_CURRENT_STATE)
            {
                // BOOST_CHECK(it->dirtyEntries->size() == 1);
            }
            else
            {
                BOOST_CHECK(false);
            }
        }

        commited = true;
        return 0;
    }

    bool onlyCommitDirty() override { return true; }

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

    Entries::Ptr select(int64_t num, TableInfo::Ptr tableInfo, const std::string& key,
        Condition::Ptr condition) override
    {
        (void)num;
        (void)condition;

        auto tableKey = tableInfo->name + key;
        auto it = tableKey2Entry.find(tableKey);

        auto entries = std::make_shared<Entries>();
        entries->addEntry(it->second);
        return entries;
    }

    size_t commit(int64_t num, const std::vector<TableData::Ptr>& datas) override
    {
        BOOST_CHECK(num == commitNum);

        BOOST_CHECK(datas.size() == 100);
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
                // BOOST_TEST(entry->getID() == i * 3 + j + 2);
                BOOST_TEST(entry->getField("Name") == "ZhangSan");
            }
        }

        commited = true;
        return 0;
    }

    bool onlyCommitDirty() override { return true; }

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

    ~CachedStorageFixture() { cachedStorage->stop(); }

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
    BOOST_CHECK_EQUAL(cachedStorage->onlyCommitDirty(), true);
}

BOOST_AUTO_TEST_CASE(setBackend)
{
    auto backend = Storage::Ptr();
    cachedStorage->setBackend(backend);
}

BOOST_AUTO_TEST_CASE(init)
{
    cachedStorage->init();
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
        cachedStorage->select(num, tableInfo, key, std::make_shared<Condition>());
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
    Entries::Ptr entries = cachedStorage->select(num, tableInfo, "LiSi", condition);
    BOOST_CHECK_EQUAL(entries->size(), 0u);

    // query from cache
    mockStorage->commited = true;
    condition = std::make_shared<Condition>();
    condition->EQ("id", "1");

    tableInfo = std::make_shared<TableInfo>();
    tableInfo->name = table;
    entries = cachedStorage->select(num, tableInfo, "LiSi", condition);
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
    size_t c = cachedStorage->commit(num, datas);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    BOOST_TEST(cachedStorage->syncNum() == 50);

    BOOST_CHECK_EQUAL(c, 1u);
    std::string table("t_test");
    std::string key("LiSi");

    auto tableInfo = std::make_shared<TableInfo>();
    tableInfo->name = table;
    tableInfo->key = "Name";
    entries = cachedStorage->select(num, tableInfo, key, std::make_shared<Condition>());
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
            // BOOST_TEST(entry->getID() == 2);
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
    size_t c = cachedStorage->commit(num, datas);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    BOOST_TEST(cachedStorage->syncNum() == 50);

    BOOST_CHECK_EQUAL(c, 1u);
    std::string table("t_test");
    std::string key("LiSi");

    auto tableInfo = std::make_shared<TableInfo>();
    tableInfo->name = table;
    tableInfo->key = "Name";
    entries = cachedStorage->select(num, tableInfo, key, std::make_shared<Condition>());
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
            // BOOST_TEST(entry->getID() == 2);
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
    size_t c = cachedStorage->commit(num, datas);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    BOOST_TEST(cachedStorage->syncNum() == 50);

    BOOST_CHECK_EQUAL(c, 600u);


#if 0
	std::string table("t_test");
	std::string key("LiSi");
	auto tableInfo = std::make_shared<TableInfo>();
	tableInfo->name = table;
	entries = cachedStorage->select(num, tableInfo, key, std::make_shared<Condition>());
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

BOOST_AUTO_TEST_CASE(ordered_commit)
{
    cachedStorage->init();
    cachedStorage->setBackend(dev::storage::Storage::Ptr());

    dev::storage::TableData::Ptr tableData = std::make_shared<dev::storage::TableData>();
    tableData->info->name = "t_test";
    tableData->info->key = "Name";
    tableData->info->fields.push_back("id");

    Entries::Ptr entries = std::make_shared<Entries>();

    for (size_t i = 0; i < 50; ++i)
    {
        boost::random::mt19937 rng;
        boost::random::uniform_int_distribution<> rand(0, 1000);

        int num = rand(rng);
        Entry::Ptr entry = std::make_shared<Entry>();
        entry->setField("Name", "node");
        entry->setField("id", boost::lexical_cast<std::string>(num));
        entries->addEntry(entry);
    }

    tableData->newEntries = entries;

    std::vector<dev::storage::TableData::Ptr> datas;
    datas.push_back(tableData);

    cachedStorage->commit(0, datas);

    auto result =
        cachedStorage->selectNoCondition(0, tableData->info, "node", std::make_shared<Condition>());
    auto cache = std::get<1>(result);

    ssize_t currentID = -1;
    for (auto it : *(cache->entries()))
    {
        if (currentID == -1)
        {
            currentID = it->getID();
        }
        else
        {
            BOOST_TEST(currentID <= it->getID());
            currentID = it->getID();
        }
        LOG(TRACE) << "CurrentID: " << it->getID();
    }
}

BOOST_AUTO_TEST_CASE(parallel_samekey_commit)
{
    cachedStorage->init();
    cachedStorage->setBackend(Storage::Ptr());

    auto tableInfo = std::make_shared<TableInfo>();
    tableInfo->name = "t_test";
    tableInfo->key = "key";
    tableInfo->fields.push_back("value");

    auto entry = std::make_shared<Entry>();
    entry->setField("key", "1");
    entry->setField("value", "200");

    auto data = std::make_shared<dev::storage::TableData>();
    data->newEntries->addEntry(entry);
    data->info = tableInfo;

    std::vector<dev::storage::TableData::Ptr> datas;
    datas.push_back(data);
    cachedStorage->commit(99, datas);

    for (size_t i = 0; i < 100; ++i)
    {
        auto result =
            cachedStorage->selectNoCondition(0, tableInfo, "1", dev::storage::Condition::Ptr());
        Cache::Ptr caches = std::get<1>(result);
        BOOST_TEST(caches->key() == "1");
        BOOST_TEST(caches->num() == 99);
    }
}

BOOST_AUTO_TEST_CASE(checkAndClear)
{
    cachedStorage->setMaxCapacity(0);
    cachedStorage->setMaxForwardBlock(0);

    auto backend = std::make_shared<CachedStorage>();
    cachedStorage->setBackend(backend);

    ordered_commit_invoker();
    parllel_commit_invoker();
    select_condition_invoker();
}

BOOST_AUTO_TEST_CASE(dirtyAndNew)
{
#if 0
    cachedStorage->setMaxCapacity(2000 * 1024 * 1024);
    cachedStorage->setMaxForwardBlock(10000);

    auto userTable = std::make_shared<TableInfo>();
    userTable->key = "key";
    userTable->fields.push_back("value");
    userTable->name = "_dag_transfer_";

    auto txTable = std::make_shared<TableInfo>();
    txTable->key = "txhash";
    txTable->fields.push_back("number");
    txTable->name = "_sys_txhash_2_block_";

    TableData::Ptr newUserData = std::make_shared<TableData>();
    newUserData->info = userTable;
    TableData::Ptr newTXData = std::make_shared<TableData>();
    newTXData->info = txTable;
    Entries::Ptr newUser = std::make_shared<Entries>();
    Entries::Ptr newTX = std::make_shared<Entries>();
    for (auto i = 0; i < 10000; ++i)
    {
        Entry::Ptr entry = std::make_shared<Entry>();
        entry->setField("key", boost::lexical_cast<std::string>(i));
        entry->setField("value", "0");
        entry->setForce(true);
        newUser->addEntry(entry);

        Entry::Ptr entry2 = std::make_shared<Entry>();
        entry2->setField("txhash", boost::lexical_cast<std::string>(i));
        entry2->setField("number", "0");
        entry2->setForce(true);
        newTX->addEntry(entry2);
    }

    newUserData->newEntries = newUser;
    newTXData->newEntries = newTX;

    std::vector<TableData::Ptr> datas = {newUserData, newTXData};

    auto c = cachedStorage->commit(1, datas);

    BOOST_TEST(c == 20000);

    std::map<int, int> totalCount;
    for (auto i = 0; i < 100; ++i)
    {
        TableData::Ptr updateUserData = std::make_shared<TableData>();
        updateUserData->info = userTable;

        TableData::Ptr newTxData = std::make_shared<TableData>();
        newTxData->info = txTable;

        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, 10000), [&](const tbb::blocked_range<size_t>& range) {
                for (size_t j = range.begin(); j < range.end(); ++j)
                {
                    auto data = cachedStorage->select(i + 2, userTable,
                        boost::lexical_cast<std::string>(j), std::make_shared<Condition>());
                    BOOST_TEST(data->size() == 1);

                    auto entry = data->get(0);
                    auto value = boost::lexical_cast<int>(entry->getField("value"));
                    entry->setField("value", boost::lexical_cast<std::string>(value + 1));

                    updateUserData->dirtyEntries->addEntry(entry);

                    auto txEntry = std::make_shared<Entry>();
                    txEntry->setField("txhash", boost::lexical_cast<std::string>(100000 + i));
                    txEntry->setField("number", "0");
                    txEntry->setForce(true);

                    newTxData->newEntries->addEntry(txEntry);
                }
            });

        auto blockDatas = std::vector<TableData::Ptr>();
        blockDatas.push_back(updateUserData);
        blockDatas.push_back(newTxData);

        cachedStorage->commit(i + 2, blockDatas);
    }

    tbb::parallel_for(
		tbb::blocked_range<size_t>(0, 10000), [&](const tbb::blocked_range<size_t>& range) {
			for (size_t i = range.begin(); i < range.end(); ++i)
			{
				auto data = cachedStorage->select(1002, userTable,
					boost::lexical_cast<std::string>(i), std::make_shared<Condition>());
				BOOST_TEST(data->size() == 1);

				auto entry = data->get(0);
				auto value = boost::lexical_cast<int>(entry->getField("value"));

				BOOST_TEST(entry->getField("key") == "key");
				BOOST_TEST(value == 1000);
			}
		});
#endif
}

class CommitCheckMock : public Storage
{
public:
    Entries::Ptr select(int64_t, TableInfo::Ptr tableInfo, const std::string& key,
        Condition::Ptr condition) override
    {
        (void)tableInfo;
        (void)key;
        (void)condition;
        return Entries::Ptr();
    }

    size_t commit(int64_t num, const std::vector<TableData::Ptr>& datas) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_num != 0)
        {
            for (size_t i = 0; i < m_datas.size(); ++i)
            {
                auto m_tableData = m_datas[i];
                auto tableData = datas[i];
                BOOST_TEST(m_tableData->info->name == tableData->info->name);
                BOOST_TEST(m_tableData->info->key == tableData->info->key);
                BOOST_TEST(m_tableData->info->fields == tableData->info->fields);

                if (tableData->info->name != "_sys_current_state_")
                {
                    for (size_t j = 0; j < m_tableData->dirtyEntries->size(); ++j)
                    {
                        auto m_entry = m_tableData->dirtyEntries->get(j);
                        auto entry = tableData->dirtyEntries->get(j);

                        // BOOST_TEST(*(m_entry->fields()) == *(entry->fields()));

                        BOOST_TEST(m_entry->getID() == entry->getID());
                        BOOST_TEST(m_entry->getStatus() == entry->getStatus());
                        BOOST_TEST(m_entry->getTempIndex() == entry->getTempIndex());
                        // BOOST_TEST(m_entry->num() == entry->num());
                        BOOST_TEST(m_entry->dirty() == entry->dirty());
                        BOOST_TEST(m_entry->force() == entry->force());
                        // BOOST_TEST(m_entry->refCount() == entry->refCount());
                        BOOST_TEST(m_entry->deleted() == entry->deleted());
                        BOOST_TEST(m_entry->capacity() == entry->capacity());
                    }
                }
            }
        }

        m_num = num;
        m_datas = datas;

        return 0;
    }

    h256 m_hash;
    int64_t m_num = 0;
    std::vector<TableData::Ptr> m_datas;
    std::mutex m_mutex;
};

BOOST_AUTO_TEST_CASE(commitCheck)
{
    cachedStorage = std::make_shared<CachedStorage>();
    cachedStorage->setMaxCapacity(256 * 1024 * 1024);
    cachedStorage->setMaxForwardBlock(100);
    auto backend = std::make_shared<CommitCheckMock>();
    cachedStorage->setBackend(backend);

    auto userTable = std::make_shared<TableInfo>();
    userTable->key = "key";
    userTable->fields.push_back("value");
    userTable->name = "_dag_transfer_";

    auto txTable = std::make_shared<TableInfo>();
    txTable->key = "txhash";
    txTable->fields.push_back("number");
    txTable->name = "_sys_txhash_2_block_";

    TableData::Ptr newUserData = std::make_shared<TableData>();
    TableData::Ptr newTXData = std::make_shared<TableData>();
    Entries::Ptr newUser = std::make_shared<Entries>();
    Entries::Ptr newTX = std::make_shared<Entries>();
    for (auto i = 0; i < 100; ++i)
    {
        Entry::Ptr entry = std::make_shared<Entry>();
        entry->setField("key", boost::lexical_cast<std::string>(i));
        entry->setField("value", "value " + boost::lexical_cast<std::string>(i));
        entry->setForce(true);
        entry->setID(i);
        newUser->addEntry(entry);

        Entry::Ptr entry2 = std::make_shared<Entry>();
        entry2->setField("txhash", boost::lexical_cast<std::string>(i));
        entry2->setField("number", boost::lexical_cast<std::string>(i + 100));
        entry->setID(100 + i);
        entry2->setForce(true);
        newTX->addEntry(entry2);
    }

    newUserData->newEntries = newUser;
    newUserData->info = userTable;
    newTXData->newEntries = newTX;
    newTXData->info = txTable;

    std::vector<TableData::Ptr> datas = {newUserData, newTXData};

    cachedStorage->commit(1, datas);

    for (size_t idx = 0; idx < 100; ++idx)
    {
        TableData::Ptr newUserData = std::make_shared<TableData>();
        TableData::Ptr newTXData = std::make_shared<TableData>();
        Entries::Ptr newUser = std::make_shared<Entries>();
        Entries::Ptr newTX = std::make_shared<Entries>();
        for (auto i = 0; i < 100; ++i)
        {
            auto entries = cachedStorage->select(idx + 1, userTable,
                boost::lexical_cast<std::string>(i), std::make_shared<Condition>());

            auto entry = entries->get(0);
            entry->setField("key", boost::lexical_cast<std::string>(i));
            entry->setField("value", "value " + boost::lexical_cast<std::string>(i));
            newUser->addEntry(entry);

            Entry::Ptr entry2 = std::make_shared<Entry>();
            entry2->setField("txhash", boost::lexical_cast<std::string>(i));
            entry2->setField("number", boost::lexical_cast<std::string>(i + 100));
            newTX->addEntry(entry2);
        }

        newUserData->dirtyEntries = newUser;
        newUserData->info = userTable;
        newTXData->newEntries = newTX;
        newTXData->info = txTable;

        std::vector<TableData::Ptr> datas = {newUserData, newTXData};

        cachedStorage->commit(idx + 1, datas);
    }
}

BOOST_AUTO_TEST_CASE(clearCommitCheck)
{
    cachedStorage = std::make_shared<CachedStorage>();
    cachedStorage->setMaxCapacity(256 * 1024 * 1024);
    cachedStorage->setMaxForwardBlock(100);
    auto backend = std::make_shared<MemoryStorage2>();
    cachedStorage->setBackend(backend);
    cachedStorage->setMaxCapacity(10);   // byte
    cachedStorage->setClearInterval(1);  // ms
    cachedStorage->startClearThread();
    auto userTable = std::make_shared<TableInfo>();
    userTable->key = "key";
    userTable->fields.push_back("value");
    userTable->name = "t_multi_entry";

    auto txTable = std::make_shared<TableInfo>();
    txTable->key = "txhash";
    txTable->fields.push_back("number");
    txTable->name = "txTable";

    TableData::Ptr newUserData = std::make_shared<TableData>();
    TableData::Ptr newTXData = std::make_shared<TableData>();
    Entries::Ptr newUser = std::make_shared<Entries>();
    Entries::Ptr newTX = std::make_shared<Entries>();
    for (auto i = 0; i < 10; ++i)
    {
        Entry::Ptr entry = std::make_shared<Entry>();
        entry->setField("key", boost::lexical_cast<std::string>(1));
        entry->setField("value", "value " + boost::lexical_cast<std::string>(i));
        entry->setID(i);
        newUser->addEntry(entry);

        Entry::Ptr entry2 = std::make_shared<Entry>();
        entry2->setField("txhash", boost::lexical_cast<std::string>(1));
        entry2->setField("number", boost::lexical_cast<std::string>(i + 100));
        entry->setID(100000 + i);
        newTX->addEntry(entry2);
    }

    newUserData->newEntries = newUser;
    newUserData->info = userTable;
    newTXData->newEntries = newTX;
    newTXData->info = txTable;

    std::vector<TableData::Ptr> datas = {newUserData, newTXData};
    cachedStorage->commit(1, datas);
    newUserData->dirtyEntries = newUser;
    newUserData->newEntries = std::make_shared<Entries>();
    newUserData->dirtyEntries = newUser;
    for (auto i = 2; i < 10; ++i)
    {
        cachedStorage->commit(i, datas);
    }
    cachedStorage->stop();
}

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
    BOOST_CHECK_THROW(AMOP->commit(num, datas, blockHash), boost::exception);
    std::string table("e");
    std::string key("Exception");

    BOOST_CHECK_THROW(AMOP->select(num, table, key, std::make_shared<Condition>()), boost::exception);
#endif
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_CachedStorage
