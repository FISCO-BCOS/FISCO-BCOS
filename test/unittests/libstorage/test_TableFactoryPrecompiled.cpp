#include "unittest/Common.h"
#include <json/json.h>
#include <libdevcrypto/Common.h>
#include <libethcore/ABI.h>
#include <libprecompiled/PrecompiledContext.h>
#include <libstorage/MemoryTableFactory.h>
#include <libstorage/Storage.h>
#include <libstorage/Table.h>
#include <libstorage/TableFactoryPrecompiled.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::precompiled;
using namespace dev::storage;

namespace test_TableFactoryPrecompiled
{
class MockAMOPTable : public dev::storage::Storage
{
public:
    virtual ~MockAMOPTable() {}

    virtual TableInfo::Ptr info(const std::string& table) override
    {
        TableInfo::Ptr info = std::make_shared<TableInfo>();
        if (table != "_sys_tables_")
        {
            info->fields.push_back("item_name");
            info->fields.push_back("item_id");
            info->key = "id";
            info->name = "t_test";
        }
        else
        {
            info->fields.push_back("key_field");
            info->fields.push_back("value_field");
            info->key = "tableName";
            info->name = "_sys_tables_";
        }
        return info;
    }

    virtual Entries::Ptr select(
        h256 hash, int num, const std::string& table, const std::string& key) override
    {
        Entries::Ptr entries = std::make_shared<Entries>();
        return entries;
    }

    virtual size_t commit(
        h256 hash, int num, const std::vector<TableData::Ptr>& datas, h256 blockHash) override
    {
        return 0;
    }

    virtual bool onlyDirty() override { return false; }
};

class MockMemoryTableFactory : public dev::storage::MemoryTableFactory
{
public:
    virtual ~MockMemoryTableFactory(){};

    virtual dev::storage::Table::Ptr openTable(h256 blockHash, int num, const std::string& table)
    {
        return dev::storage::Table::Ptr();
    }
};

class MockPrecompiledEngine : public PrecompiledContext
{
public:
    virtual ~MockPrecompiledEngine() {}
};

struct TableFactoryPrecompiledFixture
{
    TableFactoryPrecompiledFixture()
    {
        context = std::make_shared<MockPrecompiledEngine>();
        BlockInfo blockInfo{h256(0x001), u256(1)};
        context->setBlockInfo(blockInfo);
        dbFactoryPrecompiled = std::make_shared<dev::precompiled::TableFactoryPrecompiled>();
        mockAMOPTable = std::make_shared<MockAMOPTable>();
        auto mockMemoryTableFactory = std::make_shared<MockMemoryTableFactory>();
        mockMemoryTableFactory->setStateStorage(mockAMOPTable);
        dbFactoryPrecompiled->setMemoryTableFactory(mockMemoryTableFactory);
    }

    ~TableFactoryPrecompiledFixture() {}
    Storage::Ptr mockAMOPTable;
    dev::precompiled::TableFactoryPrecompiled::Ptr dbFactoryPrecompiled;
    PrecompiledContext::Ptr context;
    int addressCount = 0x10000;
};

BOOST_FIXTURE_TEST_SUITE(TableFactoryPrecompiled, TableFactoryPrecompiledFixture)

BOOST_AUTO_TEST_CASE(beforeBlock)
{
    dbFactoryPrecompiled->beforeBlock(context);
}

BOOST_AUTO_TEST_CASE(afterBlock)
{
    dbFactoryPrecompiled->afterBlock(context, false);
}

BOOST_AUTO_TEST_CASE(toString)
{
    BOOST_TEST(dbFactoryPrecompiled->toString(context) == "TableFactory");
}

BOOST_AUTO_TEST_CASE(call_afterBlock)
{
    // createTable
    dev::eth::ContractABI abi;
    bytes param =
        abi.abiIn("createTable(string,string,string)", "t_test", "id", "item_name,item_id");
    bytes out = dbFactoryPrecompiled->call(context, bytesConstRef(&param));
    Address addressOut;
    abi.abiOut(&out, addressOut);
    ++addressCount;
    // BOOST_TEST(addressOut == Address(++addressCount));

    // createTable exist
    param = abi.abiIn("createTable(string,string,string)", "t_test", "id", "item_name,item_id");
    out = dbFactoryPrecompiled->call(context, bytesConstRef(&param));
    abi.abiOut(&out, addressOut);
    BOOST_TEST(addressOut <= Address(0x2));

    // openTable not exist
    param.clear();
    out.clear();
    param = abi.abiIn("openTable(string)", "t_poor");
    out = dbFactoryPrecompiled->call(context, bytesConstRef(&param));
    addressOut.clear();
    abi.abiOut(&out, addressOut);
    BOOST_TEST(addressOut == Address(0x0));

    // openTable will fail, because createTable data uncommited
    param.clear();
    out.clear();
    param = abi.abiIn("openTable(string)", "t_test");
    out = dbFactoryPrecompiled->call(context, bytesConstRef(&param));
    addressOut.clear();
    abi.abiOut(&out, addressOut);
    BOOST_TEST(addressOut == Address());

#if 0
  // will fail, because openTable failed
  auto dbPrecompiled = std::dynamic_pointer_cast<TablePrecompiled>(
      context->getPrecompiled(addressOut));
  auto memTable = dbPrecompiled->getTable();
  auto entry = memTable->newEntry();
  entry->setField("id", "张三");
  entry->setField("item_id", "4");
  entry->setField("item_name", "M5");
  entry->setStatus(Entry::Status::NORMAL);
  memTable->insert("张三", entry);
#endif
    dbFactoryPrecompiled->afterBlock(context, true);
    // hash
    dbFactoryPrecompiled->hash(context);
}

BOOST_AUTO_TEST_CASE(hash)
{
    h256 h = dbFactoryPrecompiled->hash(context);
    BOOST_TEST(h == h256());
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_TableFactoryPrecompiled
