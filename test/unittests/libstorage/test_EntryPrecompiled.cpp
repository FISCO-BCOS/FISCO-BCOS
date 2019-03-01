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
#include <libdevcore/easylog.h>
#include <libethcore/ABI.h>
#include <libstorage/EntryPrecompiled.h>
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

    bytes bint = abi.abiIn("getInt(string)", "keyInt");
    bytes out = entryPrecompiled->call(precompiledContext, bytesConstRef(&bint));
    u256 num;
    abi.abiOut(bytesConstRef(&out), num);
    BOOST_TEST_TRUE(num == u256(100));
}

BOOST_AUTO_TEST_CASE(testGetAddress)
{
    ContractABI abi;
    entry->setField("keyAddress", "1000");
    bytes gstr = abi.abiIn("getAddress(string)", "keyAddress");
    bytes out = entryPrecompiled->call(precompiledContext, bytesConstRef(&gstr));
    Address address;
    abi.abiOut(bytesConstRef(&out), address);
    BOOST_TEST_TRUE(address == Address("1000"));
}

BOOST_AUTO_TEST_CASE(testSetInt)
{
    ContractABI abi;
    bytes sstr = abi.abiIn("set(string,int256)", "keyInt", u256(200));
    entryPrecompiled->call(precompiledContext, bytesConstRef(&sstr));
    BOOST_TEST_TRUE(entry->getField("keyInt") == boost::lexical_cast<std::string>(200));
}

BOOST_AUTO_TEST_CASE(testSetString)
{
    ContractABI abi;
    bytes sstr = abi.abiIn("set(string,string)", "keyString", "you");
    entryPrecompiled->call(precompiledContext, bytesConstRef(&sstr));
    BOOST_TEST_TRUE(entry->getField("keyString") == "you");
}

BOOST_AUTO_TEST_CASE(testGetBytes64)
{
    entry->setField("keyString", "1000");
    ContractABI abi;
    bytes sstr = abi.abiIn("getBytes64(string)", "keyString");
    bytes out = entryPrecompiled->call(precompiledContext, bytesConstRef(&sstr));
    string64 retout;
    abi.abiOut(bytesConstRef(&out), retout);
    std::string s = "1000";
    string64 ret;
    for (unsigned i = 0; i < retout.size(); ++i)
        ret[i] = i < s.size() ? s[i] : 0;
    BOOST_TEST_TRUE(retout == ret);
}

BOOST_AUTO_TEST_CASE(testGetBytes32)
{
    entry->setField("keyString", "1000");
    ContractABI abi;
    bytes sstr = abi.abiIn("getBytes32(string)", "keyString");
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
