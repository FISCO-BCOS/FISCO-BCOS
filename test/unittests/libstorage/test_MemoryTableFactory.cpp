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

#include "Common.h"
#include <libdevcore/FixedHash.h>
#include <libdevcore/easylog.h>
#include <libstorage/Common.h>
#include <libstorage/MemoryTable.h>
#include <libstorage/MemoryTableFactory.h>
#include <libstorage/Storage.h>
#include <libstorage/Table.h>
#include <boost/test/unit_test.hpp>
#include <string>
#include <thread>
#include <vector>

using namespace dev;
using namespace dev::storage;

namespace test_MemoryTableFactory
{
class MockAMOPDB : public dev::storage::Storage
{
public:
    virtual ~MockAMOPDB() {}


    virtual Entries::Ptr select(h256, int, const std::string&, const std::string&) override
    {
        Entries::Ptr entries = std::make_shared<Entries>();
        return entries;
    }

    virtual size_t commit(h256, int64_t, const std::vector<TableData::Ptr>&, h256 const&) override
    {
        return 0;
    }

    virtual bool onlyDirty() override { return false; }
};

struct MemoryTableFactoryFixture
{
    MemoryTableFactoryFixture()
    {
        std::shared_ptr<MockAMOPDB> mockAMOPDB = std::make_shared<MockAMOPDB>();

        memoryDBFactory = std::make_shared<dev::storage::MemoryTableFactory>();
        memoryDBFactory->setStateStorage(mockAMOPDB);

        BOOST_TEST_TRUE(memoryDBFactory->stateStorage() == mockAMOPDB);
    }

    dev::storage::MemoryTableFactory::Ptr memoryDBFactory;
};

BOOST_FIXTURE_TEST_SUITE(MemoryTableFactory, MemoryTableFactoryFixture)

BOOST_AUTO_TEST_CASE(open_Table)
{
    h256 blockHash(0x0101);
    std::string tableName("t_test");
    std::string keyField("key");
    std::string valueField("value");
    memoryDBFactory->createTable(tableName, keyField, valueField, true, Address(), false);
    MemoryTable<Serial>::Ptr table = std::dynamic_pointer_cast<MemoryTable<Serial>>(
        memoryDBFactory->openTable("t_test", true, false));
    table->remove("name", table->newCondition());
    auto entry = table->newEntry();
    entry->setField("key", "name");
    entry->setField("value", "Lili");
    table->insert("name", entry);
    entry = table->newEntry();
    entry->setField("key", "id");
    entry->setField("value", "12345");
    table->update("id", entry, table->newCondition());
    table->insert("id", entry);
    entry->setField("key", "balance");
    entry->setField("value", "500");
    table->insert("balance", entry);
    auto savePoint = memoryDBFactory->savepoint();
    auto condition = table->newCondition();
    condition->EQ("key", "name");
    condition->NE("value", "name");
    table->remove("name", condition);
    memoryDBFactory->rollback(savePoint);
    condition = table->newCondition();
    condition->EQ("key", "balance");
    condition->GT("value", "404");
    table->select("balance", condition);
    condition = table->newCondition();
    condition->EQ("key", "balance");
    condition->GE("value", "403");
    table->select("balance", condition);
    condition = table->newCondition();
    condition->EQ("key", "balance");
    condition->LT("value", "505");
    table->select("balance", condition);
    condition = table->newCondition();
    condition->EQ("key", "balance");
    condition->LE("value", "504");
    table->select("balance", condition);
    memoryDBFactory->commitDB(h256(0), 2);
}

BOOST_AUTO_TEST_CASE(parallel_openTable)
{
    h256 blockHash(0x0101);
    std::string tableName("t_test");
    std::string keyField("key");
    std::string valueField("value");
    MemoryTable<Parallel>::Ptr table = std::dynamic_pointer_cast<MemoryTable<Parallel>>(
        memoryDBFactory->createTable(tableName, keyField, valueField, false, Address(), true));


    std::vector<std::thread> threads;
    for (auto i = 0; i < 3; ++i)
    {
        threads.push_back(std::thread([this, i, table]() {
            auto entry = table->newEntry();
            entry->setField("key", "balance");
            auto initBalance = std::to_string(500 + i);
            entry->setField("value", initBalance);
            auto key = std::to_string(i);

            auto savepoint0 = memoryDBFactory->savepoint();
            BOOST_TEST(savepoint0 == 0);
            table->insert(key, entry);

            auto entries = table->select(key, table->newCondition());
            BOOST_TEST(entries->size() == 1);
            BOOST_TEST(entries->get(0)->getField("value") == initBalance);

            std::this_thread::sleep_for(std::chrono::milliseconds((i + 1) * 100));

            auto savepoint1 = memoryDBFactory->savepoint();
            BOOST_TEST(savepoint1 == 1);

            entry = table->newEntry();
            entry->setField("key", "balance");
            entry->setField("value", std::to_string((i + 1) * 100));
            table->update(key, entry, table->newCondition());
            entries = table->select(key, table->newCondition());
            BOOST_TEST(entries->size() == 1);
            BOOST_TEST(entries->get(0)->getField("value") == std::to_string((i + 1) * 100));

            memoryDBFactory->rollback(savepoint1);
            entries = table->select(key, table->newCondition());
            BOOST_TEST(entries->size() == 1);
            BOOST_TEST(entries->get(0)->getField("value") == initBalance);

            memoryDBFactory->rollback(savepoint0);
            entries = table->select(key, table->newCondition());
            BOOST_TEST(entries->size() == 0);

            entry = table->newEntry();
            entry->setField("key", "name");
            entry->setField("value", "Vita");
            table->insert(key, entry);

            entries = table->select(key, table->newCondition());
            BOOST_TEST(entries->size() == 1);

            auto savepoint2 = memoryDBFactory->savepoint();

            table->remove(key, table->newCondition());
            entries = table->select(key, table->newCondition());
            BOOST_TEST(entries->size() == 1);
            BOOST_TEST(entries->get(0)->getStatus() == 1);

            memoryDBFactory->rollback(savepoint2);
            entries = table->select(key, table->newCondition());
            BOOST_TEST(entries->size() == 1);
            BOOST_TEST(entries->get(0)->getStatus() == 0);
        }));
    }

    for (auto& t : threads)
    {
        t.join();
    }

    threads.clear();

    for (auto i = 0; i < 3; ++i)
    {
        threads.push_back(std::thread([this, i, table]() {
            auto entry = table->newEntry();
            entry->setField("key", "balance");
            auto initBalance = std::to_string(500 + i);
            entry->setField("value", initBalance);
            auto key = std::to_string(i + 10);

            auto savepoint0 = memoryDBFactory->savepoint();
            BOOST_TEST(savepoint0 == 0);
            table->insert(key, entry);

            auto entries = table->select(key, table->newCondition());
            BOOST_TEST(entries->size() == 1);
            BOOST_TEST(entries->get(0)->getField("value") == initBalance);

            memoryDBFactory->rollback(savepoint0);
            entries = table->select(key, table->newCondition());
            BOOST_TEST(entries->size() == 0);
        }));
    }

    for (auto& t : threads)
    {
        t.join();
    }
    memoryDBFactory->commitDB(h256(0), 2);
}

BOOST_AUTO_TEST_CASE(open_sysTables)
{
    auto table = memoryDBFactory->openTable(SYS_CURRENT_STATE);
    table = memoryDBFactory->openTable(SYS_NUMBER_2_HASH);
    table = memoryDBFactory->openTable(SYS_TX_HASH_2_BLOCK);
    table = memoryDBFactory->openTable(SYS_HASH_2_BLOCK);
}

BOOST_AUTO_TEST_CASE(setBlockHash)
{
    memoryDBFactory->setBlockHash(h256(0x12345));
}

BOOST_AUTO_TEST_CASE(setBlockNum)
{
    memoryDBFactory->setBlockNum(2);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_MemoryTableFactory
