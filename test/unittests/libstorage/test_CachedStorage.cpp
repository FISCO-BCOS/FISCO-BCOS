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

using namespace dev;
using namespace dev::storage;

namespace test_CachedStorage
{

class MockStorage: public Storage {
public:
	virtual Entries::Ptr select(h256 hash, int num, const std::string& table, const std::string& key, Condition::Ptr condition = nullptr) override {
		(void)hash;
		(void)num;

		if(commited) {
			BOOST_TEST(false);
			return nullptr;
		}

		auto entries = std::make_shared<Entries>();

		BOOST_TEST(condition == nullptr);

		if(table == SYS_CURRENT_STATE && key == SYS_KEY_CURRENT_ID) {
			Entry::Ptr entry = std::make_shared<Entry>();
			entry->setID(1);
			entry->setNum(100);
			entry->setField(SYS_KEY, SYS_KEY_CURRENT_ID);
			entry->setField(SYS_VALUE, "100");
			entry->setStatus(0);

			entries->addEntry(entry);
			return entries;
		}

		if(table == "t_test") {
			if(key == "LiSi") {
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

	virtual size_t commit(h256 hash, int64_t num, const std::vector<TableData::Ptr>& datas) override {
		(void)datas;

		BOOST_CHECK(hash == h256());
		BOOST_CHECK(num == 101);

		BOOST_CHECK(datas.size() == 2);
		for(auto it: datas) {
			if(it->info->name == "t_test") {
				BOOST_CHECK(it->entries->size() == 1);
			}
			else if(it->info->name == SYS_CURRENT_STATE) {
				BOOST_CHECK(it->entries->size() == 1);
			}
			else {
				BOOST_CHECK(false);
			}
		}

		lock.unlock();

		commited = true;
		return 0;
	}

	virtual bool onlyDirty() override {
		return true;
	}

	std::mutex lock;
	bool commited = false;
};

struct CachedStorageFixture
{
	CachedStorageFixture()
    {
		cachedStorage = std::make_shared<CachedStorage>();
		mockStorage = std::make_shared<MockStorage>();

		cachedStorage->setBackend(mockStorage);
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

BOOST_AUTO_TEST_CASE(setBackend) {
	auto backend = Storage::Ptr();
	cachedStorage->setBackend(backend);
}

BOOST_AUTO_TEST_CASE(init) {
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
    Entries::Ptr entries = cachedStorage->select(h, num, table, key, std::make_shared<Condition>());
    BOOST_CHECK_EQUAL(entries->size(), 0u);
}

BOOST_AUTO_TEST_CASE(select_condition)
{
	//query from backend
    h256 h(0x01);
    int num = 1;
    std::string table("t_test");
    auto condition = std::make_shared<Condition>();
    condition->EQ("id", "2");
    Entries::Ptr entries = cachedStorage->select(h, num, table, "LiSi", condition);
    BOOST_CHECK_EQUAL(entries->size(), 0u);

    //query from cache
    mockStorage->commited = true;
    condition = std::make_shared<Condition>();
    condition->EQ("id", "1");
    entries = cachedStorage->select(h, num, table, "LiSi", condition);
    BOOST_CHECK_EQUAL(entries->size(), 1u);
}

BOOST_AUTO_TEST_CASE(commit)
{
    h256 h;
    int64_t num = 101;
    std::vector<dev::storage::TableData::Ptr> datas;
    dev::storage::TableData::Ptr tableData = std::make_shared<dev::storage::TableData>();
    tableData->info->name = "t_test";
    tableData->info->key = "Name";
    tableData->info->fields.push_back("id");
    Entries::Ptr entries = getEntries();
    tableData->entries = entries;
    datas.push_back(tableData);

    mockStorage->lock.lock();
    size_t c = cachedStorage->commit(h, num, datas);
    BOOST_TEST(cachedStorage->syncNum() == 0);
    mockStorage->lock.lock();
    mockStorage->lock.unlock();
    BOOST_TEST(cachedStorage->syncNum() == 101);

    BOOST_CHECK_EQUAL(c, 1u);
    std::string table("t_test");
    std::string key("LiSi");
    entries = cachedStorage->select(h, num, table, key, std::make_shared<Condition>());
    BOOST_CHECK_EQUAL(entries->size(), 2u);

    for(size_t i=0; i<entries->size(); ++i) {
    	auto entry = entries->get(i);
    	if(entry->getField("id") == "1") {
    		BOOST_TEST(entry->getID() == 1);
    	}
    	else if(entry->getField("id") == "2") {
    		BOOST_TEST(entry->getID() == 2);
    	}
    	else {
    		BOOST_TEST(false);
    	}
    }
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
    BOOST_CHECK_THROW(AMOP->commit(h, num, datas, blockHash), boost::exception);
    std::string table("e");
    std::string key("Exception");

    BOOST_CHECK_THROW(AMOP->select(h, num, table, key, std::make_shared<Condition>()), boost::exception);
#endif
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_AMOPStateStorage
