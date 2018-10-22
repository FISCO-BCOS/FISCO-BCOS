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

namespace test_MemoryDBFactory
{
class MockAMOPDB : public dev::storage::Storage
{
public:
    virtual ~MockAMOPDB() {}

    virtual TableInfo::Ptr info(const std::string& table) override
    {
        TableInfo::Ptr info = std::make_shared<TableInfo>();
        if (table != "_sys_tables_")
        {
            BOOST_CHECK(table == "t_test");
            // info->fields.push_back("姓名");
            // info->fields.push_back("资产号");
            info->key = "姓名";
            info->name = "t_test";
        }
        else
        {
            // info->fields.push_back("key_field");
            // info->fields.push_back("value_field");
            info->key = "key";
            info->name = "_sys_tables_";
        }
        return info;
    }

    virtual Entries::Ptr select(
        h256 hash, int num, const std::string& table, const std::string& key) override
    {
        // BOOST_TEST(table == "t_test");

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

struct MemoryDBFactoryFixture
{
    MemoryDBFactoryFixture()
    {
        std::shared_ptr<MockAMOPDB> mockAMOPDB = std::make_shared<MockAMOPDB>();

        memoryDBFactory = std::make_shared<dev::storage::MemoryTableFactory>();
        memoryDBFactory->setStateStorage(mockAMOPDB);

        BOOST_TEST_TRUE(memoryDBFactory->stateStorage() == mockAMOPDB);
    }

    dev::storage::MemoryTableFactory::Ptr memoryDBFactory;
};

BOOST_FIXTURE_TEST_SUITE(MemoryDBFactory, MemoryDBFactoryFixture)

BOOST_AUTO_TEST_CASE(open_Table)
{
    h256 blockHash(0x0101);
    int num = 1;
    std::string tableName("t_test");
    std::string keyField("hash");
    std::vector<std::string> valueField{"hash", "num", "name", "item_id", "item_name", "status"};
    memoryDBFactory->createTable(blockHash, num, tableName, keyField, valueField);
    MemoryTable::Ptr db = std::dynamic_pointer_cast<MemoryTable>(
        memoryDBFactory->openTable(h256(0x12345), 1, "t_test"));
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

}  // namespace test_MemoryDBFactory
