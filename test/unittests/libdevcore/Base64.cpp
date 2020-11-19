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
 * @brief Unit tests for the Base64
 * @file Base64.cpp
 * @author: chaychen
 * @date 2018
 */
#include <iostream>

#include <libutilities/Assertions.h>
#include <libutilities/Base64.h>
#include <libutilities/CommonJS.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <string>

using namespace bcos;
namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(Base64, TestOutputHelperFixture)
BOOST_AUTO_TEST_CASE(testFromBase64)
{
    const std::string encodeStr = "MTIzNEFCY2Q=";
    bytes decodeStr = fromBase64(encodeStr);
    std::string oriStr;
    for (size_t i = 0; i < decodeStr.size(); i++)
    {
        oriStr += char(decodeStr[i]);
    }
    BOOST_CHECK(oriStr == "1234ABcd");
}

BOOST_AUTO_TEST_CASE(testToBase64)
{
    const std::string decodeStr = "1234ABcd";
    bytes bs;
    for (size_t i = 0; i < decodeStr.length(); i++)
    {
        bs.push_back((byte)decodeStr[i]);
    }
    bytesConstRef bsConst(&bs);
    BOOST_CHECK(toBase64(bsConst) == "MTIzNEFCY2Q=");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
