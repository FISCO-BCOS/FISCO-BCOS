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
 * @brief: unit test of CommonJS of libprotocol
 *
 * @file CommonJS.cpp
 * @author: yujiechen
 * @date 2018-08-30
 */

#include <libdevcrypto/Common.h>
#include <libprotocol/CommonJS.h>
#include <libprotocol/Exceptions.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <string>

using namespace bcos;
namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(EthcoreCommonJSTest, TestOutputHelperFixture)
BOOST_AUTO_TEST_CASE(testJsToAddress)
{
    KeyPair key_pair = KeyPair::create();
    /// test jsToAddress
    std::string addr = toHexStringWithPrefix(toAddress(key_pair.pub()));
    BOOST_CHECK(jsToAddress(addr) == toAddress(key_pair.pub()));
    // exception test
    std::string invalid_addr = "0x1234";
    BOOST_CHECK_THROW(jsToAddress(invalid_addr), bcos::protocol::InvalidAddress);
    invalid_addr = "adb234ef";
    BOOST_CHECK_THROW(jsToAddress(invalid_addr), bcos::protocol::InvalidAddress);
}

BOOST_AUTO_TEST_CASE(testToBlockNumber)
{
    BOOST_CHECK(bcos::protocol::jsToBlockNumber("0x14") == (bcos::protocol::BlockNumber)(20));
    BOOST_CHECK(bcos::protocol::jsToBlockNumber("100") == (bcos::protocol::BlockNumber)(100));
    BOOST_CHECK_THROW(bcos::protocol::jsToBlockNumber("adk"), std::exception);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
