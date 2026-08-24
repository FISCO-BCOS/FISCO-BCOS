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
 * @brief unit test for DataConvertUtility.*(mainly type covert)
 *
 * @file DataConvertUtility.cpp
 * @author: yujiechen
 */
#include "bcos-utilities/DataConvertUtility.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <boost/test/unit_test.hpp>
#include <cstdlib>
#include <ctime>
#include <iostream>

using namespace bcos;
namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(CommonDataTests, TestPromptFixture)

BOOST_AUTO_TEST_CASE(testHex)
{
    // toHex && fromHexString
    unsigned int round = 10;
    unsigned int size = 10;
    bytes hexVec(size, 0);
    std::string hexStr;
    std::string hexStrWithPrefix;
    std::srand(std::time(nullptr));
    for (unsigned int i = 0; i < round; i++)
    {
        for (unsigned int j = 0; j < size; j++)
        {
            hexVec[j] = std::rand() % 16;
        }
        hexStr = toHex(hexVec);
        hexStrWithPrefix = toHex(hexVec, "0x");
        BOOST_CHECK(fromHex(hexStr) == hexVec);
        BOOST_CHECK(fromHex(hexStrWithPrefix) == hexVec);
    }
    // fromHexString Exception
    BOOST_CHECK_THROW(fromHex("0934xyza"), BadHexCharacter);
    BOOST_CHECK(fromHex("093") == fromHex("0093"));
    BOOST_CHECK(fromHex("0xabc") == fromHex("0abc"));
    BOOST_CHECK(isHexString("0934xyz") == false);

    BOOST_CHECK(isHexString("0x000abc") == true);

    BOOST_CHECK(isHexString("000123123") == true);
}

/// Strict hex-quantity parsing (fromQuantity / safeFromQuantity): optional 0x prefix,
/// hex digits only, no leading sign, no trailing garbage, must fit in uint64.
BOOST_AUTO_TEST_CASE(testQuantity)
{
    // Valid forms.
    BOOST_CHECK_EQUAL(fromQuantity("0x10"), 0x10u);
    BOOST_CHECK_EQUAL(fromQuantity("0Xff"), 0xffu);
    BOOST_CHECK_EQUAL(fromQuantity("10"), 0x10u);  // bare hex, not decimal
    BOOST_CHECK_EQUAL(fromQuantity("0x7fffffffffffffff"),
        static_cast<uint64_t>(0x7fffffffffffffffULL));
    BOOST_CHECK_EQUAL(fromQuantity("ffffffffffffffff"), UINT64_MAX);

    // Strict: trailing garbage / signs / empty are rejected (previously stoull silently
    // truncated at the first non-hex character and accepted a leading '-').
    BOOST_CHECK_THROW(fromQuantity("chain0"), std::invalid_argument);  // 'h' is not hex
    BOOST_CHECK_THROW(fromQuantity("0x10zz"), std::invalid_argument);
    BOOST_CHECK_THROW(fromQuantity("-1"), std::invalid_argument);
    BOOST_CHECK_THROW(fromQuantity("+1"), std::invalid_argument);
    BOOST_CHECK_THROW(fromQuantity("0x"), std::invalid_argument);
    BOOST_CHECK_THROW(fromQuantity(""), std::invalid_argument);
    BOOST_CHECK_THROW(fromQuantity("0x0x1a"), std::invalid_argument);
    BOOST_CHECK_THROW(fromQuantity("10000000000000000"), std::invalid_argument);  // > uint64

    // Non-throwing companion mirrors the above as nullopt.
    BOOST_CHECK(safeFromQuantity("0x10") == 0x10u);
    BOOST_CHECK(safeFromQuantity("10") == 0x10u);
    BOOST_CHECK(!safeFromQuantity("chain0").has_value());
    BOOST_CHECK(!safeFromQuantity("0x10zz").has_value());
    BOOST_CHECK(!safeFromQuantity("-1").has_value());
    BOOST_CHECK(!safeFromQuantity("+1").has_value());
    BOOST_CHECK(!safeFromQuantity("0x").has_value());
    BOOST_CHECK(!safeFromQuantity("").has_value());
    BOOST_CHECK(!safeFromQuantity("0x0x1a").has_value());
    BOOST_CHECK(!safeFromQuantity("10000000000000000").has_value());
}

/// safeFromBigQuantity is the strict u256 counterpart of fromBigQuantity. The
/// fromBigQuantity checks below are the baseline they exist to replace: hex2u catches
/// every parse failure and answers 0, so on that path a malformed quantity is
/// indistinguishable from a genuine zero.
BOOST_AUTO_TEST_CASE(testBigQuantity)
{
    BOOST_CHECK_EQUAL(*safeFromBigQuantity("0x10"), u256(0x10));
    BOOST_CHECK_EQUAL(*safeFromBigQuantity("0X10"), u256(0x10));
    BOOST_CHECK_EQUAL(*safeFromBigQuantity("10"), u256(0x10));  // bare hex, not decimal
    // Exactly 64 hex digits: the full u256 width, still accepted.
    auto const widest = "0x" + std::string(64, 'f');
    BOOST_CHECK_EQUAL(*safeFromBigQuantity(widest), std::numeric_limits<u256>::max());

    // Malformed input is rejected instead of silently becoming 0.
    BOOST_CHECK(!safeFromBigQuantity("0xnothex").has_value());
    BOOST_CHECK_EQUAL(fromBigQuantity("0xnothex"), u256(0));
    BOOST_CHECK(!safeFromBigQuantity("0x10zz").has_value());
    BOOST_CHECK(!safeFromBigQuantity("-1").has_value());
    BOOST_CHECK(!safeFromBigQuantity("0x").has_value());
    BOOST_CHECK(!safeFromBigQuantity("").has_value());
    BOOST_CHECK(!safeFromBigQuantity("0x0x1a").has_value());

    // 65 hex digits overflows u256; hex2u truncates it to the low 32 bytes instead.
    auto const tooWide = "0x1" + std::string(64, '0');
    BOOST_CHECK(!safeFromBigQuantity(tooWide).has_value());
    BOOST_CHECK_EQUAL(fromBigQuantity(tooWide), u256(0));
}

/// test asString && asBytes
BOOST_AUTO_TEST_CASE(testStringTrans)
{
    std::string tmp_str = "abcdef012343";
    BOOST_CHECK(asString(asBytes(tmp_str)) == tmp_str);
    BOOST_CHECK(asString(asBytes(tmp_str)) == tmp_str);
    // construct random vector
    unsigned int round = 10;
    unsigned int size = 10;
    std::string tmp_str_from_ref;
    bytes tmp_bytes(size, 0);
    for (unsigned int i = 0; i < round; i++)
    {
        for (unsigned int j = 0; j < size; j++)
        {
            tmp_bytes[j] = std::rand();
        }
        tmp_str = asString(tmp_bytes);
        tmp_str_from_ref = asString(ref(tmp_bytes));
        BOOST_CHECK(tmp_str == tmp_str_from_ref);
        BOOST_CHECK(asBytes(tmp_str) == tmp_bytes);
    }
}

/// test toBigEndian && fromBigEndian
BOOST_AUTO_TEST_CASE(testBigEndian)
{
    // check u256
    u256 number("9832989324908234742342343243243234324324243432432234324");
    u160 number_u160("983298932");
    bytes big_endian_bytes = toBigEndian(number);
    BOOST_CHECK(fromBigEndian<u256>(big_endian_bytes) == number);
    BOOST_CHECK(fromBigEndian<u160>(toBigEndian(number_u160)) == number_u160);
}

/// test toBigEndian && fromBigEndian
BOOST_AUTO_TEST_CASE(testBigEndianToU64)
{
    uint64_t number = 0xfffffffffffffffa;
    u256 u256Number(number);
    auto bigEndU64 = toBigEndian(u256Number);
    std::cout << "uint64_t:" << number << std::endl;
    std::cout << "u256:" << u256Number.str(16) << std::endl;
    std::cout << "bigEndU64:" << toHex(bigEndU64) << std::endl;
    // check u256
    uint64_t fromBig0 = 0;
    std::reverse_copy(bigEndU64.data() + 24, bigEndU64.data() + 32, (char*)&fromBig0);
    std::cout << "fromBig:" << fromBig0 << std::endl;
    BOOST_CHECK(fromBig0 == number);

    uint64_t fromBig = 0;
    std::reverse(bigEndU64.data() + 24, bigEndU64.data() + 32);
    fromBig = *(uint64_t*)(bigEndU64.data() + 24);
    std::cout << "fromBig:" << fromBig << std::endl;
    BOOST_CHECK(fromBig == number);
}

/// test operator+
BOOST_AUTO_TEST_CASE(testOperators)
{
    // test is_pod operator+
    std::string a_str = "abcdef";
    std::string b_str = "01234";
    std::vector<char> a_vec(a_str.begin(), a_str.end());
    std::vector<char> b_vec(b_str.begin(), b_str.end());
    std::vector<char> result = a_vec + b_vec;
    BOOST_CHECK(std::string(result.begin(), result.end()) == (a_str + b_str));
    // test common operator+
    std::vector<std::string> total_array;
    std::vector<std::string> a_str_array;
    a_str_array.push_back("11");
    a_str_array.push_back("22");
    total_array = a_str_array;
    std::vector<std::string> b_str_array;
    b_str_array.push_back("aa");
    b_str_array.push_back("cc");
    total_array.push_back("aa");
    total_array.push_back("cc");
    std::vector<std::string> c_str_array = a_str_array + b_str_array;
    BOOST_CHECK(c_str_array == total_array);

    // test toString
    string32 s_32 = {{'a', 'b', 'c'}};
    std::string s = toString(s_32);
    BOOST_CHECK(s == "abc");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
