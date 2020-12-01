/**
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
 * @brief unit test for DataConvertUtility.*(mainly type covert)
 *
 * @file DataConvertUtility.cpp
 * @author: yujiechen
 * @date 2018-08-24
 */
#include <libutilities/DataConvertUtility.h>
#include <libutilities/Exceptions.h>
#include <libutilities/vector_ref.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <cstdlib>
#include <ctime>
#include <iostream>

using namespace bcos;
namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(CommonDataTests, TestOutputHelperFixture)

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
        hexStr = *toHexString(hexVec);
        hexStrWithPrefix = *toHexString(hexVec);
        BOOST_CHECK(*fromHexString(hexStr) == hexVec);
        BOOST_CHECK(*fromHexString(hexStrWithPrefix) == hexVec);
    }
    // fromHexString Exception
    BOOST_CHECK_THROW(fromHexString("0934xyz"), BadHexCharacter);
    BOOST_CHECK(isHexString("0934xyz") == false);

    BOOST_CHECK(isHexString("0x000abc") == true);

    BOOST_CHECK(isHexString("000123123") == true);
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
