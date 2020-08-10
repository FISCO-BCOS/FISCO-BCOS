/**
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
 *
 * @brief Construct a new boost auto test case object for MemoryDB CRUD
 *
 * @file MemoryDB.cpp
 * @author: tabsu
 * @date 2018-08-24
 */

#include "libdevcrypto/CryptoInterface.h"
#include <libmptstate/MemoryDB.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace std;

namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(MemoryDB, TestOutputHelperFixture)

BOOST_AUTO_TEST_CASE(testMemoryDBKeyExist)
{
    dev::MemoryDB memoryDB;
    h256 key("0x2979B90fF15080A5F956bE0dD2dF1A345b120183");
    BOOST_CHECK(memoryDB.exists(key) == false);
}

BOOST_AUTO_TEST_CASE(testMemoryDBKeyValue)
{
    dev::MemoryDB memoryDB;
    dev::EnforceRefs enforceRefs(memoryDB, true);

    h256 key = crypto::Hash("0x2979B90fF15080A5F956bE0dD2dF1A345b120183");
    h256 keyNotExist = crypto::Hash("0x2979B90fF15080A5F956bE0dD2dF1A345b120184");
    h256 key2 = crypto::Hash("0x2979B90fF15080A5F956bE0dD2dF1A345b120185");

    bytesConstRef value("helloworld");
    memoryDB.insert(key, value);
    BOOST_CHECK(memoryDB.exists(key) == true);

    BOOST_CHECK(memoryDB.lookup(keyNotExist) == "");

    memoryDB.insert(key2, value);
    auto keyMap = memoryDB.get();
    BOOST_CHECK(keyMap.size() == 2);

    auto keys = memoryDB.keys();
    BOOST_CHECK(keys.size() == 2);

    bool ok = memoryDB.kill(key);
    BOOST_CHECK(ok == true);

    ok = memoryDB.kill(keyNotExist);
    BOOST_CHECK(ok == false);

    BOOST_CHECK(memoryDB.exists(key) == false);
}

BOOST_AUTO_TEST_CASE(testMemoryDBAux)
{
    dev::MemoryDB memoryDB;
    dev::EnforceRefs enforceRefs(memoryDB, true);

    h256 key = crypto::Hash("aux_0x2979B90fF15080A5F956bE0dD2dF1A345b120183");

    string valueStr = "helloworld";
    bytesConstRef value(valueStr);
    memoryDB.insertAux(key, value);

    auto valueBytes = memoryDB.lookupAux(key);
    valueStr = dev::asString(valueBytes);
    BOOST_CHECK(valueStr == "helloworld");

    memoryDB.removeAux(key);
    BOOST_CHECK(memoryDB.lookupAux(key) == dev::bytes());

    dev::EnforceRefs enforceRefsFalse(memoryDB, false);
    memoryDB.purge();
    BOOST_CHECK(memoryDB.lookupAux(key) == dev::bytes());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
