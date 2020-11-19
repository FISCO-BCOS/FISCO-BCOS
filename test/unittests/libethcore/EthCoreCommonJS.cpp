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
 * @brief: unit test of CommonJS of libethcore
 *
 * @file CommonJS.cpp
 * @author: yujiechen
 * @date 2018-08-30
 */

#include <libdevcrypto/Common.h>
#include <libethcore/CommonJS.h>
#include <libethcore/Exceptions.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <string>

using namespace bcos;
namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(EthcoreCommonJSTest, TestOutputHelperFixture)
BOOST_AUTO_TEST_CASE(testJsToPublic)
{
    /// test jsToPublic
    std::string public_str =
        "0x519775d37a45a9d2c8a072e1701792f91"
        "da54a008de970e5c75ba0e4a198f76a";
    Public pub = jsToPublic(public_str);
    BOOST_CHECK(pub == h512(public_str));

    KeyPair key_pair = KeyPair::create();
    pub = jsToPublic(toHex(key_pair.pub()));
    BOOST_CHECK(pub != key_pair.pub());
    pub = jsToPublic(toHexPrefixed(key_pair.pub()));
    BOOST_CHECK(pub == key_pair.pub());
    /// test jsToSecret
    std::string sec_str =
        "0xbcec428d5205abe0f0cc8a734083908d9eb8"
        "563e31f943d760786edf42ad67dd";
    Secret sec = jsToSecret(sec_str);
    BOOST_CHECK(sec);

    sec_str = "1234324";
    sec = jsToSecret(sec_str);
    BOOST_CHECK(sec);

    /// test jsToAddress
    std::string addr = toHexPrefixed(toAddress(key_pair.pub()));
    BOOST_CHECK(jsToAddress(addr) == toAddress(key_pair.pub()));
    // exception test
    std::string invalid_addr = "0x1234";
    BOOST_CHECK_THROW(jsToAddress(invalid_addr), bcos::eth::InvalidAddress);
    invalid_addr = "adb234ef";
    BOOST_CHECK_THROW(jsToAddress(invalid_addr), bcos::eth::InvalidAddress);
}

BOOST_AUTO_TEST_CASE(testToBlockNumber)
{
    BOOST_CHECK(bcos::eth::jsToBlockNumber("0x14") == (bcos::eth::BlockNumber)(20));
    BOOST_CHECK(bcos::eth::jsToBlockNumber("100") == (bcos::eth::BlockNumber)(100));
    BOOST_CHECK_THROW(bcos::eth::jsToBlockNumber("adk"), std::exception);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
