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
 * @file ABI.cpp
 * @author: jimmyshi
 * @date 2018-09-02
 */

#include <libethcore/ABI.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <iostream>
#include <string>

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace dev::test;
namespace ut = boost::unit_test;

namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(ABITest, TestOutputHelperFixture)

void Test_abi_gm(const string& _str)
{
    ContractABI ct;
    bytes serialBytes = ct.abiIn(_str);
    bytesConstRef serial = bytesConstRef(&serialBytes).cropped(4);
    std::cout << "====_str========== " << _str << std::endl;
    std::cout << "abi serialBytes ====>" << serialBytes << std::endl;
}
BOOST_AUTO_TEST_CASE(ContractABITest)
{
    ContractABI ct;

    u256 x = 12345;
    u160 y = 67890;
    string xs("xxxsxxxsxxs");
    string ys("ysysysysysysys");
    string32 str32 = {{6, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0}};
    byte b = 'a';
    bytes serialBytes = ct.abiIn("caculate(u256,u160)", b, xs, x, str32, y, ys);
    std::cout << "abi serialBytes ====>" << serialBytes << std::endl;
    bytesConstRef serial = bytesConstRef(&serialBytes).cropped(4);

    u256 compareX;
    u160 compareY;
    string compareXs;
    string compareYs;
    string32 compareStr32;
    byte compareB;

    ct.abiOut(serial, compareB, compareXs, compareX, compareStr32, compareY, compareYs);
    BOOST_CHECK_EQUAL(x, compareX);
    BOOST_CHECK_EQUAL(y, compareY);
    BOOST_CHECK_EQUAL(xs, compareXs);
    BOOST_CHECK_EQUAL(ys, compareYs);
    BOOST_CHECK(str32 == compareStr32);
    BOOST_CHECK_EQUAL(b, compareB);  // There are some bug of convert the byte
    Test_abi_gm("EQ(string,int256)");
    Test_abi_gm("EQ(string,string)");
    Test_abi_gm("GE(string,int256)");
    Test_abi_gm("GT(string,int256)");
    Test_abi_gm("LE(string,int256)");
    Test_abi_gm("LT(string,int256)");
    Test_abi_gm("NE(string,int256)");
    Test_abi_gm("NE(string,string)");
    Test_abi_gm("limit(int256)");
    Test_abi_gm("limit(int256,int256)");

    Test_abi_gm("select(string,string)");

    Test_abi_gm("get(int256)");
    Test_abi_gm("size()");

    Test_abi_gm("getInt(string)");
    Test_abi_gm("set(string,int256)");
    Test_abi_gm("set(string,string)");
    Test_abi_gm("getAddress(string)");
    Test_abi_gm("getBytes64(string)");

    Test_abi_gm("add(string)");
    Test_abi_gm("remove(string)");

    Test_abi_gm("openDB(string)");
    Test_abi_gm("openTable(string)");
    Test_abi_gm("createTable(string,string,string)");

    Test_abi_gm("select(string,address)");
    Test_abi_gm("insert(string,address)");
    Test_abi_gm("newCondition()");
    Test_abi_gm("newEntry()");
    Test_abi_gm("remove(string,address)");
    Test_abi_gm("update(string,address,address)");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev