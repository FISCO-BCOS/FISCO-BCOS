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
 * @brief unit test for CommonData.*(mainly type covert)
 *
 * @file CommonData.cpp
 * @author: yujiechen
 * @date 2018-08-24
 */
#include <libutilities/CommonData.h>
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

/// test toHex && fromHex && isHex && isHash
BOOST_AUTO_TEST_CASE(testHex)
{
    // toHex && fromHex
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
        hexStrWithPrefix = toHex(hexVec);
        BOOST_CHECK(fromHex(hexStr) == hexVec);
        BOOST_CHECK(fromHex(hexStrWithPrefix) == hexVec);
    }
    // fromHex Exception
    BOOST_REQUIRE_NO_THROW(fromHex("0934xyz", WhenError::DontThrow));
    BOOST_CHECK_THROW(fromHex("0934xyz", WhenError::Throw), BadHexCharacter);
    // isHex && isHash
    BOOST_CHECK(isHex("0934xyz") == false);

    BOOST_CHECK(isHex("0x000abc") == true);
    BOOST_CHECK(isHash<h256>("0x000abc") == false);

    BOOST_CHECK(isHex("000123123") == true);
    BOOST_CHECK(isHash<h256>("000123123") == false);
    BOOST_CHECK(
        isHash<h256>("06d012b348ff19750bd253a1a508064ce7752f0adb8304a4c5bbb63ccc106f0d") == true);
    BOOST_CHECK(
        isHash<h256>("0x06d012b348ff19750bd253a1a508064ce7752f0adb8304a4c5bbb63ccc106f0d") == true);
    BOOST_CHECK(isHash<h128>("b2b4085dcee9c8af9a420fe87dcb7d4b") == true);
    BOOST_CHECK(isHash<h128>("0xb2b4085dcee9c8af9a420fe87dcb7d4b") == true);
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

/// test asNibbles && toBigEndian && fromBigEndian
BOOST_AUTO_TEST_CASE(testBigEndian)
{
    // check asNibbles
    // asNibbles:Converts a string into the big-endian base-16 stream of integers (NOT ASCII).
    std::string str = "A";
    BOOST_CHECK((asNibbles(bytesConstRef(str))[0]) == 4 && (asNibbles(bytesConstRef(str))[1] == 1));
    // check u256
    u256 number("9832989324908234742342343243243234324324243432432234324");
    u160 number_u160("983298932");
    bytes big_endian_bytes = toBigEndian(number);
    BOOST_CHECK(fromBigEndian<u256>(big_endian_bytes) == number);
    BOOST_CHECK(fromBigEndian<u160>(toBigEndian(number_u160)) == number_u160);
    BOOST_CHECK(fromBigEndian<u256>(toBigEndianString(number)) == number);
    BOOST_CHECK(fromBigEndian<u160>(toBigEndianString(number_u160)) == number_u160);
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
