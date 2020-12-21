/*
 *  Copyright (C) 2020 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file JonDataConvertUtility.cpp
 * Tests for functions in JonDataConvertUtility
 */

#include <libutilities/Exceptions.h>
#include <libutilities/JsonDataConvertUtility.h>
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

    BOOST_CHECK(toJonString(a) == "0xbaadf00ddeadbeef");
    BOOST_CHECK(toJonString(c) == "0x913ffc283");
    BOOST_CHECK(toJonString(d) == "0xff00efbc");
}

BOOST_AUTO_TEST_CASE(test_jsToBytes)
{
    bytes a = {0xff, 0xaa, 0xbb, 0xcc};
    bytes b = {0x03, 0x89, 0x90, 0x23, 0x42, 0x43};
    BOOST_CHECK(a == jonStringToBytes("0xffaabbcc"));
    BOOST_CHECK(b == jonStringToBytes("38990234243"));
    BOOST_CHECK(bytes() == jonStringToBytes(""));
    BOOST_CHECK_THROW(jonStringToBytes("Invalid hex chars"), invalid_argument);
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
    BOOST_CHECK(43832124 == jsonStringToInt("43832124"));
    BOOST_CHECK(1342356623 == jsonStringToInt("0x5002bc8f"));
    BOOST_CHECK(3483942 == jsonStringToInt("015224446"));
    BOOST_CHECK_THROW(jsonStringToInt("NotAHexadecimalOrDecimal"), std::exception);

    BOOST_CHECK(u256("983298932490823474234") == jsonStringToInt<32>("983298932490823474234"));
    BOOST_CHECK(u256("983298932490823474234") == jsonStringToInt<32>("0x354e03915c00571c3a"));
    BOOST_CHECK_THROW(jsonStringToInt<32>("NotAHexadecimalOrDecimal"), std::exception);
    BOOST_CHECK_THROW(jsonStringToInt<16>("NotAHexadecimalOrDecimal"), bcos::BadCast);
}

BOOST_AUTO_TEST_CASE(test_jsToU256)
{
    BOOST_CHECK(u256("983298932490823474234") == jonStringToU256("983298932490823474234"));
    BOOST_CHECK_THROW(jonStringToU256("NotAHexadecimalOrDecimal"), bcos::BadCast);
}

BOOST_AUTO_TEST_SUITE_END()
