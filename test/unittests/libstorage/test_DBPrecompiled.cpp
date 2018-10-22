// "Copyright [2018] <fisco-dev>"
#include "Common.h"
#include <libdevcrypto/Common.h>
#include <libethcore/ABI.h>
#include <libstorage/ConditionPrecompiled.h>
#include <libstorage/EntriesPrecompiled.h>
#include <libstorage/EntryPrecompiled.h>
#include <libstorage/MemoryTable.h>
#include <libstorage/Table.h>
#include <libstorage/TablePrecompiled.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::storage;

namespace test_DBPrecompiled
{
class MockPrecompiledEngine : public dev::blockverifier::ExecutiveContext
{
public:
    virtual ~MockPrecompiledEngine() {}

    virtual Address registerPrecompiled(BlockInfo info, Precompiled::Ptr p) { return ++address; }

    h160 address;
};

class MockMemoryDB : public dev::storage::MemoryTable
{
public:
    virtual ~MockMemoryDB() {}
};

struct DBPrecompiledFixture
{
    DBPrecompiledFixture()
    {
        context = std::make_shared<MockPrecompiledEngine>();
        dbPrecompiled = std::make_shared<dev::blockverifier::TablePrecompiled>();
        auto db = std::make_shared<MockMemoryDB>();
        TableInfo::Ptr info = std::make_shared<TableInfo>();
        // info->fields.emplace_back("name");
        // info->fields.emplace_back("_status_");
        // db->setTableInfo(info);
        dbPrecompiled->setTable(db);
    }

    ~DBPrecompiledFixture() {}

    dev::blockverifier::TablePrecompiled::Ptr dbPrecompiled;
    ExecutiveContext::Ptr context;
    BlockInfo blockInfo;
    int addressCount = 0x10000;
};

BOOST_FIXTURE_TEST_SUITE(DBPrecompiled, DBPrecompiledFixture)

BOOST_AUTO_TEST_CASE(getDB)
{
    dbPrecompiled->getTable();
}
BOOST_AUTO_TEST_CASE(hash)
{
    dbPrecompiled->hash();
}

BOOST_AUTO_TEST_CASE(toString)
{
    BOOST_CHECK_EQUAL(dbPrecompiled->toString(context), "DB");
}

BOOST_AUTO_TEST_CASE(call_select)
{
    storage::Condition::Ptr condition = std::make_shared<storage::Condition>();
    condition->EQ("name", "LiSi");
    auto conditionPrecompiled = std::make_shared<ConditionPrecompiled>();
    conditionPrecompiled->setCondition(condition);
    Address conditionAddress = context->registerPrecompiled(conditionPrecompiled);
    eth::ContractABI abi;
    bytes in = abi.abiIn("select(string,address)", "name", conditionAddress);
    bytes out = dbPrecompiled->call(context, bytesConstRef(&in));
    Address entriesAddress;
    abi.abiOut(bytesConstRef(&out), entriesAddress);
    auto entriesPrecompiled =
        std::dynamic_pointer_cast<EntriesPrecompiled>(context->getPrecompiled(entriesAddress));
    auto entries = entriesPrecompiled->getEntries();
    BOOST_TEST_TRUE(entries->size() == 0u);
}

BOOST_AUTO_TEST_CASE(call_insert)
{
    auto entry = std::make_shared<storage::Entry>();
    entry->setField("name", "WangWu");
    auto entryPrecompiled = std::make_shared<EntryPrecompiled>();
    entryPrecompiled->setEntry(entry);

    auto entryAddress = context->registerPrecompiled(entryPrecompiled);
    eth::ContractABI abi;
    bytes in = abi.abiIn("insert(string,address)", "name", entryAddress);
    bytes out = dbPrecompiled->call(context, bytesConstRef(&in));
    u256 num;
    abi.abiOut(bytesConstRef(&out), num);
    BOOST_TEST_TRUE(num == 1u);
}

BOOST_AUTO_TEST_CASE(call_newCondition)
{
    eth::ContractABI abi;
    bytes in = abi.abiIn("newCondition()");
    bytes out1 = dbPrecompiled->call(context, bytesConstRef(&in));
    Address address(++addressCount);
    bytes out2 = abi.abiIn("", address);
    BOOST_TEST_TRUE(out1 == out2);
}

BOOST_AUTO_TEST_CASE(call_newEntry)
{
    eth::ContractABI abi;
    bytes in = abi.abiIn("newEntry()");
    bytes out1 = dbPrecompiled->call(context, bytesConstRef(&in));
    Address address(++addressCount);
    bytes out2 = abi.abiIn("", address);
    BOOST_CHECK(out1 == out2);
}

BOOST_AUTO_TEST_CASE(call_remove)
{
    storage::Condition::Ptr condition = std::make_shared<storage::Condition>();
    condition->EQ("name", "LiSi");
    auto conditionPrecompiled = std::make_shared<ConditionPrecompiled>();
    conditionPrecompiled->setCondition(condition);
    Address conditionAddress = context->registerPrecompiled(conditionPrecompiled);
    eth::ContractABI abi;
    bytes in = abi.abiIn("remove(string,address)", "name", conditionAddress);
    bytes out = dbPrecompiled->call(context, bytesConstRef(&in));
    u256 num;
    abi.abiOut(bytesConstRef(&out), num);
    BOOST_TEST_TRUE(num == 0u);
}

BOOST_AUTO_TEST_CASE(call_update2)
{
    storage::Condition::Ptr condition = std::make_shared<storage::Condition>();
    condition->EQ("name", "LiSi");
    auto conditionPrecompiled = std::make_shared<ConditionPrecompiled>();
    conditionPrecompiled->setCondition(condition);
    Address conditionAddress = context->registerPrecompiled(conditionPrecompiled);
    auto entry = std::make_shared<storage::Entry>();
    auto entryPrecompiled = std::make_shared<EntryPrecompiled>();
    entryPrecompiled->setEntry(entry);
    auto entryAddress = context->registerPrecompiled(entryPrecompiled);
    eth::ContractABI abi;
    bytes in = abi.abiIn("update(string,address,address)", "name", entryAddress, conditionAddress);
    bytes out = dbPrecompiled->call(context, bytesConstRef(&in));
    u256 num;
    abi.abiOut(bytesConstRef(&out), num);
    BOOST_TEST_TRUE(num == 0u);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_DBPrecompiled
