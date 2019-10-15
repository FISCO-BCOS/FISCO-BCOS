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
#include <libdevcrypto/Common.h>
#include <libethcore/ABI.h>
#include <libprecompiled/ConditionPrecompiled.h>
#include <libprecompiled/EntriesPrecompiled.h>
#include <libprecompiled/EntryPrecompiled.h>
#include <libprecompiled/TablePrecompiled.h>
#include <libstorage/MemoryTable2.h>
#include <libstorage/Table.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace std;
using namespace dev::blockverifier;
using namespace dev::storage;

namespace test_TablePrecompiled2
{
class MockPrecompiledEngine : public dev::blockverifier::ExecutiveContext
{
public:
    virtual ~MockPrecompiledEngine() {}
};

class MockMemoryDB : public dev::storage::MemoryTable2
{
public:
    virtual ~MockMemoryDB() {}
};

struct TablePrecompiledFixture2
{
    TablePrecompiledFixture2()
    {
        context = std::make_shared<MockPrecompiledEngine>();
        tablePrecompiled = std::make_shared<dev::blockverifier::TablePrecompiled>();
        auto table = std::make_shared<MockMemoryDB>();
        TableInfo::Ptr info = std::make_shared<TableInfo>();
        info->fields.emplace_back(ID_FIELD);
        info->fields.emplace_back("name");
        info->fields.emplace_back(STATUS);
        table->setTableInfo(info);
        table->setRecorder(
            [&](Table::Ptr, Change::Kind, string const&, vector<Change::Record>&) {});
        tablePrecompiled->setTable(table);
    }

    ~TablePrecompiledFixture2() {}

    dev::blockverifier::TablePrecompiled::Ptr tablePrecompiled;
    ExecutiveContext::Ptr context;
    BlockInfo blockInfo;
    int addressCount = 0x10000;
};

BOOST_FIXTURE_TEST_SUITE(TablePrecompiled2, TablePrecompiledFixture2)

BOOST_AUTO_TEST_CASE(getDB)
{
    tablePrecompiled->getTable();
}

BOOST_AUTO_TEST_CASE(hash)
{
    tablePrecompiled->hash();
}

BOOST_AUTO_TEST_CASE(clear)
{
    auto table = tablePrecompiled->getTable();
    table->clear();
}

BOOST_AUTO_TEST_CASE(toString)
{
    BOOST_CHECK_EQUAL(tablePrecompiled->toString(), "Table");
}

BOOST_AUTO_TEST_CASE(call_select)
{
    storage::Condition::Ptr condition = std::make_shared<storage::Condition>();
    condition->EQ("name", "LiSi");
    auto conditionPrecompiled = std::make_shared<ConditionPrecompiled>();
    conditionPrecompiled->setCondition(condition);
    Address conditionAddress = context->registerPrecompiled(conditionPrecompiled);
    eth::ContractABI abi;
    bytes in = abi.abiIn("select(string,address)", std::string("name"), conditionAddress);
    bytes out = tablePrecompiled->call(context, bytesConstRef(&in));
    Address entriesAddress;
    abi.abiOut(bytesConstRef(&out), entriesAddress);
    auto entriesPrecompiled =
        std::dynamic_pointer_cast<EntriesPrecompiled>(context->getPrecompiled(entriesAddress));
    Entries::ConstPtr entries = entriesPrecompiled->getEntries();
    BOOST_TEST(entries->size() == 0u);
}

BOOST_AUTO_TEST_CASE(call_insert)
{
    auto entry = std::make_shared<storage::Entry>();
    entry->setField("name", "WangWu");
    auto entryPrecompiled = std::make_shared<EntryPrecompiled>();
    entryPrecompiled->setEntry(entry);

    auto entryAddress = context->registerPrecompiled(entryPrecompiled);
    eth::ContractABI abi;
    bytes in = abi.abiIn("insert(string,address)", std::string("name"), entryAddress);
    bytes out = tablePrecompiled->call(context, bytesConstRef(&in));
    u256 num;
    abi.abiOut(bytesConstRef(&out), num);
    BOOST_TEST(num == 1u);
}

BOOST_AUTO_TEST_CASE(call_newCondition)
{
    eth::ContractABI abi;
    bytes in = abi.abiIn("newCondition()");
    bytes out1 = tablePrecompiled->call(context, bytesConstRef(&in));
    Address address(++addressCount);
    bytes out2 = abi.abiIn("", address);
    BOOST_TEST(out1 == out2);
}

BOOST_AUTO_TEST_CASE(call_newEntry)
{
    eth::ContractABI abi;
    bytes in = abi.abiIn("newEntry()");
    bytes out1 = tablePrecompiled->call(context, bytesConstRef(&in));
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
    bytes in = abi.abiIn("remove(string,address)", std::string("name"), conditionAddress);
    bytes out = tablePrecompiled->call(context, bytesConstRef(&in));
    u256 num;
    abi.abiOut(bytesConstRef(&out), num);
    BOOST_TEST(num == 0u);
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
    bytes in = abi.abiIn(
        "update(string,address,address)", std::string("name"), entryAddress, conditionAddress);
    bytes out = tablePrecompiled->call(context, bytesConstRef(&in));
    u256 num;
    abi.abiOut(bytesConstRef(&out), num);
    BOOST_TEST(num == 0u);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_TablePrecompiled2
