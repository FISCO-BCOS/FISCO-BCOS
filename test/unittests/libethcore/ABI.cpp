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
#include <iostream>

#include <libethcore/ABI.h>
#include <libethcore/ABIParser.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>

using namespace std;
using namespace bcos;
using namespace bcos::eth;
using namespace bcos::eth::abi;
using namespace bcos::test;
namespace ut = boost::unit_test;

namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(ABITest, TestOutputHelperFixture)

BOOST_AUTO_TEST_CASE(ContractABIType_func0)
{
    ContractABI ct;
    u256 a("0x123");
    std::vector<u256> b;
    u256 u0("0x456");
    u256 u1("0x789");
    b.push_back(u0);
    b.push_back(u1);
    string32 c = toString32("1234567890");
    std::string d = "Hello, world!";

    auto r = ct.abiInHex("", a, b, c, d);
    auto rb = ct.abiIn("", a, b, c, d);
    BOOST_CHECK(r == toHex(rb));

    BOOST_CHECK_EQUAL(
        r, std::string("0000000000000000000000000000000000000000000000000000000000000123"
                       "0000000000000000000000000000000000000000000000000000000000000080"
                       "3132333435363738393000000000000000000000000000000000000000000000"
                       "00000000000000000000000000000000000000000000000000000000000000e0"
                       "0000000000000000000000000000000000000000000000000000000000000002"
                       "0000000000000000000000000000000000000000000000000000000000000456"
                       "0000000000000000000000000000000000000000000000000000000000000789"
                       "000000000000000000000000000000000000000000000000000000000000000d"
                       "48656c6c6f2c20776f726c642100000000000000000000000000000000000000"));

    u256 outA;
    std::vector<u256> outB;
    string32 outC;
    std::string outD;
    auto Ok = ct.abiOutHex(r, outA, outB, outC, outD);
    BOOST_CHECK(Ok == true);
    BOOST_CHECK(a == outA);
    BOOST_CHECK(b == outB);
    BOOST_CHECK(c == outC);
    BOOST_CHECK(d == outD);
}

BOOST_AUTO_TEST_CASE(ContractABIType_func1)
{
    ContractABI ct;
    std::string a("dave");
    bool b(true);
    std::vector<u256> c{1, 2, 3};

    auto rb = ct.abiIn("", a, b, c);
    auto r = toHex(rb);
    BOOST_CHECK_EQUAL(
        r, std::string("0000000000000000000000000000000000000000000000000000000000000060"
                       "0000000000000000000000000000000000000000000000000000000000000001"
                       "00000000000000000000000000000000000000000000000000000000000000a0"
                       "0000000000000000000000000000000000000000000000000000000000000004"
                       "6461766500000000000000000000000000000000000000000000000000000000"
                       "0000000000000000000000000000000000000000000000000000000000000003"
                       "0000000000000000000000000000000000000000000000000000000000000001"
                       "0000000000000000000000000000000000000000000000000000000000000002"
                       "0000000000000000000000000000000000000000000000000000000000000003"));

    std::string outA;
    bool outB;
    std::vector<u256> outC;
    auto Ok = ct.abiOutHex(r, outA, outB, outC);
    BOOST_CHECK(Ok == true);
    BOOST_CHECK(a == outA);
    BOOST_CHECK(b == outB);
    BOOST_CHECK(c == outC);
}

BOOST_AUTO_TEST_CASE(ContractABIType_func2)
{
    ContractABI ct;
    std::string a("daslfjaklfdaskl");
    u256 b = 1111;
    std::array<u256, 6> c{1, 2, 3, 4, 5, 6};
    std::vector<u256> d{1, 2, 3, 4, 5, 6};
    bool e = false;
    Address f("0x692a70d2e424a56d2c6c27aa97d1a86395877b3a");

    auto rb = ct.abiIn("", a, b, c, d, e, f);
    auto r = toHex(rb);

    std::string outA;
    u256 outB;
    std::array<u256, 6> outC;
    std::vector<u256> outD;
    bool outE = true;
    Address outF;

    auto Ok = ct.abiOutHex(r, outA, outB, outC, outD, outE, outF);
    BOOST_CHECK(Ok == true);
    BOOST_CHECK(a == outA);
    BOOST_CHECK(b == outB);
    BOOST_CHECK(c == outC);
    BOOST_CHECK(d == outD);
    BOOST_CHECK(e == outE);
    BOOST_CHECK(f == outF);
}


BOOST_AUTO_TEST_CASE(ContractABIType_func3)
{
    //"aaafadsfsfadsfdasf","0x35ef07393b57464e93deb59175ff72e6499450cf",11111,-11111
    ContractABI ct;
    std::string a("aaafadsfsfadsfdasf");
    Address b("0x35ef07393b57464e93deb59175ff72e6499450cf");
    u256 c = 11111;
    int d = -11111;

    auto rb = ct.abiIn("", a, b, c, d);
    auto r = toHex(rb);

    BOOST_CHECK_EQUAL(
        r, std::string(
               "00000000000000000000000000000000000000000000000000000000000000800000000"
               "0000000000000000035ef07393b57464e93deb59175ff72e6499450cf000000000000000000000000"
               "0000000000000000000000000000000000002b67fffffffffffffffffffffffffffffffffffffffff"
               "fffffffffffffffffffd4990000000000000000000000000000000000000000000000000000000000"
               "0000126161616661647366736661647366646173660000000000000000000000000000"));

    std::string outA;
    Address outB;
    u256 outC;
    s256 outD;

    auto Ok = ct.abiOutHex(r, outA, outB, outC, outD);
    BOOST_CHECK(Ok == true);
    BOOST_CHECK(a == outA);
    BOOST_CHECK(b == outB);
    BOOST_CHECK(c == outC);
    BOOST_CHECK(d == outD);
}


BOOST_AUTO_TEST_CASE(ContractABIType_func4)
{
    //"aaafadsfsfadsfdasf","0x35ef07393b57464e93deb59175ff72e6499450cf",11111,-11111
    ContractABI ct;
    std::string a;
    u256 b;
    s256 c;
    string32 d;
    std::array<Address, 10> e;
    std::vector<std::array<std::string, 3>> f(3);

    auto rb = ct.abiIn("", a, b, c, d, e, f);
    auto r = toHex(rb);

    std::string outA = "HelloWorld";
    u256 outB = 11111;
    s256 outC = -1111;
    string32 outD = toString32("aa");
    std::array<Address, 10> outE;
    std::vector<std::array<std::string, 3>> outF;

    auto Ok = ct.abiOutHex(r, outA, outB, outC, outD, outE, outF);
    BOOST_CHECK(Ok == true);
    BOOST_CHECK(a == outA);
    BOOST_CHECK(b == outB);
    BOOST_CHECK(c == outC);
    BOOST_CHECK(d == outD);
    BOOST_CHECK(e == outE);
    BOOST_CHECK(f == outF);
}

BOOST_AUTO_TEST_CASE(ContractABIType_u256)
{
    ContractABI ct;

    u256 x = 0;
    std::string r = toHex(ct.serialise(x));
    BOOST_CHECK(r == "0000000000000000000000000000000000000000000000000000000000000000");

    u256 y("0x7fffffffffffffff");
    r = toHex(ct.serialise(y));
    BOOST_CHECK(r == "0000000000000000000000000000000000000000000000007fffffffffffffff");

    u256 z("0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    r = toHex(ct.serialise(z));
    BOOST_CHECK(r == "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

    u256 u("0xfffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffe");
    r = toHex(ct.serialise(u));
    BOOST_CHECK(r == "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffe");
}

BOOST_AUTO_TEST_CASE(ContractABIType_s256)
{
    ContractABI ct;

    s256 x = 0;
    std::string r = toHex(ct.serialise(x));
    BOOST_CHECK_EQUAL(r, "0000000000000000000000000000000000000000000000000000000000000000");

    s256 y("0x7fffffffffffffff");
    r = toHex(ct.serialise(y));
    BOOST_CHECK_EQUAL(r, "0000000000000000000000000000000000000000000000007fffffffffffffff");

    s256 z = -1;
    r = toHex(ct.serialise(z));
    BOOST_CHECK_EQUAL(r, "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

    s256 s = 1000;
    r = toHex(ct.serialise(s));
    BOOST_CHECK_EQUAL(r, "00000000000000000000000000000000000000000000000000000000000003e8");
}

BOOST_AUTO_TEST_CASE(ContractABIType_bool)
{
    ContractABI ct;

    bool x = true;
    std::string r = toHex(ct.serialise(x));
    BOOST_CHECK_EQUAL(r, "0000000000000000000000000000000000000000000000000000000000000001");


    bool y = false;
    r = toHex(ct.serialise(y));
    BOOST_CHECK_EQUAL(r, "0000000000000000000000000000000000000000000000000000000000000000");
}

BOOST_AUTO_TEST_CASE(ContractABIType_addr)
{
    ContractABI ct;

    Address x;
    std::string r = toHex(ct.serialise(x));
    BOOST_CHECK_EQUAL(r, "0000000000000000000000000000000000000000000000000000000000000000");


    Address y("0xbe5422d15f39373eb0a97ff8c10fbd0e40e29338");
    r = toHex(ct.serialise(y));
    BOOST_CHECK_EQUAL(r, "000000000000000000000000be5422d15f39373eb0a97ff8c10fbd0e40e29338");
}

BOOST_AUTO_TEST_CASE(ContractABIType_string)
{
    ContractABI ct;

    std::string x("Hello, world!");
    std::string r = toHex(ct.serialise(x));
    BOOST_CHECK_EQUAL(
        r, std::string("000000000000000000000000000000000000000000000000000000000000000d48656c6c6"
                       "f2c20776f726c642100000000000000000000000000000000000000"));

    std::string y("");
    r = toHex(ct.serialise(y));
    BOOST_CHECK_EQUAL(
        r, std::string("0000000000000000000000000000000000000000000000000000000000000000"));
}

BOOST_AUTO_TEST_CASE(ContractABIType_array_uint256)
{
    ContractABI ct;

    std::array<u256, 3> x{1, 2, 3};
    std::string r = toHex(ct.serialise(x));
    BOOST_CHECK_EQUAL(
        r, std::string("00000000000000000000000000000000000000000000000000000000000000010"
                       "0000000000000000000000000"
                       "00000000000000000000000000000000000002000000000000000000000000000"
                       "0000000000000000000000000"
                       "000000000003"));

    std::vector<u256> y{1, 2, 3};
    r = toHex(ct.serialise(y));
    BOOST_CHECK_EQUAL(
        r, std::string("000000000000000000000000000000000000000000000000000000000000000300000000000"
                       "000000000000000"
                       "0000000000000000000000000000000000000100000000000000000000000000"
                       "000000000000000000000000000000000000020000000000000000000000000000000000000"
                       "000000000000000"
                       "000000000003"));
}

BOOST_AUTO_TEST_CASE(ContractABITest0)
{
    ContractABI ct;

    u256 a = 12345;
    s256 b = -67890;
    string c("xxxsxxxsxxs");
    string32 d = toString32(std::string("adsggsakjffl;kajsdf"));

    auto rb = ct.abiIn("", a, b, c, d);
    auto r = toHex(rb);

    u256 outA;
    s256 outB;
    string outC;
    string32 outD;

    bool Ok = ct.abiOutHex(r, outA, outB, outC, outD);
    BOOST_CHECK(Ok == true);
    BOOST_CHECK(a == outA);
    BOOST_CHECK(b == outB);
    BOOST_CHECK(c == outC);
    BOOST_CHECK(d == outD);
}

BOOST_AUTO_TEST_CASE(ContractABITest1)
{
    u256 a = 100;
    s256 b = -100;
    std::string c = "abc";
    std::vector<std::string> d = {"abc", "abc", "abc"};
    std::array<std::string, 3> e{"abc", "abc", "abc"};

    std::string expect =
        "0000000000000000000000000000000000000000000000000000000000000064ffffffffffffffffffffffffff"
        "ffffffffffffffffffffffffffffffffffff9c0000000000000000000000000000000000000000000000000000"
        "0000000000a000000000000000000000000000000000000000000000000000000000000000e000000000000000"
        "000000000000000000000000000000000000000000000002200000000000000000000000000000000000000000"
        "000000000000000000000003616263000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000030000000000000000000000000000"
        "000000000000000000000000000000000060000000000000000000000000000000000000000000000000000000"
        "00000000a000000000000000000000000000000000000000000000000000000000000000e00000000000000000"
        "000000000000000000000000000000000000000000000003616263000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000036162"
        "630000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000361626300000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000060000000000000000000"
        "00000000000000000000000000000000000000000000a000000000000000000000000000000000000000000000"
        "000000000000000000e00000000000000000000000000000000000000000000000000000000000000003616263"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000036162630000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000361626300000000000000"
        "00000000000000000000000000000000000000000000";

    ContractABI ct;
    auto rb = ct.abiIn("", a, b, c, d, e);
    auto r = toHex(rb);
    BOOST_CHECK_EQUAL(r, expect);

    u256 outA;
    s256 outB;
    std::string outC;
    std::vector<std::string> outD;
    std::array<std::string, 3> outE;

    bool Ok = ct.abiOutHex(r, outA, outB, outC, outD, outE);

    BOOST_CHECK_EQUAL(Ok, true);
    BOOST_CHECK(a == outA);
    BOOST_CHECK(b == outB);
    BOOST_CHECK(c == outC);
    BOOST_CHECK(d == outD);
    BOOST_CHECK(e == outE);
}

BOOST_AUTO_TEST_CASE(ContractABITest2)
{
    std::array<std::vector<u256>, 3> a{
        std::vector<u256>{1}, std::vector<u256>{2, 3}, std::vector<u256>{4, 5, 6}};

    std::string expect =
        "000000000000000000000000000000000000000000000000000000000000002000000000000000000000000000"
        "000000000000000000000000000000000000600000000000000000000000000000000000000000000000000000"
        "0000000000a0000000000000000000000000000000000000000000000000000000000000010000000000000000"
        "000000000000000000000000000000000000000000000000010000000000000000000000000000000000000000"
        "000000000000000000000001000000000000000000000000000000000000000000000000000000000000000200"
        "000000000000000000000000000000000000000000000000000000000000020000000000000000000000000000"
        "000000000000000000000000000000000003000000000000000000000000000000000000000000000000000000"
        "000000000300000000000000000000000000000000000000000000000000000000000000040000000000000000"
        "000000000000000000000000000000000000000000000005000000000000000000000000000000000000000000"
        "0000000000000000000006";

    ContractABI ct;
    auto rb = ct.abiIn("", a);
    auto r = toHex(rb);

    BOOST_CHECK(r == expect);

    std::array<std::vector<u256>, 3> outA;
    bool Ok = ct.abiOutHex(r, outA);

    BOOST_CHECK_EQUAL(Ok, true);
    BOOST_CHECK(a == outA);
}

BOOST_AUTO_TEST_CASE(ContractABITest3)
{
    ContractABI ct;
    u256 a = 123;
    Address b("0x692a70d2e424a56d2c6c27aa97d1a86395877b3a");
    std::string c = "string c";
    std::vector<u256> d{1, 2, 3};
    std::array<u256, 3> e = {4, 5, 6};

    std::vector<std::string> f{"abc", "def", "ghi"};
    std::array<std::string, 3> g{"abc", "def", "ghi"};

    std::vector<std::vector<u256>> h{{1, 1, 1}, {2, 2, 2}, {3, 3, 3}};
    std::vector<std::array<u256, 3>> i{{4, 4, 4}, {5, 5, 5}};

    auto rb = ct.abiIn("", a, b, c, d, e, f, g, h, i);
    auto r = toHex(rb);

    std::string expect =
        "000000000000000000000000000000000000000000000000000000000000007b00000000000000000000000069"
        "2a70d2e424a56d2c6c27aa97d1a86395877b3a0000000000000000000000000000000000000000000000000000"
        "00000000016000000000000000000000000000000000000000000000000000000000000001a000000000000000"
        "000000000000000000000000000000000000000000000000040000000000000000000000000000000000000000"
        "000000000000000000000005000000000000000000000000000000000000000000000000000000000000000600"
        "000000000000000000000000000000000000000000000000000000000002200000000000000000000000000000"
        "000000000000000000000000000000000360000000000000000000000000000000000000000000000000000000"
        "000000048000000000000000000000000000000000000000000000000000000000000006800000000000000000"
        "000000000000000000000000000000000000000000000008737472696e67206300000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000030000"
        "000000000000000000000000000000000000000000000000000000000001000000000000000000000000000000"
        "000000000000000000000000000000000200000000000000000000000000000000000000000000000000000000"
        "000000030000000000000000000000000000000000000000000000000000000000000003000000000000000000"
        "000000000000000000000000000000000000000000006000000000000000000000000000000000000000000000"
        "000000000000000000a000000000000000000000000000000000000000000000000000000000000000e0000000"
        "000000000000000000000000000000000000000000000000000000000361626300000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000003646566000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000036768690000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000006000000000"
        "000000000000000000000000000000000000000000000000000000a00000000000000000000000000000000000"
        "0000000000000000000000000000e0000000000000000000000000000000000000000000000000000000000000"
        "000361626300000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000003646566000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000036768690000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000300000000000000000000000000000000000000000000000000000000000000"
        "6000000000000000000000000000000000000000000000000000000000000000e0000000000000000000000000"
        "000000000000000000000000000000000000016000000000000000000000000000000000000000000000000000"
        "000000000000030000000000000000000000000000000000000000000000000000000000000001000000000000"
        "000000000000000000000000000000000000000000000000000100000000000000000000000000000000000000"
        "000000000000000000000000010000000000000000000000000000000000000000000000000000000000000003"
        "000000000000000000000000000000000000000000000000000000000000000200000000000000000000000000"
        "000000000000000000000000000000000000020000000000000000000000000000000000000000000000000000"
        "000000000002000000000000000000000000000000000000000000000000000000000000000300000000000000"
        "000000000000000000000000000000000000000000000000030000000000000000000000000000000000000000"
        "000000000000000000000003000000000000000000000000000000000000000000000000000000000000000300"
        "000000000000000000000000000000000000000000000000000000000000020000000000000000000000000000"
        "000000000000000000000000000000000004000000000000000000000000000000000000000000000000000000"
        "000000000400000000000000000000000000000000000000000000000000000000000000040000000000000000"
        "000000000000000000000000000000000000000000000005000000000000000000000000000000000000000000"
        "00000000000000000000050000000000000000000000000000000000000000000000000000000000000005";


    BOOST_CHECK_EQUAL(r, expect);

    u256 outA;
    Address outB;
    std::string outC;
    std::vector<u256> outD;
    std::array<u256, 3> outE;

    std::vector<std::string> outF;
    std::array<std::string, 3> outG;

    std::vector<std::vector<u256>> outH;
    std::vector<std::array<u256, 3>> outI;
    bool Ok = ct.abiOutHex(r, outA, outB, outC, outD, outE, outF, outG, outH, outI);

    BOOST_CHECK(Ok == true);
    BOOST_CHECK(a == outA);
    BOOST_CHECK(b == outB);
    BOOST_CHECK(c == outC);
    BOOST_CHECK(d == outD);
    BOOST_CHECK(e == outE);
    BOOST_CHECK(f == outF);
    BOOST_CHECK(g == outG);
    BOOST_CHECK(h == outH);
    BOOST_CHECK(i == outI);
}

BOOST_AUTO_TEST_CASE(ContractABI_ABIType)
{
    std::string s = "string";
    ABIInType at;
    auto ok = at.reset(s);
    BOOST_CHECK(ok == true);
    BOOST_CHECK(at.getType() == "string");
    BOOST_CHECK(at.dynamic() == true);
    BOOST_CHECK(at.getEleType() == "string");
    BOOST_CHECK(at.rank() == 0);

    ok = at.reset("adf");
    BOOST_CHECK(ok == false);

    ok = at.reset("uint256");
    BOOST_CHECK(ok == true);
    BOOST_CHECK(at.getType() == "uint256");
    BOOST_CHECK(at.dynamic() == false);
    BOOST_CHECK(at.getEleType() == "uint256");
    BOOST_CHECK(at.rank() == 0);


    ok = at.reset("bool");
    BOOST_CHECK(ok == true);
    BOOST_CHECK(at.getType() == "bool");
    BOOST_CHECK(at.dynamic() == false);
    BOOST_CHECK(at.getEleType() == "bool");
    BOOST_CHECK(at.rank() == 0);

    ok = at.reset("bool[]");
    BOOST_CHECK(ok == true);
    BOOST_CHECK(at.getType() == "bool[]");
    BOOST_CHECK(at.dynamic() == true);
    BOOST_CHECK(at.getEleType() == "bool");
    BOOST_CHECK(at.rank() == 1);

    ok = at.reset("bool[10]");
    BOOST_CHECK(ok == true);
    BOOST_CHECK(at.getType() == "bool[10]");
    BOOST_CHECK(at.dynamic() == false);
    BOOST_CHECK(at.getEleType() == "bool");
    BOOST_CHECK(at.extent(1) == 10);
    BOOST_CHECK(at.rank() == 1);


    ok = at.reset("string[10][][20]");
    BOOST_CHECK(ok == true);
    BOOST_CHECK(at.getType() == "string[10][][20]");
    BOOST_CHECK(at.dynamic() == true);
    BOOST_CHECK(at.getEleType() == "string");
    BOOST_CHECK(at.extent(1) == 10);
    BOOST_CHECK(at.extent(2) == 0);
    BOOST_CHECK(at.extent(3) == 20);
    BOOST_CHECK(at.rank() == 3);

    at.removeExtent();
    BOOST_CHECK(at.extent(1) == 10);
    BOOST_CHECK(at.extent(2) == 0);
    BOOST_CHECK(at.rank() == 2);

    at.removeExtent();
    BOOST_CHECK(at.extent(1) == 10);
    BOOST_CHECK(at.rank() == 1);

    at.removeExtent();
    BOOST_CHECK(at.rank() == 0);
}

BOOST_AUTO_TEST_CASE(ContractABI_ABIFunc0)
{
    std::string s = "transfer (string, uint256, int256, string[])";
    ABIFunc afunc;
    auto ok = afunc.parser(s);
    BOOST_CHECK(ok == true);
    BOOST_CHECK(afunc.getFuncName() == "transfer");
    BOOST_CHECK(afunc.getSignature() == "transfer(string,uint256,int256,string[])");
    std::vector<std::string> exp{"string", "uint256", "int256", "string[]"};
    BOOST_CHECK(afunc.getParamsType() == exp);
}

BOOST_AUTO_TEST_CASE(ContractABI_ABIFunc1)
{
    std::string s0 = "register(string,uint25)";
    ABIFunc afunc0;
    auto ok = afunc0.parser(s0);
    BOOST_CHECK(ok == false);


    std::string s1 = "f()";
    ABIFunc afunc1;
    ok = afunc1.parser(s1);
    BOOST_CHECK(ok == true);
    BOOST_CHECK(afunc1.getFuncName() == "f");
    BOOST_CHECK(afunc1.getSignature() == "f()");
}

BOOST_AUTO_TEST_CASE(ContractABI_ABIFunc2)
{
    std::string s = "trans(string,uint256)";

    ABIFunc afunc;
    auto ok = afunc.parser(s);
    BOOST_CHECK(ok == true);

    BOOST_CHECK(afunc.getFuncName() == "trans");
    BOOST_CHECK(afunc.getSignature() == "trans(string,uint256)");
    std::vector<std::string> exp{"string", "uint256"};
    BOOST_CHECK(afunc.getParamsType() == exp);
}

BOOST_AUTO_TEST_CASE(ContractABI_AbiOutString0)
{
    u256 u = 111111111;
    std::string s = "test string";
    ContractABI ct;
    auto in = ct.abiIn("", u, s);

    ABIFunc afunc;
    auto ok = afunc.parser("test(uint256,string)");
    BOOST_CHECK(ok == true);

    auto allTypes = afunc.getParamsType();

    BOOST_CHECK(allTypes.size() == 2);
    BOOST_CHECK(allTypes[0] == "uint256");
    BOOST_CHECK(allTypes[1] == "string");

    std::vector<std::string> allOut;
    ct.abiOutByFuncSelector(bytesConstRef(&in), allTypes, allOut);
    BOOST_CHECK(allOut.size() == 2);
    BOOST_CHECK(allOut[0] == "111111111");
    BOOST_CHECK(allOut[1] == "test string");
}

BOOST_AUTO_TEST_CASE(ContractABI_AbiOutString1)
{
    u256 u = 111111111;
    s256 i = -11111111;
    std::string s = "aaaaaaa";
    ContractABI ct;
    auto in = ct.abiIn("", s, u, i);

    ABIFunc afunc;
    auto ok = afunc.parser("f(string,uint256,int256)");
    BOOST_CHECK(ok == true);

    auto allTypes = afunc.getParamsType();

    BOOST_CHECK(allTypes.size() == 3);
    BOOST_CHECK(allTypes[0] == "string");
    BOOST_CHECK(allTypes[1] == "uint256");
    BOOST_CHECK(allTypes[2] == "int256");

    std::vector<std::string> allOut;
    ct.abiOutByFuncSelector(bytesConstRef(&in), allTypes, allOut);
    BOOST_CHECK(allOut.size() == 3);
    BOOST_CHECK(allOut[1] == "111111111");
    BOOST_CHECK(allOut[2] == "-11111111");
    BOOST_CHECK(allOut[0] == "aaaaaaa");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
