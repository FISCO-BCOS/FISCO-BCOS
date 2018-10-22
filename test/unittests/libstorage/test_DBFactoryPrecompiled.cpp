#include "Common.h"
#include <libblockverifier/ExecutiveContext.h>
#include <libdevcrypto/Common.h>
#include <libethcore/ABI.h>
#include <libstorage/MemoryTableFactory.h>
#include <libstorage/Storage.h>
#include <libstorage/Table.h>
#include <libstorage/TableFactoryPrecompiled.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::storage;

namespace test_DBFactoryPrecompiled
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
            info->key = "id";
            info->name = "t_test";
        }
        else
        {
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
        h256 hash, int64_t num, const std::vector<TableData::Ptr>& datas, h256 blockHash) override
    {
        return 0;
    }

    virtual bool onlyDirty() override { return false; }
};

class MockMemoryDBFactory : public dev::storage::MemoryTableFactory
{
public:
    virtual ~MockMemoryDBFactory(){};

    virtual dev::storage::Table::Ptr openDB(h256 blockHash, int num, const std::string& table)
    {
        return dev::storage::Table::Ptr();
    }
};

class MockPrecompiledEngine : public dev::blockverifier::ExecutiveContext
{
public:
    virtual ~MockPrecompiledEngine() {}
};

struct DBFactoryPrecompiledFixture
{
    DBFactoryPrecompiledFixture()
    {
        context = std::make_shared<MockPrecompiledEngine>();
        dbFactoryPrecompiled = std::make_shared<dev::blockverifier::TableFactoryPrecompiled>();
        mockAMOPDB = std::make_shared<MockAMOPDB>();
        auto mockMemoryDBFactory = std::make_shared<MockMemoryDBFactory>();
        mockMemoryDBFactory->setStateStorage(mockAMOPDB);
        dbFactoryPrecompiled->setMemoryTableFactory(mockMemoryDBFactory);
    }

    ~DBFactoryPrecompiledFixture() {}
    Storage::Ptr mockAMOPDB;
    dev::blockverifier::TableFactoryPrecompiled::Ptr dbFactoryPrecompiled;
    dev::blockverifier::ExecutiveContext::Ptr context;
    int addressCount = 0x10000;
};

BOOST_FIXTURE_TEST_SUITE(DBFactoryPrecompiled, DBFactoryPrecompiledFixture)

BOOST_AUTO_TEST_CASE(toString)
{
    BOOST_CHECK_EQUAL((dbFactoryPrecompiled->toString(context) == "DBFactory"), true);
}

BOOST_AUTO_TEST_CASE(openDB)
{
#if 0
	BlockInfo info;
	info.hash = h256(0x12345);
	info.number = 1000;

	dbFactoryPrecompiled->beforeBlock(info);

	dev::eth::ContractABI abi;
	bytes param = abi.abiIn("openDB(string)", "t_test");
	bytes out = dbFactoryPrecompiled->call(info, &param);

	Address address;
	abi.abiOut(&out, address);

	BOOST_TEST(address == h160(0x1));

	//同一块内第二次打开t_test，地址一样
	out.clear();
	address.clear();
	out = dbFactoryPrecompiled->call(info, &param);
	abi.abiOut(&out, address);

	BOOST_TEST(address == h160(0x1));

	//同一块内打开别的表，新地址
	param = abi.abiIn("openDB(string)", "t_test2");
	out.clear();
	address.clear();

	out = dbFactoryPrecompiled->call(info, &param);
	abi.abiOut(&out, address);

	BOOST_TEST(address == h160(0x2));

	dbFactoryPrecompiled->afterBlock(info, true);

	info.hash = h256(0x22345);
	dbFactoryPrecompiled->beforeBlock(info);

	//另一块内打开别的表，新地址
	param = abi.abiIn("openDB(string)", "t_test");
	out.clear();
	address.clear();

	out = dbFactoryPrecompiled->call(info, &param);
	abi.abiOut(&out, address);

	BOOST_TEST(address == h160(0x3));
#endif
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
    BOOST_CHECK_EQUAL((addressOut <= Address(0x2)), true);

    // openDB not exist
    param.clear();
    out.clear();
    param = abi.abiIn("openTable(string)", "t_poor");
    out = dbFactoryPrecompiled->call(context, bytesConstRef(&param));
    addressOut.clear();
    abi.abiOut(&out, addressOut);
    BOOST_CHECK_EQUAL((addressOut == Address(0x0)), true);

    // openTable will fail, because createTable data uncommited
    param.clear();
    out.clear();
    param = abi.abiIn("openTable(string)", "t_test");
    out = dbFactoryPrecompiled->call(context, bytesConstRef(&param));
    addressOut.clear();
    abi.abiOut(&out, addressOut);
    BOOST_CHECK_EQUAL((addressOut == Address()), true);

#if 0
  // will fail, because openTable failed
  auto dbPrecompiled = std::dynamic_pointer_cast<DBPrecompiled>(
      context->getPrecompiled(addressOut));
  auto memDB = dbPrecompiled->getDB();
  auto entry = memDB->newEntry();
  entry->setField("id", "张三");
  entry->setField("item_id", "4");
  entry->setField("item_name", "M5");
  entry->setStatus(Entry::Status::NORMAL);
  memDB->insert("张三", entry);
#endif
    // hash
    dbFactoryPrecompiled->hash(context);
}

BOOST_AUTO_TEST_CASE(hash)
{
    h256 h = dbFactoryPrecompiled->hash(context);
    BOOST_CHECK_EQUAL((h == h256()), true);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_DBFactoryPrecompiled
