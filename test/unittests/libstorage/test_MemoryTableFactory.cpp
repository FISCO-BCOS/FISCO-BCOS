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

using namespace dev;
using namespace dev::storage;

namespace test_MemoryTableFactory
{
class MockAMOPDB : public dev::storage::Storage
{
public:
    virtual ~MockAMOPDB() {}


    virtual Entries::Ptr select(
        h256 hash, int num, const std::string& table, const std::string& key) override
    {
        Entries::Ptr entries = std::make_shared<Entries>();
        return entries;
    }

    virtual size_t commit(
        h256 hash, int64_t num, const std::vector<TableData::Ptr>& datas, h256 blockHash) override
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
    int num = 1;
    std::string tableName("t_test");
    std::string keyField("key");
    std::string valueField("value");
    memoryDBFactory->createTable(tableName, keyField, valueField, true);
    MemoryTable::Ptr table =
        std::dynamic_pointer_cast<MemoryTable>(memoryDBFactory->openTable("t_test"));
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
