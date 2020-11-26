/*
    This file is part of cpp-ethereum.

    cpp-ethereum is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    cpp-ethereum is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file CommonJS.cpp
 * @author Lefteris Karapetsas <lefteris@refu.co>
 * @date 2015
 * Tests for functions in CommonJS.h
 */

#include <libutilities/CommonJS.h>
#include <libutilities/Exceptions.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace std;
using namespace test;

BOOST_FIXTURE_TEST_SUITE(CommonJSTests, TestOutputHelperFixture)

BOOST_AUTO_TEST_CASE(test_toJS)
{
    h64 a("0xbaadf00ddeadbeef");
    uint64_t c = 38990234243;
    bytes d = {0xff, 0x0, 0xef, 0xbc};

    BOOST_CHECK(toJS(a) == "0xbaadf00ddeadbeef");
    BOOST_CHECK(toJS(c) == "0x913ffc283");
    BOOST_CHECK(toJS(d) == "0xff00efbc");
}

BOOST_AUTO_TEST_CASE(test_jsToBytes)
{
    bytes a = {0xff, 0xaa, 0xbb, 0xcc};
    bytes b = {0x03, 0x89, 0x90, 0x23, 0x42, 0x43};
    BOOST_CHECK(a == jsToBytes("0xffaabbcc"));
    BOOST_CHECK(b == jsToBytes("38990234243"));
    BOOST_CHECK(bytes() == jsToBytes(""));
    BOOST_CHECK(bytes() == jsToBytes("Invalid hex chars"));
}

BOOST_AUTO_TEST_CASE(test_jsToFixed)
{
    h256 a("0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    BOOST_CHECK(
        a == jsToFixed<32>("0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    h256 b("0x000000000000000000000000000000000000000000000000000000740c54b42f");
    BOOST_CHECK(b == jsToFixed<32>("498423084079"));
    BOOST_CHECK(h256() == jsToFixed<32>("NotAHexadecimalOrDecimal"));
}

BOOST_AUTO_TEST_CASE(test_jsToInt)
{
    BOOST_CHECK(43832124 == jsToInt("43832124"));
    BOOST_CHECK(1342356623 == jsToInt("0x5002bc8f"));
    BOOST_CHECK(3483942 == jsToInt("015224446"));
    BOOST_CHECK_THROW(jsToInt("NotAHexadecimalOrDecimal"), std::exception);

    BOOST_CHECK(u256("983298932490823474234") == jsToInt<32>("983298932490823474234"));
    BOOST_CHECK(u256("983298932490823474234") == jsToInt<32>("0x354e03915c00571c3a"));
    BOOST_CHECK_THROW(jsToInt<32>("NotAHexadecimalOrDecimal"), std::exception);
    BOOST_CHECK_THROW(jsToInt<16>("NotAHexadecimalOrDecimal"), bcos::BadCast);
}

BOOST_AUTO_TEST_CASE(test_jsToU256)
{
    BOOST_CHECK(u256("983298932490823474234") == jsToU256("983298932490823474234"));
    BOOST_CHECK_THROW(jsToU256("NotAHexadecimalOrDecimal"), bcos::BadCast);
}

BOOST_AUTO_TEST_SUITE_END()
