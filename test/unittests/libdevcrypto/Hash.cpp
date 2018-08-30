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
 * @brief Unit tests for the Hash
 * @file Hash.cpp
 * @author: chaychen
 * @date 2018
 */

#include <libdevcore/Assertions.h>
#include <libdevcore/CommonJS.h>
#include <libdevcrypto/Hash.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <string>

using namespace dev;
namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(Hash, TestOutputHelperFixture)
BOOST_AUTO_TEST_CASE(testSha256)
{
    const std::string plainText = "123456ABC+";
    const std::string cipherText =
        "0x2218be4abd327ca929399fc73314f3d0cdd03cfc98927fabe7cd40f2059efd01";
    bytes bs;
    for (size_t i = 0; i < plainText.length(); i++)
    {
        bs.push_back((byte)plainText[i]);
    }
    bytesConstRef bsConst(&bs);
    BOOST_CHECK(toJS(sha256(bsConst)) == cipherText);
}

BOOST_AUTO_TEST_CASE(testRipemd160)
{
    const std::string plainText = "123456ABC+";
    const std::string cipherText = "0x74204bedd818292adc1127f9bb24bafd75468b62";
    bytes bs;
    for (size_t i = 0; i < plainText.length(); i++)
    {
        bs.push_back((byte)plainText[i]);
    }
    bytesConstRef bsConst(&bs);
    BOOST_CHECK(toJS(ripemd160(bsConst)) == cipherText);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
