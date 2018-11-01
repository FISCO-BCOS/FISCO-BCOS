#include "Common.h"
#include <libdevcore/FixedHash.h>
#include <libdevcore/easylog.h>
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
    memoryDBFactory->createTable(tableName, keyField, valueField);
    MemoryTable::Ptr db =
        std::dynamic_pointer_cast<MemoryTable>(memoryDBFactory->openTable("t_test"));
    auto entry = db->newEntry();
    entry->setField("key", "name");
    entry->setField("value", "Lili");
    db->insert("name", entry);
    entry->setField("key", "balance");
    entry->setField("value", "500");
    db->insert("balance", entry);
    auto condition = db->newCondition();
    condition->EQ("key", "name");
    condition->NE("value", "name");
    db->remove("name", condition);
    condition = db->newCondition();
    condition->EQ("key", "balance");
    condition->GT("value", "404");
    condition->GE("value", "404");
    condition->LT("value", "505");
    condition->LE("value", "505");
    db->remove("balance", condition);
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
