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
 * @brief
 *
 * @file Assertions.cpp
 * @author: yujiechen
 * @date 2018-08-24
 */

#include <iostream>

#include "libutilities/Assertions.h"
#include "libutilities/Exceptions.h"
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <string>
#include <vector>

using namespace bcos;
namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(Assertions, TestOutputHelperFixture)
/// test micro "asserts"
BOOST_AUTO_TEST_CASE(testAsserts)
{
    // logical test
    int a = 10;
    int b = 11;
    BOOST_CHECK(asserts(a == b) == true);
    BOOST_CHECK(asserts(a > b) == true);
    BOOST_CHECK(asserts(a < b) == false);
    BOOST_CHECK(asserts(a == a) == false);
    // math test
    BOOST_CHECK(asserts(a + b == 21) == false);
    BOOST_CHECK(asserts(a + b == 22) == true);
    // pointer test
    char* str = nullptr;
    BOOST_CHECK(asserts(str) == true);
    const char* str2 = "hello, test";
    BOOST_CHECK(asserts(str2) == false);
}

/// test micro "assertsEqual"
BOOST_AUTO_TEST_CASE(testAssertsEqual)
{
    BOOST_CHECK(assertsEqual(10, 10) == false);
    BOOST_CHECK(assertsEqual('a', 'a') == false);
    BOOST_CHECK(assertsEqual(10, 11) == true);
    BOOST_CHECK(assertsEqual('a', 'b') == true);
    std::string str1 = "abcd";
    std::string str2 = "bcde";
    BOOST_CHECK(assertsEqual(str1, str2) == true);
    str2 = "abcd";
    BOOST_CHECK(assertsEqual(str1, str2) == false);
}
/// test function "testAssertAux"
BOOST_AUTO_TEST_CASE(testAssertAux)
{
    char const* str = "testAssertAux_func";
    unsigned line = 10;
    char const* file = "test/unittests/Assertions.cpp";
    char const* func = "testAssertAux_BOOST_AUTO_TEST_CASE";
    BOOST_CHECK(assertAux(true, str, line, file, func) == false);
    BOOST_CHECK(assertAux(false, str, line, file, func) == true);
    BOOST_CHECK(assertAux(true, "a + b == 10", __LINE__, __FILE__, ETH_FUNC) == false);
    BOOST_CHECK(assertAux(false, "a + b == 10", __LINE__, __FILE__, ETH_FUNC) == true);
}

/// test template function "assertEqualAux"
BOOST_AUTO_TEST_CASE(testAssertEqualAux)
{
    unsigned int a = 0xffffffff;
    unsigned b = -1;
    const char* aStr = "(signed)(-1)";
    const char* bStr = "(unsigned)(-1)";
    unsigned line = 11;
    char const* file = "test/unittests/Assertions.cpp";
    char const* func = "testAssertEqualAux_BOOST_AUTO_TEST_CASE";
    BOOST_CHECK(assertEqualAux(a, b, aStr, bStr, line, file, func) == false);
    BOOST_CHECK(assertEqualAux(a, b, aStr, bStr, __LINE__, __FILE__, ETH_FUNC) == false);
}

/// test micro "assertThrow"
BOOST_AUTO_TEST_CASE(testAssertThrow)
{
    BOOST_CHECK_THROW(assertThrow(false, BadRoot, "BadRoot Throw"), BadRoot);
}

/// test template function "testAssertThrow"
BOOST_AUTO_TEST_CASE(testAssertThrowAux)
{
    BOOST_CHECK_THROW(assertThrowAux<InterfaceNotSupported>(
                          false, "InterfaceNotSupported Throw", __LINE__, __FILE__, ETH_FUNC),
        InterfaceNotSupported);
    BOOST_CHECK_THROW(assertThrowAux<BadRoot>(false, "BadRoot Throw", 21,
                          "test/unittests/Assertions.cpp", "testAssertThrow"),
        BadRoot);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
