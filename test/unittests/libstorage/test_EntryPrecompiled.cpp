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
#include <libblockverifier/ExecutiveContext.h>
#include <libethcore/ABI.h>
#include <libprecompiled/EntryPrecompiled.h>
#include <libstorage/Table.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::storage;
using namespace dev::blockverifier;
using namespace dev::eth;

namespace test_precompiled
{
struct EntryPrecompiledFixture
{
    EntryPrecompiledFixture()
    {
        entry = std::make_shared<Entry>();
        precompiledContext = std::make_shared<dev::blockverifier::ExecutiveContext>();
        entryPrecompiled = std::make_shared<dev::blockverifier::EntryPrecompiled>();

        entryPrecompiled->setEntry(entry);
    }
    ~EntryPrecompiledFixture() {}

    dev::storage::Entry::Ptr entry;
    dev::blockverifier::ExecutiveContext::Ptr precompiledContext;
    dev::blockverifier::EntryPrecompiled::Ptr entryPrecompiled;
};

BOOST_FIXTURE_TEST_SUITE(EntryPrecompiled, EntryPrecompiledFixture)

BOOST_AUTO_TEST_CASE(testBeforeAndAfterBlock)
{
    BOOST_TEST_TRUE(entryPrecompiled->toString() == "Entry");
}

BOOST_AUTO_TEST_CASE(testEntry)
{
    entry->setField("key", "value");
    entryPrecompiled->setEntry(entry);
    BOOST_TEST_TRUE(entryPrecompiled->getEntry() == entry);
}

BOOST_AUTO_TEST_CASE(testGetInt)
{
    entry->setField("keyInt", "100");
    ContractABI abi;

    bytes bint = abi.abiIn("getInt(string)", std::string("keyInt"));
    bytes out = entryPrecompiled->call(precompiledContext, bytesConstRef(&bint));
    u256 num;
    abi.abiOut(bytesConstRef(&out), num);
    BOOST_TEST_TRUE(num == u256(100));
}

BOOST_AUTO_TEST_CASE(testGetAddress)
{
    ContractABI abi;
    entry->setField("keyAddress", "1000");
    bytes gstr = abi.abiIn("getAddress(string)", std::string("keyAddress"));
    bytes out = entryPrecompiled->call(precompiledContext, bytesConstRef(&gstr));
    Address address;
    abi.abiOut(bytesConstRef(&out), address);
    BOOST_TEST_TRUE(address == Address("1000"));
    Address address2("0x123456789");
    bytes data = abi.abiIn("set(string,address)", std::string("keyAddress"), address2);
    entryPrecompiled->call(precompiledContext, bytesConstRef(&data));
    data = abi.abiIn("getAddress(string)", std::string("keyAddress"));
    out = entryPrecompiled->call(precompiledContext, bytesConstRef(&data));
    Address address3;
    abi.abiOut(bytesConstRef(&out), address3);
    BOOST_TEST_TRUE(address3 == address2);
}

BOOST_AUTO_TEST_CASE(testSetInt)
{
    ContractABI abi;
    bytes sstr = abi.abiIn("set(string,int256)", std::string("keyInt"), u256(200));
    entryPrecompiled->call(precompiledContext, bytesConstRef(&sstr));
    BOOST_TEST_TRUE(entry->getField("keyInt") == boost::lexical_cast<std::string>(200));
    sstr = abi.abiIn("set(string,int256)", std::string("keyInt"), s256(-1));
    entryPrecompiled->call(precompiledContext, bytesConstRef(&sstr));
    bytes bint = abi.abiIn("getInt(string)", std::string("keyInt"));
    bytes out = entryPrecompiled->call(precompiledContext, bytesConstRef(&bint));
    s256 num;
    abi.abiOut(bytesConstRef(&out), num);
    BOOST_TEST_TRUE(num == -1);
}

BOOST_AUTO_TEST_CASE(testSetString)
{
    ContractABI abi;
    bytes data = abi.abiIn("set(string,string)", std::string("keyString"), std::string("you"));
    entryPrecompiled->call(precompiledContext, bytesConstRef(&data));
    BOOST_TEST_TRUE(entry->getField("keyString") == "you");
    std::string longStr("123456789,123456789,123456789,123456789,123456789");
    data = abi.abiIn("set(string,string)", std::string("keyString"), longStr);
    entryPrecompiled->call(precompiledContext, bytesConstRef(&data));
    BOOST_TEST_TRUE(entry->getField("keyString") == longStr);
    data = abi.abiIn("getString(string)", std::string("keyString"));
    bytes out = entryPrecompiled->call(precompiledContext, bytesConstRef(&data));
    std::string ret;
    abi.abiOut(bytesConstRef(&out), ret);
    BOOST_TEST_TRUE(ret == longStr);
}

BOOST_AUTO_TEST_CASE(testGetBytes64_0)
{
    entry->setField("keyString", "1000");
    ContractABI abi;
    bytes sstr = abi.abiIn("getBytes64(string)", std::string("keyString"));
    bytes out = entryPrecompiled->call(precompiledContext, bytesConstRef(&sstr));

    string32 ret0;
    string32 ret1;

    string32 retOut0;
    string32 retOut1;

    auto ok = abi.abiOut(bytesConstRef(&out), retOut0, retOut1);

    std::string s = "1000";

    for (unsigned i = 0; i < 32; ++i)
        ret0[i] = (i < s.size() ? s[i] : 0);

    for (unsigned i = 32; i < 64; ++i)
        ret1[i - 32] = (i < s.size() ? s[i] : 0);

    BOOST_CHECK(ok == true);
    BOOST_CHECK(ret0 == retOut0);
    BOOST_CHECK(ret1 == retOut1);
}

BOOST_AUTO_TEST_CASE(testGetBytes64_1)
{
    std::string key = "KeyString";
    std::string value = "adfasfjals;kdfjadlfasdfjadslkfhaskdlfjaislkdjifakldjfkal;fjakdl";
    entry->setField(key, value);
    ContractABI abi;
    bytes sstr = abi.abiIn("getBytes64(string)", key);
    bytes out = entryPrecompiled->call(precompiledContext, bytesConstRef(&sstr));

    string32 ret0;
    string32 ret1;

    string32 retOut0;
    string32 retOut1;

    auto ok = abi.abiOut(bytesConstRef(&out), retOut0, retOut1);

    for (unsigned i = 0; i < 32; ++i)
        ret0[i] = (i < value.size() ? value[i] : 0);

    for (unsigned i = 32; i < 64; ++i)
        ret1[i - 32] = (i < value.size() ? value[i] : 0);

    BOOST_CHECK(ok == true);

    BOOST_CHECK(ret0 == retOut0);
    BOOST_CHECK(ret1 == retOut1);
}

BOOST_AUTO_TEST_CASE(testGetBytes32)
{
    entry->setField("keyString", "1000");
    ContractABI abi;
    bytes sstr = abi.abiIn("getBytes32(string)", std::string("keyString"));
    bytes out = entryPrecompiled->call(precompiledContext, bytesConstRef(&sstr));
    string32 retout;
    abi.abiOut(bytesConstRef(&out), retout);
    std::string s = "1000";
    string32 ret;
    for (unsigned i = 0; i < retout.size(); ++i)
        ret[i] = i < s.size() ? s[i] : 0;
    BOOST_TEST_TRUE(retout == ret);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_precompiled
