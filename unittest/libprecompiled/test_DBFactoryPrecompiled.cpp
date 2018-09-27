#include <json/json.h>
#include <libdevcrypto/Common.h>
#include <libethcore/ABI.h>
#include <libprecompiled/PrecompiledContext.h>
#include <libstorage/DBFactoryPrecompiled.h>
#include <libstorage/MemoryDBFactory.h>
#include <libstorage/DB.h>
#include <libstorage/Storage.h>
#include <boost/test/unit_test.hpp>
#include "unittest/Common.h"

using namespace dev;
using namespace dev::precompiled;
using namespace dev::storage;

namespace test_DBFactoryPrecompiled {

class MockAMOPDB : public dev::storage::Storage {
 public:
  virtual ~MockAMOPDB() {}

  virtual TableInfo::Ptr info(const std::string &table) override {
    TableInfo::Ptr info = std::make_shared<TableInfo>();
    if (table != "_sys_tables_") {
      info->indices.push_back("姓名");
      info->indices.push_back("资产号");
      info->key = "姓名";
      info->name = "t_test";
    } else {
      info->indices.push_back("key_field");
      info->indices.push_back("value_field");
      info->key = "key";
      info->name = "_sys_tables_";
    }
    return info;
  }

  virtual Entries::Ptr select(h256 hash, int num, const std::string &table,
                              const std::string &key) override {
    Entries::Ptr entries = std::make_shared<Entries>();
    return entries;
  }

  virtual size_t commit(h256 hash, int num,
                        const std::vector<TableData::Ptr> &datas,
                        h256 blockHash) override {
    return 0;
  }

  virtual bool onlyDirty() override { return false; }
};

class MockMemoryDBFactory : public dev::storage::MemoryDBFactory {
 public:
  virtual ~MockMemoryDBFactory(){};

  virtual dev::storage::DB::Ptr openDB(h256 blockHash, int num,
                                            const std::string &table) {
    return dev::storage::DB::Ptr();
  }
};

class MockPrecompiledEngine : public PrecompiledContext {
 public:
  virtual ~MockPrecompiledEngine() {}
};

struct DBFactoryPrecompiledFixture {
  DBFactoryPrecompiledFixture() {
    context = std::make_shared<MockPrecompiledEngine>();
    BlockInfo blockInfo{h256(0x001), u256(1)};
    context->setBlockInfo(blockInfo);
    dbFactoryPrecompiled =
        std::make_shared<dev::precompiled::DBFactoryPrecompiled>();
    mockAMOPDB = std::make_shared<MockAMOPDB>();
    auto mockMemoryDBFactory = std::make_shared<MockMemoryDBFactory>();
    mockMemoryDBFactory->setStateStorage(mockAMOPDB);
    dbFactoryPrecompiled->setMemoryDBFactory(mockMemoryDBFactory);
  }

  ~DBFactoryPrecompiledFixture() {
    
  }
  Storage::Ptr mockAMOPDB;
  dev::precompiled::DBFactoryPrecompiled::Ptr dbFactoryPrecompiled;
  PrecompiledContext::Ptr context;
  int addressCount = 0x10000;
};

BOOST_FIXTURE_TEST_SUITE(DBFactoryPrecompiled, DBFactoryPrecompiledFixture)

BOOST_AUTO_TEST_CASE(beforeBlock) {
  dbFactoryPrecompiled->beforeBlock(context);
}

BOOST_AUTO_TEST_CASE(afterBlock) {
  dbFactoryPrecompiled->afterBlock(context, false);
}

BOOST_AUTO_TEST_CASE(toString) {
  BOOST_TEST(dbFactoryPrecompiled->toString(context) == "DBFactory");
}

BOOST_AUTO_TEST_CASE(openDB) {
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

BOOST_AUTO_TEST_CASE(call_afterBlock) {
  // createTable
  dev::eth::ContractABI abi;
  bytes param = abi.abiIn("createTable(string,string,string)", "t_test", "id",
                          "name,item_id");
  bytes out = dbFactoryPrecompiled->call(context, bytesConstRef(&param));
  Address addressOut;
  abi.abiOut(&out, addressOut);
  ++addressCount;
  // BOOST_TEST(addressOut == Address(++addressCount));
 
   // createTable exist
  param = abi.abiIn("createTable(string,string,string)", "t_test", "id",
                          "name,item_id");
  out = dbFactoryPrecompiled->call(context, bytesConstRef(&param));
  abi.abiOut(&out, addressOut);
  // BOOST_TEST(addressOut == Address(addressCount));

  // openDB not exist
  param.clear();
  out.clear();
  param = abi.abiIn("openDB(string)", "t_poor");
  out = dbFactoryPrecompiled->call(context, bytesConstRef(&param));
  addressOut.clear();
  abi.abiOut(&out, addressOut);
  BOOST_TEST(addressOut == Address(0x0));

  // openDB exist
  param.clear();
  out.clear();
  param = abi.abiIn("openDB(string)", "t_test");
  out = dbFactoryPrecompiled->call(context, bytesConstRef(&param));
  addressOut.clear();
  abi.abiOut(&out, addressOut);
  BOOST_TEST(addressOut == Address(++addressCount));

  // dbFactoryPrecompiled
  auto dbPrecompiled = std::dynamic_pointer_cast<DBPrecompiled>(
      context->getPrecompiled(addressOut));
  auto memDB = dbPrecompiled->getDB();
  auto entry = memDB->newEntry();
  entry->setField("姓名", "张三");
  entry->setField("资产号", "4");
  entry->setField("资产名", "M5");
  entry->setStatus(Entry::Status::NORMAL);
  memDB->insert("张三", entry);
  dbFactoryPrecompiled->afterBlock(context, true);
  // hash
  dbFactoryPrecompiled->hash(context);
}

BOOST_AUTO_TEST_CASE(hash) {
  h256 h = dbFactoryPrecompiled->hash(context);
  BOOST_TEST(h == h256());
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_DBFactoryPrecompiled
