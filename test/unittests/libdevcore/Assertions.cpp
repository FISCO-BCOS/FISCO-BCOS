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


#include <libdevcore/Assertions.h>
#include <boost/test/unit_test.hpp>

using namespace dev;

namespace dev
{
namespace test
{
BOOST_AUTO_TEST_SUITE(Assertions)

/// test asserts micro
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
BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace dev
