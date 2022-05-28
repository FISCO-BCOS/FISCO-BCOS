/**
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief Unit tests for the FixedBytes
 * @file FixedBytesTest.cpp
 * @author: chaychen
 */

#include "bcos-utilities/JsonDataConvertUtility.h"
#include "bcos-utilities/DataConvertUtility.h"
#include "bcos-utilities/FixedBytes.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <boost/test/unit_test.hpp>
#include <string>

using namespace bcos;
namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(FixedBytes, TestPromptFixture)
BOOST_AUTO_TEST_CASE(testLeft160)
{
    const std::string str = "0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e";
    h256 h = jonStringToFixedBytes<32>(str);
    BOOST_CHECK("0x067150c07dab4facb7160e075548007e067150c0" == toJonString(left160(h)));
}

BOOST_AUTO_TEST_CASE(testRight160)
{
    const std::string str = "0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e";
    h256 h = jonStringToFixedBytes<32>(str);
    BOOST_CHECK("0x5548007e067150c07dab4facb7160e075548007e" == toJonString(right160(h)));
    // test u256
    h256 h256Data(
        *fromHexString("12b5155eda010a5b7ae26a4a268e466a4b8d31547ad875fce9ab298c639a1b2f"));
    // trans h256Data to u256
    u256 value(h256Data);
    // trans value to h256 again
    h256 convertedH256Data = value;
    std::cout << "### value: " << value << ", h256Data:" << *toHexString(h256Data)
              << "convertedH256Data" << *toHexString(convertedH256Data) << std::endl;
    BOOST_CHECK(convertedH256Data == h256Data);
}

BOOST_AUTO_TEST_CASE(testToJonString)
{
    h64 a("0xbaadf00ddeadbeef");
    uint64_t c = 38990234243;
    bytes d = {0xff, 0x0, 0xef, 0xbc};

    BOOST_CHECK(toJonString(a) == "0xbaadf00ddeadbeef");
    BOOST_CHECK(toJonString(c) == "0x913ffc283");
    BOOST_CHECK(toJonString(d) == "0xff00efbc");
}

BOOST_AUTO_TEST_CASE(testJonStringToBytes)
{
    bytes a = {0xff, 0xaa, 0xbb, 0xcc};
    bytes b = {0x03, 0x89, 0x90, 0x23, 0x42, 0x43};
    BOOST_CHECK(a == jonStringToBytes("0xffaabbcc"));
    BOOST_CHECK(b == jonStringToBytes("38990234243"));
    BOOST_CHECK(bytes() == jonStringToBytes(""));
    BOOST_CHECK_THROW(jonStringToBytes("Invalid hex chars"), bcos::BadHexCharacter);
}

BOOST_AUTO_TEST_CASE(testJonStringToFixedBytes)
{
    h256 a("0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    BOOST_CHECK(a == jonStringToFixedBytes<32>(
                         "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    h256 b("0x000000000000000000000000000000000000000000000000000000740c54b42f");
    BOOST_CHECK(b == jonStringToFixedBytes<32>("498423084079"));
    BOOST_CHECK(h256() == jonStringToFixedBytes<32>("NotAHexadecimalOrDecimal"));
}

BOOST_AUTO_TEST_CASE(testJsonStringToInt)
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

BOOST_AUTO_TEST_CASE(testJonStringToU256)
{
    BOOST_CHECK(u256("983298932490823474234") == jonStringToU256("983298932490823474234"));
    BOOST_CHECK_THROW(jonStringToU256("NotAHexadecimalOrDecimal"), bcos::BadCast);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
