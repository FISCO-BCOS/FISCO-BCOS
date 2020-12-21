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
#include <libprecompiled/ConditionPrecompiled.h>
#include <libprecompiled/EntriesPrecompiled.h>
#include <libprecompiled/EntryPrecompiled.h>
#include <libprecompiled/TablePrecompiled.h>
#include <libprotocol/ContractABICodec.h>
#include <libstorage/MemoryTable.h>
#include <libstorage/MemoryTableFactory.h>
#include <libstorage/Table.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace std;
using namespace bcos::blockverifier;
using namespace bcos::precompiled;
using namespace bcos::storage;

namespace test_TablePrecompiled
{
class MockPrecompiledEngine : public bcos::blockverifier::ExecutiveContext
{
public:
    virtual ~MockPrecompiledEngine() {}
};

class MockMemoryDB : public bcos::storage::MemoryTable
{
public:
    virtual ~MockMemoryDB() {}
};

struct TablePrecompiledFixture
{
    TablePrecompiledFixture()
    {
        context = std::make_shared<MockPrecompiledEngine>();
        context->setMemoryTableFactory(std::make_shared<storage::MemoryTableFactory>());
        tablePrecompiled = std::make_shared<bcos::precompiled::TablePrecompiled>();
        auto table = std::make_shared<MockMemoryDB>();
        TableInfo::Ptr info = std::make_shared<TableInfo>();
        info->fields.emplace_back("name");
        info->fields.emplace_back(STATUS);
        table->setTableInfo(info);
        table->setRecorder(
            [&](Table::Ptr, Change::Kind, string const&, vector<Change::Record>&) {});
        tablePrecompiled->setTable(table);

        auto precompiledGasFactory = std::make_shared<bcos::precompiled::PrecompiledGasFactory>(0);
        auto precompiledExecResultFactory =
            std::make_shared<bcos::precompiled::PrecompiledExecResultFactory>();
        precompiledExecResultFactory->setPrecompiledGasFactory(precompiledGasFactory);
        tablePrecompiled->setPrecompiledExecResultFactory(precompiledExecResultFactory);
    }

    ~TablePrecompiledFixture() {}

    bcos::precompiled::TablePrecompiled::Ptr tablePrecompiled;
    ExecutiveContext::Ptr context;
    BlockInfo blockInfo;
    int addressCount = 0x10000;
};

BOOST_FIXTURE_TEST_SUITE(TablePrecompiled, TablePrecompiledFixture)

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
    protocol::ContractABICodec abi;
    bytes in = abi.abiIn("select(string,address)", std::string("name"), conditionAddress);
    auto callResult = tablePrecompiled->call(context, bytesConstRef(&in));
    bytes out = callResult->execResult();
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
    protocol::ContractABICodec abi;
    bytes in = abi.abiIn("insert(string,address)", std::string("name"), entryAddress);
    auto callResult = tablePrecompiled->call(context, bytesConstRef(&in));
    bytes out = callResult->execResult();
    u256 num;
    abi.abiOut(bytesConstRef(&out), num);
    BOOST_TEST(num == 1u);
}

BOOST_AUTO_TEST_CASE(call_newCondition)
{
    protocol::ContractABICodec abi;
    bytes in = abi.abiIn("newCondition()");
    auto callResult = tablePrecompiled->call(context, bytesConstRef(&in));
    bytes out1 = callResult->execResult();
    Address address(++addressCount);
    bytes out2 = abi.abiIn("", address);
    BOOST_TEST(out1 == out2);
}

BOOST_AUTO_TEST_CASE(call_newEntry)
{
    protocol::ContractABICodec abi;
    bytes in = abi.abiIn("newEntry()");
    auto callResult = tablePrecompiled->call(context, bytesConstRef(&in));
    bytes out1 = callResult->execResult();
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
    protocol::ContractABICodec abi;
    bytes in = abi.abiIn("remove(string,address)", std::string("name"), conditionAddress);
    auto callResult = tablePrecompiled->call(context, bytesConstRef(&in));
    bytes out = callResult->execResult();
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
    protocol::ContractABICodec abi;
    bytes in = abi.abiIn(
        "update(string,address,address)", std::string("name"), entryAddress, conditionAddress);
    auto callResult = tablePrecompiled->call(context, bytesConstRef(&in));
    bytes out = callResult->execResult();
    u256 num;
    abi.abiOut(bytesConstRef(&out), num);
    BOOST_TEST(num == 0u);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_TablePrecompiled
