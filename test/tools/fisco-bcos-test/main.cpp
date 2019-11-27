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
 * @brief: Empty test framework of FISCO-BCOS. Define BOOST_TEST_MODULE and import easylogging
 *
 * @file main.cpp
 * @author: yujiechen, jimmyshi
 * @date 2018-08-24
 */
#define BOOST_TEST_MODULE FISCO_BCOS_Tests
#define BOOST_TEST_NO_MAIN

#include <boost/test/included/unit_test.hpp>
#include <boost/test/unit_test.hpp>

int main(int argc, const char* argv[])
{
    auto fakeInit = [](int, char**) -> boost::unit_test::test_suite* { return nullptr; };
    int result = boost::unit_test::unit_test_main(fakeInit, argc, const_cast<char**>(argv));
    return result;
}
