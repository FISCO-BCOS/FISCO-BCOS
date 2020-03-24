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

#include "MemoryStorage.h"
#include <json/json.h>
#include <libblockverifier/ExecutiveContext.h>
#include <libdevcrypto/Common.h>
#include <libethcore/ABI.h>
#include <libprecompiled/TableFactoryPrecompiled.h>
#include <libstorage/MemoryTableFactory.h>
#include <libstorage/Storage.h>
#include <libstorage/Table.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::storage;

namespace test_TableFactoryPrecompiled
{
class MockMemoryTableFactory : public dev::storage::MemoryTableFactory
{
public:
    virtual ~MockMemoryTableFactory(){};
};

class MockPrecompiledEngine : public dev::blockverifier::ExecutiveContext
{
public:
    virtual ~MockPrecompiledEngine() {}
};

struct TableFactoryPrecompiledFixture
{
    TableFactoryPrecompiledFixture()
    {
        context = std::make_shared<MockPrecompiledEngine>();
        BlockInfo blockInfo{h256(0x001), 1, h256(0x001)};
        context->setBlockInfo(blockInfo);
        tableFactoryPrecompiled = std::make_shared<dev::precompiled::TableFactoryPrecompiled>();
        memStorage = std::make_shared<MemoryStorage>();
        auto mockMemoryTableFactory = std::make_shared<MockMemoryTableFactory>();
        mockMemoryTableFactory->setStateStorage(memStorage);
        tableFactoryPrecompiled->setMemoryTableFactory(mockMemoryTableFactory);
    }

    ~TableFactoryPrecompiledFixture() {}
    Storage::Ptr memStorage;
    dev::precompiled::TableFactoryPrecompiled::Ptr tableFactoryPrecompiled;
    ExecutiveContext::Ptr context;
    int addressCount = 0x10000;
};

BOOST_FIXTURE_TEST_SUITE(TableFactoryPrecompiled, TableFactoryPrecompiledFixture)

BOOST_AUTO_TEST_CASE(toString)
{
    BOOST_TEST(tableFactoryPrecompiled->toString() == "TableFactory");
}

BOOST_AUTO_TEST_CASE(call_afterBlock)
{
    // createTable
    dev::eth::ContractABI abi;
    bytes param = abi.abiIn("createTable(string,string,string)", std::string("t_test"),
        std::string("id"), std::string("item_name,item_id"));
    bytes out = tableFactoryPrecompiled->call(context, bytesConstRef(&param));
    s256 errCode;
    abi.abiOut(&out, errCode);
    BOOST_TEST(errCode == 0);

    // createTable exist
    param = abi.abiIn("createTable(string,string,string)", std::string("t_test"), std::string("id"),
        std::string("item_name,item_id"));
    out = tableFactoryPrecompiled->call(context, bytesConstRef(&param));
    abi.abiOut(&out, errCode);
    if (g_BCOSConfig.version() > RC2_VERSION)
    {
        BOOST_TEST(errCode == CODE_TABLE_NAME_ALREADY_EXIST);
    }
    else
    {
        BOOST_TEST(errCode == -CODE_TABLE_NAME_ALREADY_EXIST);
    }

    // openTable not exist
    param.clear();
    out.clear();
    param = abi.abiIn("openTable(string)", std::string("t_poor"));
    out = tableFactoryPrecompiled->call(context, bytesConstRef(&param));
    Address addressOut;
    addressOut.clear();
    abi.abiOut(&out, addressOut);
    BOOST_TEST(addressOut == Address(0x0));

    param.clear();
    out.clear();
    param = abi.abiIn("openTable(string)", std::string("t_test"));
    out = tableFactoryPrecompiled->call(context, bytesConstRef(&param));
    addressOut.clear();
    abi.abiOut(&out, addressOut);
    BOOST_TEST(addressOut == Address(++addressCount));
}

BOOST_AUTO_TEST_CASE(hash)
{
    h256 h = tableFactoryPrecompiled->hash();
    BOOST_TEST(h == h256());
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_TableFactoryPrecompiled
