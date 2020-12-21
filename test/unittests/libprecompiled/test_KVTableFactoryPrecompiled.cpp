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

#include "../libstorage/MemoryStorage.h"
#include "../libstorage/MemoryStorage2.h"
#include "libprecompiled/TableFactoryPrecompiled.h"
#include "libstorage/MemoryTableFactory.h"
#include <json/json.h>
#include <libblockverifier/ExecutiveContext.h>
#include <libdevcrypto/Common.h>
#include <libprecompiled/KVTableFactoryPrecompiled.h>
#include <libprotocol/ContractABICodec.h>
#include <libstorage/Storage.h>
#include <libstorage/Table.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::blockverifier;
using namespace bcos::precompiled;
using namespace bcos::storage;

namespace test_KVTableFactoryPrecompiled
{
class MockMemoryTableFactory : public bcos::storage::MemoryTableFactory
{
public:
    virtual ~MockMemoryTableFactory(){};
};

class MockPrecompiledEngine : public bcos::blockverifier::ExecutiveContext
{
public:
    virtual ~MockPrecompiledEngine() {}
};

struct TableFactoryPrecompiledFixture
{
    TableFactoryPrecompiledFixture()
    {
        context = std::make_shared<MockPrecompiledEngine>();
        memStorage = std::make_shared<MemoryStorage2>();
        MemoryTableFactory::Ptr tableFactory = std::make_shared<MemoryTableFactory>();
        tableFactory->setStateStorage(memStorage);
        context->setMemoryTableFactory(tableFactory);
        auto tfPrecompiled = std::make_shared<bcos::precompiled::TableFactoryPrecompiled>();
        tfPrecompiled->setMemoryTableFactory(tableFactory);
        context->setAddress2Precompiled(Address(0x1001), tfPrecompiled);
        BlockInfo blockInfo{h256(0x001), 1, h256(0x001)};
        context->setBlockInfo(blockInfo);
        tableFactoryPrecompiled = std::make_shared<precompiled::KVTableFactoryPrecompiled>();
        auto mockMemoryTableFactory = std::make_shared<MockMemoryTableFactory>();
        mockMemoryTableFactory->setStateStorage(memStorage);
        tableFactoryPrecompiled->setMemoryTableFactory(mockMemoryTableFactory);

        auto precompiledGasFactory = std::make_shared<bcos::precompiled::PrecompiledGasFactory>(0);
        auto precompiledExecResultFactory = std::make_shared<PrecompiledExecResultFactory>();
        precompiledExecResultFactory->setPrecompiledGasFactory(precompiledGasFactory);
        tableFactoryPrecompiled->setPrecompiledExecResultFactory(precompiledExecResultFactory);
    }

    ~TableFactoryPrecompiledFixture() {}
    Storage::Ptr memStorage;
    precompiled::KVTableFactoryPrecompiled::Ptr tableFactoryPrecompiled;
    ExecutiveContext::Ptr context;
    int addressCount = 0x10000;
};

BOOST_FIXTURE_TEST_SUITE(KVTableFactoryPrecompiled, TableFactoryPrecompiledFixture)

BOOST_AUTO_TEST_CASE(toString)
{
    BOOST_TEST(tableFactoryPrecompiled->toString() == "KVTableFactory");
}

BOOST_AUTO_TEST_CASE(call_afterBlock)
{
    // createTable
    bcos::protocol::ContractABICodec abi;
    bytes param = abi.abiIn("createTable(string,string,string)", std::string("t_test"),
        std::string("id"), std::string("item_name,item_id"));
    auto callResult = tableFactoryPrecompiled->call(context, bytesConstRef(&param));
    bytes out = callResult->execResult();
    s256 errCode;
    abi.abiOut(&out, errCode);
    BOOST_TEST(errCode == 0);

    // createTable exist
    param = abi.abiIn("createTable(string,string,string)", std::string("t_test"), std::string("id"),
        std::string("item_name,item_id"));
    callResult = tableFactoryPrecompiled->call(context, bytesConstRef(&param));
    out = callResult->execResult();
    abi.abiOut(&out, errCode);

    BOOST_TEST(errCode == CODE_TABLE_NAME_ALREADY_EXIST);

    // openTable not exist
    param.clear();
    out.clear();
    param = abi.abiIn("openTable(string)", std::string("t_poor"));
    BOOST_CHECK_THROW(
        tableFactoryPrecompiled->call(context, bytesConstRef(&param)), PrecompiledException);

    param.clear();
    out.clear();
    param = abi.abiIn("openTable(string)", std::string("t_test"));
    callResult = tableFactoryPrecompiled->call(context, bytesConstRef(&param));
    out = callResult->execResult();
    Address addressOut;
    abi.abiOut(&out, addressOut);
    BOOST_TEST(addressOut == Address(++addressCount));
}

BOOST_AUTO_TEST_CASE(hash)
{
    h256 h = tableFactoryPrecompiled->hash();
    BOOST_TEST(h == h256());
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_KVTableFactoryPrecompiled
