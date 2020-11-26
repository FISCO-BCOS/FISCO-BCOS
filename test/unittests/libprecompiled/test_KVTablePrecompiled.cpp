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

#include "../libstorage/MemoryStorage2.h"
#include "libprecompiled/TableFactoryPrecompiled.h"
#include "libstorage/MemoryTableFactory.h"
#include <libdevcrypto/Common.h>
#include <libethcore/ABI.h>
#include <libprecompiled/ConditionPrecompiled.h>
#include <libprecompiled/EntriesPrecompiled.h>
#include <libprecompiled/EntryPrecompiled.h>
#include <libprecompiled/KVTablePrecompiled.h>
#include <libstorage/MemoryTable.h>
#include <libstorage/Table.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace std;
using namespace bcos::blockverifier;
using namespace bcos::storage;
using namespace bcos::precompiled;

namespace test_KVTablePrecompiled
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

struct TablePrecompiledFixture2
{
    TablePrecompiledFixture2()
    {
        auto memStorage = std::make_shared<MemoryStorage2>();
        MemoryTableFactory::Ptr tableFactory = std::make_shared<MemoryTableFactory>();
        tableFactory->setStateStorage(memStorage);
        context = std::make_shared<MockPrecompiledEngine>();
        context->setMemoryTableFactory(tableFactory);
        auto tableFactoryPrecompiled =
            std::make_shared<bcos::precompiled::TableFactoryPrecompiled>();
        tableFactoryPrecompiled->setMemoryTableFactory(tableFactory);
        context->setAddress2Precompiled(Address(0x1001), tableFactoryPrecompiled);

        tablePrecompiled = std::make_shared<precompiled::KVTablePrecompiled>();
        auto table = std::make_shared<MockMemoryDB>();
        table->setStateStorage(memStorage);
        TableInfo::Ptr info = std::make_shared<TableInfo>();
        info->fields.emplace_back(ID_FIELD);
        info->fields.emplace_back("name");
        info->fields.emplace_back(STATUS);
        okAddress = Address("0x420f853b49838bd3e9466c85a4cc3428c960dde2");
        info->authorizedAddress.emplace_back(okAddress);

        table->setTableInfo(info);
        table->setRecorder(
            [&](Table::Ptr, Change::Kind, string const&, vector<Change::Record>&) {});
        tablePrecompiled->setTable(table);

        auto precompiledGasFactory = std::make_shared<bcos::precompiled::PrecompiledGasFactory>(0);
        auto precompiledExecResultFactory = std::make_shared<PrecompiledExecResultFactory>();
        precompiledExecResultFactory->setPrecompiledGasFactory(precompiledGasFactory);
        tablePrecompiled->setPrecompiledExecResultFactory(precompiledExecResultFactory);
    }

    ~TablePrecompiledFixture2() {}

    precompiled::KVTablePrecompiled::Ptr tablePrecompiled;
    ExecutiveContext::Ptr context;
    BlockInfo blockInfo;
    Address okAddress;
    int addressCount = 0x10000;
};

BOOST_FIXTURE_TEST_SUITE(KVTablePrecompiled, TablePrecompiledFixture2)

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
    BOOST_CHECK_EQUAL(tablePrecompiled->toString(), "KVTable");
}

BOOST_AUTO_TEST_CASE(call_get_set)
{
    eth::ContractABI abi;
    bytes in = abi.abiIn("get(string)", std::string("name"));
    auto callResult = tablePrecompiled->call(context, bytesConstRef(&in));
    bytes out = callResult->execResult();
    bool status = true;
    Address entryAddress;
    abi.abiOut(bytesConstRef(&out), status, entryAddress);
    BOOST_TEST(status == false);
    auto entry = std::make_shared<storage::Entry>();
    entry->setField("name", "WangWu");
    auto entryPrecompiled = std::make_shared<EntryPrecompiled>();
    entryPrecompiled->setEntry(entry);
    auto entryAddress1 = context->registerPrecompiled(entryPrecompiled);
    bytes in1 = abi.abiIn("set(string,address)", std::string("name"), entryAddress1);
    callResult = tablePrecompiled->call(context, bytesConstRef(&in1), okAddress);
    bytes out1 = callResult->execResult();
    u256 num;
    abi.abiOut(bytesConstRef(&out1), num);
    BOOST_TEST(num == 1);
    bytes in2 = abi.abiIn("get(string)", std::string("name"));
    callResult = tablePrecompiled->call(context, bytesConstRef(&in2));
    bytes out2 = callResult->execResult();
    bool status2 = true;
    Address entryAddress2;
    abi.abiOut(bytesConstRef(&out2), status2, entryAddress2);
    BOOST_TEST(status2 == true);
    auto entryPrecompiled2 =
        std::dynamic_pointer_cast<EntryPrecompiled>(context->getPrecompiled(entryAddress2));
    entry = entryPrecompiled2->getEntry();
    BOOST_TEST(entry->getField(std::string("name")) == "WangWu");
}

BOOST_AUTO_TEST_CASE(call_set_permission_denied)
{
    eth::ContractABI abi;
    auto entry = std::make_shared<storage::Entry>();
    entry->setField("name", "WangWu");
    auto entryPrecompiled = std::make_shared<EntryPrecompiled>();
    entryPrecompiled->setEntry(entry);
    auto entryAddress1 = context->registerPrecompiled(entryPrecompiled);
    bytes in1 = abi.abiIn("set(string,address)", std::string("name"), entryAddress1);
    BOOST_CHECK_THROW(tablePrecompiled->call(context, bytesConstRef(&in1)), PrecompiledException);
    auto tooLongKey256 = std::string(
        "555555012345678901234567890123456789012345678901234567890123456789012345678901234567890123"
        "456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123"
        "4567890123456789012345678901234567890123456789012345678901234567890123456789");
    bytes in = abi.abiIn("set(string,address)", tooLongKey256, entryAddress1);
    BOOST_CHECK_THROW(tablePrecompiled->call(context, bytesConstRef(&in)), PrecompiledException);
}

BOOST_AUTO_TEST_CASE(call_newEntry)
{
    eth::ContractABI abi;
    bytes in = abi.abiIn("newEntry()");
    auto callResult = tablePrecompiled->call(context, bytesConstRef(&in));
    bytes out1 = callResult->execResult();
    Address address(++addressCount);
    bytes out2 = abi.abiIn("", address);
    BOOST_CHECK(out1 == out2);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_KVTablePrecompiled
