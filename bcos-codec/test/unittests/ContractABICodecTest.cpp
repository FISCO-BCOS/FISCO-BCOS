/*
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
 * @brief unittest for ContractABICodec
 * @author: octopuswang
 * @date: 2019-04-01
 */
#include "bcos-codec/abi/ContractABICodec.h"
#include "bcos-codec/abi/ContractABIType.h"
#include "bcos-framework/testutils/crypto/HashImpl.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <boost/test/unit_test.hpp>

using namespace std;
using namespace bcos;
using namespace bcos::codec::abi;
using namespace bcos::codec;
namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(ABITest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(ContractABIType_func0)
{
    auto hashImpl = std::make_shared<Keccak256Hash>();
    ContractABICodec ct(hashImpl);
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
    BOOST_CHECK(r == *toHexString(rb));

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
    auto hashImpl = std::make_shared<Keccak256Hash>();
    ContractABICodec ct(hashImpl);
    std::string a("dave");
    bool b(true);
    std::vector<u256> c{1, 2, 3};

    auto rb = ct.abiIn("", a, b, c);
    auto r = *toHexString(rb);
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
    auto hashImpl = std::make_shared<Keccak256Hash>();
    ContractABICodec ct(hashImpl);
    std::string a("daslfjaklfdaskl");
    u256 b = 1111;
    std::array<u256, 6> c{1, 2, 3, 4, 5, 6};
    std::vector<u256> d{1, 2, 3, 4, 5, 6};
    bool e = false;
    Address f("0x692a70d2e424a56d2c6c27aa97d1a86395877b3a");

    auto rb = ct.abiIn("", a, b, c, d, e, f);
    auto r = *toHexString(rb);

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
    auto hashImpl = std::make_shared<Keccak256Hash>();
    ContractABICodec ct(hashImpl);
    std::string a("aaafadsfsfadsfdasf");
    Address b("0x35ef07393b57464e93deb59175ff72e6499450cf");
    u256 c = 11111;
    int d = -11111;

    auto rb = ct.abiIn("", a, b, c, d);
    auto r = *toHexString(rb);

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
    auto hashImpl = std::make_shared<Keccak256Hash>();
    ContractABICodec ct(hashImpl);
    std::string a;
    u256 b;
    s256 c;
    string32 d;
    std::array<Address, 10> e;
    std::vector<std::array<std::string, 3>> f(3);

    auto rb = ct.abiIn("", a, b, c, d, e, f);
    auto r = *toHexString(rb);

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
    auto hashImpl = std::make_shared<Keccak256Hash>();
    ContractABICodec ct(hashImpl);

    u256 x = 0;
    std::string r = *toHexString(ct.serialise(x));
    BOOST_CHECK(r == "0000000000000000000000000000000000000000000000000000000000000000");

    u256 y("0x7fffffffffffffff");
    r = *toHexString(ct.serialise(y));
    BOOST_CHECK(r == "0000000000000000000000000000000000000000000000007fffffffffffffff");

    u256 z("0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    r = *toHexString(ct.serialise(z));
    BOOST_CHECK(r == "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

    u256 u("0xfffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffe");
    r = *toHexString(ct.serialise(u));
    BOOST_CHECK(r == "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffe");
}

BOOST_AUTO_TEST_CASE(ContractABIType_s256)
{
    auto hashImpl = std::make_shared<Keccak256Hash>();
    ContractABICodec ct(hashImpl);

    s256 x = 0;
    std::string r = *toHexString(ct.serialise(x));
    BOOST_CHECK_EQUAL(r, "0000000000000000000000000000000000000000000000000000000000000000");

    s256 y("0x7fffffffffffffff");
    r = *toHexString(ct.serialise(y));
    BOOST_CHECK_EQUAL(r, "0000000000000000000000000000000000000000000000007fffffffffffffff");

    s256 z = -1;
    r = *toHexString(ct.serialise(z));
    BOOST_CHECK_EQUAL(r, "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

    s256 s = 1000;
    r = *toHexString(ct.serialise(s));
    BOOST_CHECK_EQUAL(r, "00000000000000000000000000000000000000000000000000000000000003e8");
}

BOOST_AUTO_TEST_CASE(ContractABIType_bool)
{
    auto hashImpl = std::make_shared<Keccak256Hash>();
    ContractABICodec ct(hashImpl);

    bool x = true;
    std::string r = *toHexString(ct.serialise(x));
    BOOST_CHECK_EQUAL(r, "0000000000000000000000000000000000000000000000000000000000000001");


    bool y = false;
    r = *toHexString(ct.serialise(y));
    BOOST_CHECK_EQUAL(r, "0000000000000000000000000000000000000000000000000000000000000000");
}

BOOST_AUTO_TEST_CASE(ContractABIType_addr)
{
    auto hashImpl = std::make_shared<Keccak256Hash>();
    ContractABICodec ct(hashImpl);

    Address x;
    std::string r = *toHexString(ct.serialise(x));
    BOOST_CHECK_EQUAL(r, "0000000000000000000000000000000000000000000000000000000000000000");


    Address y("0xbe5422d15f39373eb0a97ff8c10fbd0e40e29338");
    r = *toHexString(ct.serialise(y));
    BOOST_CHECK_EQUAL(r, "000000000000000000000000be5422d15f39373eb0a97ff8c10fbd0e40e29338");
}

BOOST_AUTO_TEST_CASE(ContractABIType_string)
{
    auto hashImpl = std::make_shared<Keccak256Hash>();
    ContractABICodec ct(hashImpl);

    std::string x("Hello, world!");
    std::string r = *toHexString(ct.serialise(x));
    BOOST_CHECK_EQUAL(
        r, std::string("000000000000000000000000000000000000000000000000000000000000000d48656c6c6"
                       "f2c20776f726c642100000000000000000000000000000000000000"));

    std::string y("");
    r = *toHexString(ct.serialise(y));
    BOOST_CHECK_EQUAL(
        r, std::string("0000000000000000000000000000000000000000000000000000000000000000"));
}

BOOST_AUTO_TEST_CASE(ContractABIType_array_uint256)
{
    auto hashImpl = std::make_shared<Keccak256Hash>();
    ContractABICodec ct(hashImpl);

    std::array<u256, 3> x{1, 2, 3};
    std::string r = *toHexString(ct.serialise(x));
    BOOST_CHECK_EQUAL(
        r, std::string("00000000000000000000000000000000000000000000000000000000000000010"
                       "0000000000000000000000000"
                       "00000000000000000000000000000000000002000000000000000000000000000"
                       "0000000000000000000000000"
                       "000000000003"));

    std::vector<u256> y{1, 2, 3};
    r = *toHexString(ct.serialise(y));
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
    auto hashImpl = std::make_shared<Keccak256Hash>();
    ContractABICodec ct(hashImpl);

    u256 a = 12345;
    s256 b = -67890;
    string c("xxxsxxxsxxs");
    string32 d = toString32(std::string("adsggsakjffl;kajsdf"));

    auto rb = ct.abiIn("", a, b, c, d);
    auto r = *toHexString(rb);

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

    auto hashImpl = std::make_shared<Keccak256Hash>();
    ContractABICodec ct(hashImpl);
    auto rb = ct.abiIn("", a, b, c, d, e);
    auto r = *toHexString(rb);
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

    auto hashImpl = std::make_shared<Keccak256Hash>();
    ContractABICodec ct(hashImpl);
    auto rb = ct.abiIn("", a);
    auto r = *toHexString(rb);

    BOOST_CHECK(r == expect);

    std::array<std::vector<u256>, 3> outA;
    bool Ok = ct.abiOutHex(r, outA);

    BOOST_CHECK_EQUAL(Ok, true);
    BOOST_CHECK(a == outA);
}

BOOST_AUTO_TEST_CASE(ContractABITest3)
{
    auto hashImpl = std::make_shared<Keccak256Hash>();
    ContractABICodec ct(hashImpl);
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
    auto r = *toHexString(rb);

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
    auto hashImpl = std::make_shared<Keccak256Hash>();
    ContractABICodec ct(hashImpl);
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
    auto hashImpl = std::make_shared<Keccak256Hash>();
    ContractABICodec ct(hashImpl);
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

BOOST_AUTO_TEST_CASE(testABIOutBytes)
{
    // test byte32
    std::string hashStr = "1ab21d8355cfa17f8e61194831e81a8f22bec8c728fefb747ed035eb5082aa2b";
    h256 hashData = h256(hashStr);
    std::string plainText = "test";
    bytes plainBytes = *fromHexString(*toHexString(plainText));
    ABIFunc afunc;
    auto ok = afunc.parser("f(bytes32,bytes,bytes32)");
    BOOST_CHECK(ok == true);
    auto allTypes = afunc.getParamsType();

    BOOST_CHECK(allTypes.size() == 3);
    BOOST_CHECK(allTypes[0] == "bytes32");
    BOOST_CHECK(allTypes[1] == "bytes");
    BOOST_CHECK(allTypes[2] == "bytes32");

    auto hashImpl = std::make_shared<Keccak256Hash>();
    ContractABICodec abi(hashImpl);
    auto paramData = abi.abiIn("", toString32(hashData), plainBytes, toString32(hashData));
    string32 decodedParam1;
    bytes decodedParam2;
    string32 decodedParam3;
    abi.abiOut(ref(paramData), decodedParam1, decodedParam2, decodedParam3);

    BOOST_CHECK(*toHexString(fromString32(decodedParam1)) == hashStr);
    BOOST_CHECK(*toHexString(fromString32(decodedParam3)) == hashStr);
    std::cout << "decodedParam1: " << *toHexString(decodedParam1) << std::endl;
    std::cout << "decodedParam2: " << *toHexString(decodedParam2) << std::endl;
    std::cout << "decodedParam3: " << *toHexString(decodedParam3) << std::endl;
    BOOST_CHECK(*toHexString(decodedParam2) == *toHexString(plainText));

    // test bytes
    plainText = "testabcxxd";
    bytesConstRef refPlainBytes(plainText);
    paramData = abi.abiIn("", refPlainBytes.toBytes());
    bytes decodedParam;
    abi.abiOut(ref(paramData), decodedParam);
    BOOST_CHECK(*toHexString(decodedParam) == *toHexString(plainText));
}

BOOST_AUTO_TEST_CASE(testABITuple)
{
    auto hashImpl = std::make_shared<Keccak256Hash>();
    ContractABICodec abi(hashImpl);
    // tuple(vector(tuple(u256,string,string,u256)))
    {
        auto tuple1 = std::make_tuple(u256(1), std::string("id1"), std::string("test1"), u256(2));
        auto tuple2 = std::make_tuple(u256(1), std::string("id2"), std::string("test2"), u256(2));
        auto testEncode = std::make_tuple(std::vector<decltype(tuple1)>{tuple1, tuple2});
        auto encodedBytes = abi.abiIn("", std::string("t_test"), testEncode);
        // solc 0.6.12
        // solidity struct A{ enum,string,string,enum } struct B {A[]}
        // B(A[]({1,"id1","test1",2},{1,"id2","test2",2}))
        std::string bytesString =
            "00000000000000000000000000000000000000000000000000000000000000400000000000000000000000"
            "0000"
            "00000000000000000000000000000000000080000000000000000000000000000000000000000000000000"
            "0000"
            "000000000006745f7465737400000000000000000000000000000000000000000000000000000000000000"
            "0000"
            "00000000000000000000000000000000000000000000000020000000000000000000000000000000000000"
            "0000"
            "00000000000000000000000200000000000000000000000000000000000000000000000000000000000000"
            "4000"
            "00000000000000000000000000000000000000000000000000000000000140000000000000000000000000"
            "0000"
            "00000000000000000000000000000000000100000000000000000000000000000000000000000000000000"
            "0000"
            "000000008000000000000000000000000000000000000000000000000000000000000000c0000000000000"
            "0000"
            "00000000000000000000000000000000000000000000000200000000000000000000000000000000000000"
            "0000"
            "00000000000000000000036964310000000000000000000000000000000000000000000000000000000000"
            "0000"
            "00000000000000000000000000000000000000000000000000000000000574657374310000000000000000"
            "0000"
            "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
            "0000"
            "00000001000000000000000000000000000000000000000000000000000000000000008000000000000000"
            "0000"
            "00000000000000000000000000000000000000000000c00000000000000000000000000000000000000000"
            "0000"
            "00000000000000000002000000000000000000000000000000000000000000000000000000000000000369"
            "6432"
            "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
            "0000"
            "00000000000000000000000000000005746573743200000000000000000000000000000000000000000000"
            "0000"
            "000000";
        std::string hexString = *toHexString(encodedBytes);
        BOOST_CHECK(hexString == bytesString);

        decltype(testEncode) testDecode;
        std::string testString;
        abi.abiOut(ref(encodedBytes), testString, testDecode);

        BOOST_CHECK(std::get<0>(testDecode).size() == 2);
        auto tupleDecode1 = std::get<0>(testDecode).at(0);
        auto tupleDecode2 = std::get<0>(testDecode).at(1);
        BOOST_CHECK(std::get<0>(tupleDecode1) == std::get<0>(tuple1));
        BOOST_CHECK(std::get<1>(tupleDecode1) == std::get<1>(tuple1));
        BOOST_CHECK(std::get<2>(tupleDecode1) == std::get<2>(tuple1));
        BOOST_CHECK(std::get<3>(tupleDecode1) == std::get<3>(tuple1));

        BOOST_CHECK(std::get<0>(tupleDecode2) == std::get<0>(tuple2));
        BOOST_CHECK(std::get<1>(tupleDecode2) == std::get<1>(tuple2));
        BOOST_CHECK(std::get<2>(tupleDecode2) == std::get<2>(tuple2));
        BOOST_CHECK(std::get<3>(tupleDecode2) == std::get<3>(tuple2));
    }

    // simple tuple
    {
        auto test1 = std::make_tuple(std::string("123"), s256(-1),
            Address("0x420f853b49838bd3e9466c85a4cc3428c960dde2"),
            *fromHexString("420f853b49838bd3e9466c85a4cc3428c960dde2"),
            std::vector<std::string>({"123", "456"}));
        auto test1Encode = abi.abiIn("", test1);
        decltype(test1) test1Decode;
        abi.abiOut(ref(test1Encode), test1Decode);

        BOOST_CHECK(std::get<0>(test1) == std::get<0>(test1Decode));
        BOOST_CHECK(std::get<1>(test1) == std::get<1>(test1Decode));
        BOOST_CHECK(std::get<2>(test1) == std::get<2>(test1Decode));
        BOOST_CHECK(std::get<3>(test1) == std::get<3>(test1Decode));
        BOOST_CHECK(std::get<4>(test1)[0] == std::get<4>(test1Decode)[0]);
        BOOST_CHECK(std::get<4>(test1)[1] == std::get<4>(test1Decode)[1]);
    }
    // tuple(u256,bool,vector(tuple(string,s256,Address,bytes,vector)),vector(tuple(string,s256,Address,bytes,vector)))
    {
        auto tuple1 = std::make_tuple(std::string("123"), s256(-1),
            Address("0x420f853b49838bd3e9466c85a4cc3428c960dde2"),
            *fromHexString("420f853b49838bd3e9466c85a4cc3428c960dde2"),
            std::vector<std::string>({"123", "456"}));

        auto tuple2 = std::make_tuple(std::string("456"), s256(-123),
            Address("0x420f853b49838bd3e9466c85a4cc3428c360dde2"),
            *fromHexString("420f853b49838bd3e9466c85a4cc3528c960dde2"),
            std::vector<std::string>({"321", "33333"}));

        std::vector<decltype(tuple1)> tupleV1{tuple1, tuple2};
        std::vector<decltype(tuple1)> tupleV2{tuple2, tuple1};

        auto tupleTest1 = std::make_tuple(u256(321321441), true, tupleV1, tupleV2);
        auto test1Encode = abi.abiIn("", tuple1, tupleTest1);
        decltype(tuple1) tuple1Decode;
        decltype(tupleTest1) test1Decode;
        abi.abiOut(ref(test1Encode), tuple1Decode, test1Decode);

        BOOST_CHECK(std::get<0>(tuple1Decode) == std::get<0>(tuple1));
        BOOST_CHECK(std::get<1>(tuple1Decode) == std::get<1>(tuple1));
        BOOST_CHECK(std::get<2>(tuple1Decode) == std::get<2>(tuple1));
        BOOST_CHECK(std::get<3>(tuple1Decode) == std::get<3>(tuple1));
        BOOST_CHECK(std::get<4>(tuple1Decode)[0] == std::get<4>(tuple1)[0]);
        BOOST_CHECK(std::get<4>(tuple1Decode)[1] == std::get<4>(tuple1)[1]);

        BOOST_CHECK(std::get<0>(test1Decode) == std::get<0>(tupleTest1));
        BOOST_CHECK(std::get<1>(test1Decode) == std::get<1>(tupleTest1));
        auto test1DecodeTupleV1 = std::get<2>(test1Decode);
        auto test1DecodeTupleV2 = std::get<3>(test1Decode);

        BOOST_CHECK(std::get<0>(test1DecodeTupleV1[0]) == std::get<0>(tuple1));
        BOOST_CHECK(std::get<1>(test1DecodeTupleV1[0]) == std::get<1>(tuple1));
        BOOST_CHECK(std::get<2>(test1DecodeTupleV1[0]) == std::get<2>(tuple1));
        BOOST_CHECK(std::get<3>(test1DecodeTupleV1[0]) == std::get<3>(tuple1));
        BOOST_CHECK(std::get<4>(test1DecodeTupleV1[0])[0] == std::get<4>(tuple1)[0]);
        BOOST_CHECK(std::get<4>(test1DecodeTupleV1[0])[1] == std::get<4>(tuple1)[1]);

        BOOST_CHECK(std::get<0>(test1DecodeTupleV1[1]) == std::get<0>(tuple2));
        BOOST_CHECK(std::get<1>(test1DecodeTupleV1[1]) == std::get<1>(tuple2));
        BOOST_CHECK(std::get<2>(test1DecodeTupleV1[1]) == std::get<2>(tuple2));
        BOOST_CHECK(std::get<3>(test1DecodeTupleV1[1]) == std::get<3>(tuple2));
        BOOST_CHECK(std::get<4>(test1DecodeTupleV1[1])[0] == std::get<4>(tuple2)[0]);
        BOOST_CHECK(std::get<4>(test1DecodeTupleV1[1])[1] == std::get<4>(tuple2)[1]);

        BOOST_CHECK(std::get<0>(test1DecodeTupleV2[0]) == std::get<0>(tuple2));
        BOOST_CHECK(std::get<1>(test1DecodeTupleV2[0]) == std::get<1>(tuple2));
        BOOST_CHECK(std::get<2>(test1DecodeTupleV2[0]) == std::get<2>(tuple2));
        BOOST_CHECK(std::get<3>(test1DecodeTupleV2[0]) == std::get<3>(tuple2));
        BOOST_CHECK(std::get<4>(test1DecodeTupleV2[0])[0] == std::get<4>(tuple2)[0]);
        BOOST_CHECK(std::get<4>(test1DecodeTupleV2[0])[1] == std::get<4>(tuple2)[1]);

        BOOST_CHECK(std::get<0>(test1DecodeTupleV2[1]) == std::get<0>(tuple1));
        BOOST_CHECK(std::get<1>(test1DecodeTupleV2[1]) == std::get<1>(tuple1));
        BOOST_CHECK(std::get<2>(test1DecodeTupleV2[1]) == std::get<2>(tuple1));
        BOOST_CHECK(std::get<3>(test1DecodeTupleV2[1]) == std::get<3>(tuple1));
        BOOST_CHECK(std::get<4>(test1DecodeTupleV2[1])[0] == std::get<4>(tuple1)[0]);
        BOOST_CHECK(std::get<4>(test1DecodeTupleV2[1])[1] == std::get<4>(tuple1)[1]);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos