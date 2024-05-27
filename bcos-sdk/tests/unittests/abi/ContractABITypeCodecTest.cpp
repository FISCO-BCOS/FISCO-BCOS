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
 * @file ContractABITypeCodec.cpp
 * @author: octopus
 * @date 2022-05-30
 */

#include <bcos-cpp-sdk/utilities/abi/ContractABIType.h>
#include <bcos-cpp-sdk/utilities/abi/ContractABITypeCodec.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/FixedBytes.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/tools/old/interface.hpp>
#include <stdexcept>
#include <utility>

using namespace bcos;
using namespace bcos::cppsdk;
using namespace bcos::cppsdk::abi;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(ContractABITypeCodecTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(test_solBaseTypeABIType)
{
    ContractABITypeCodecSolImpl codec;
    {  // uint
        u256 u0(1);
        bcos::bytes buffer;

        codec.serialize(u0, 256, buffer);
        BOOST_CHECK_EQUAL("0000000000000000000000000000000000000000000000000000000000000001",
            *toHexString(buffer));

        u256 u00 = 0;
        codec.deserialize(u00, buffer, 0);
        BOOST_CHECK_EQUAL(u0, u00);

        u256 u1(1111111111111111111);
        buffer.clear();
        // uint
        codec.serialize(u1, 256, buffer);
        BOOST_CHECK_EQUAL("0000000000000000000000000000000000000000000000000f6b75ab2bc471c7",
            *toHexString(buffer));

        u256 u10 = 0;
        codec.deserialize(u10, buffer, 0);
        BOOST_CHECK_EQUAL(u1, u10);

        u256 u2(-1);
        buffer.clear();
        // uint
        codec.serialize(u2, 256, buffer);
        BOOST_CHECK_EQUAL("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
            *toHexString(buffer));

        u256 u20 = 0;
        codec.deserialize(u20, buffer, 0);
        BOOST_CHECK_EQUAL(u2, u20);
    }

    {  // int
        s256 s0(1);
        bcos::bytes buffer;
        // int
        codec.serialize(s0, 256, buffer);
        BOOST_CHECK_EQUAL("0000000000000000000000000000000000000000000000000000000000000001",
            *toHexString(buffer));

        s256 s00 = 0;
        codec.deserialize(s00, buffer, 0);
        BOOST_CHECK_EQUAL(s0, s00);

        s256 s1(-1);
        buffer.clear();
        // int
        codec.serialize(s1, 256, buffer);
        BOOST_CHECK_EQUAL("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
            *toHexString(buffer));

        s256 s10 = 0;
        codec.deserialize(s10, buffer, 0);
        BOOST_CHECK_EQUAL(s1, s10);

        s256 s2("0x7fffffffffffffff");
        buffer.clear();
        // int
        codec.serialize(s2, 2565, buffer);
        BOOST_CHECK_EQUAL("0000000000000000000000000000000000000000000000007fffffffffffffff",
            *toHexString(buffer));

        s256 s20 = 0;
        codec.deserialize(s20, buffer, 0);
        BOOST_CHECK_EQUAL(s2, s20);

        s256 s3(-1111111111111111111);
        buffer.clear();
        // int
        codec.serialize(s3, 256, buffer);
        BOOST_CHECK_EQUAL("fffffffffffffffffffffffffffffffffffffffffffffffff0948a54d43b8e39",
            *toHexString(buffer));

        s256 s30 = 0;
        codec.deserialize(s30, buffer, 0);
        BOOST_CHECK_EQUAL(s3, s30);
    }

    {  // bool
        bool b0(true);
        bcos::bytes buffer;
        codec.serialize(b0, buffer);
        BOOST_CHECK_EQUAL("0000000000000000000000000000000000000000000000000000000000000001",
            *toHexString(buffer));

        bool b00;
        codec.deserialize(b00, buffer, 0);
        BOOST_CHECK_EQUAL(b0, b00);

        buffer.clear();
        bool b1(false);
        codec.serialize(b1, buffer);
        BOOST_CHECK_EQUAL("0000000000000000000000000000000000000000000000000000000000000000",
            *toHexString(buffer));

        bool b10;
        codec.deserialize(b10, buffer, 0);
        BOOST_CHECK_EQUAL(b1, b10);
    }

    {  // address
        Address addr0("0xbe5422d15f39373eb0a97ff8c10fbd0e40e29338");
        bcos::bytes buffer;
        codec.serialize(addr0, buffer);
        BOOST_CHECK_EQUAL("000000000000000000000000be5422d15f39373eb0a97ff8c10fbd0e40e29338",
            *toHexString(buffer));

        Address addr00;
        codec.deserialize(addr00, buffer, 0);
        BOOST_CHECK_EQUAL(addr0, addr00);

        buffer.clear();
        // address
        Address addr1("0x0");
        codec.serialize(addr1, buffer);
        BOOST_CHECK_EQUAL("0000000000000000000000000000000000000000000000000000000000000000",
            *toHexString(buffer));

        Address addr10;
        codec.deserialize(addr10, buffer, 0);
        BOOST_CHECK_EQUAL(addr1, addr10);
    }

    {
        // bytesN
        bcos::bytes bs0{0, 1, 2, 3, 4, 5};
        bcos::bytes buffer;
        codec.serialize(bs0, true, buffer);

        BOOST_CHECK_EQUAL("0001020304050000000000000000000000000000000000000000000000000000",
            *toHexString(buffer));

        bcos::bytes bs00;
        codec.deserialize(bs00, buffer, 0, 6);
        BOOST_CHECK_EQUAL(*toHexString(bs0), *toHexString(bs00));

        buffer.clear();

        bcos::bytes bs1{0};
        codec.serialize(bs1, true, buffer);
        BOOST_CHECK_EQUAL("0000000000000000000000000000000000000000000000000000000000000000",
            *toHexString(buffer));

        bcos::bytes bs10;
        codec.deserialize(bs10, buffer, 0, 1);
        BOOST_CHECK_EQUAL(*toHexString(bs1), *toHexString(bs10));

        buffer.clear();

        std::string s = "dave";

        bcos::bytes bs2(s.begin(), s.end());
        codec.serialize(bs2, true, buffer);
        BOOST_CHECK_EQUAL("6461766500000000000000000000000000000000000000000000000000000000",
            *toHexString(buffer));

        bcos::bytes bs20;
        codec.deserialize(bs20, buffer, 0, 4);
        BOOST_CHECK_EQUAL(*toHexString(bs2), *toHexString(bs20));

        std::string s1 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        bcos::bytes bs3(s1.begin(), s1.end());
        BOOST_CHECK_THROW(codec.serialize(bs3, true, buffer), std::runtime_error);
    }

    {
        // bytes
        bcos::bytes bs0(*fromHexString("692a70d2e424a56d2c6c27aa97d1a86395877b3a"));
        bcos::bytes buffer;
        codec.serialize(bs0, buffer);
        BOOST_CHECK_EQUAL(
            "0000000000000000000000000000000000000000000000000000000000000014692a70d2e424a56d2c6c27"
            "aa97d1a86395877b3a000000000000000000000000",
            *toHexString(buffer));

        bcos::bytes bs00;
        codec.deserialize(bs00, buffer, 0);
        BOOST_CHECK_EQUAL(*toHexString(bs0), *toHexString(bs00));

        buffer.clear();

        std::string s =
            "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor "
            "incididunt "
            "ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation "
            "ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in "
            "reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. "
            "Excepteur "
            "sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim "
            "id "
            "est laborum.";

        bcos::bytes bs1(s.begin(), s.end());
        codec.serialize(bs1, buffer);
        BOOST_CHECK_EQUAL(
            "00000000000000000000000000000000000000000000000000000000000001bd4c6f72656d20697073756d"
            "20646f6c6f722073697420616d65742c20636f6e73656374657475722061646970697363696e6720656c69"
            "742c2073656420646f20656975736d6f642074656d706f7220696e6369646964756e74207574206c61626f"
            "726520657420646f6c6f7265206d61676e6120616c697175612e20557420656e696d206164206d696e696d"
            "2076656e69616d2c2071756973206e6f737472756420657865726369746174696f6e20756c6c616d636f20"
            "6c61626f726973206e69736920757420616c697175697020657820656120636f6d6d6f646f20636f6e7365"
            "717561742e2044756973206175746520697275726520646f6c6f7220696e20726570726568656e64657269"
            "7420696e20766f6c7570746174652076656c697420657373652063696c6c756d20646f6c6f726520657520"
            "667567696174206e756c6c612070617269617475722e204578636570746575722073696e74206f63636165"
            "63617420637570696461746174206e6f6e2070726f6964656e742c2073756e7420696e2063756c70612071"
            "7569206f666669636961206465736572756e74206d6f6c6c697420616e696d20696420657374206c61626f"
            "72756d2e000000",
            *toHexString(buffer));

        bcos::bytes bs10;
        codec.deserialize(bs10, buffer, 0);
        BOOST_CHECK_EQUAL(*toHexString(bs1), *toHexString(bs10));

        buffer.clear();

        bcos::bytes bs2{0, 1, 2, 3, 4, 5};
        codec.serialize(bs2, buffer);
        BOOST_CHECK_EQUAL(
            "0000000000000000000000000000000000000000000000000000000000000006"
            "0001020304050000000000000000000000000000000000000000000000000000",
            *toHexString(buffer));

        bcos::bytes bs20;
        codec.deserialize(bs20, buffer, 0);
        BOOST_CHECK_EQUAL(*toHexString(bs2), *toHexString(bs20));
    }

    {
        // string
        std::string s0("dave");
        bcos::bytes buffer;
        codec.serialize(s0, buffer);
        BOOST_CHECK_EQUAL(
            "00000000000000000000000000000000000000000000000000000000000000046461766500000000000000"
            "000000000000000000000000000000000000000000",
            *toHexString(buffer));

        std::string s00;
        codec.deserialize(s00, buffer, 0);
        BOOST_CHECK_EQUAL(s0, s00);

        buffer.clear();

        std::string s1;
        codec.serialize(s1, buffer);
        BOOST_CHECK_EQUAL("0000000000000000000000000000000000000000000000000000000000000000",
            *toHexString(buffer));

        std::string s10;
        codec.deserialize(s10, buffer, 0);
        BOOST_CHECK_EQUAL(s1, s10);

        buffer.clear();

        std::string s2 =
            "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor "
            "incididunt "
            "ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation "
            "ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in "
            "reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. "
            "Excepteur "
            "sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim "
            "id "
            "est laborum.";
        codec.serialize(s2, buffer);
        BOOST_CHECK_EQUAL(
            "00000000000000000000000000000000000000000000000000000000000001bd4c6f72656d20697073756d"
            "20646f6c6f722073697420616d65742c20636f6e73656374657475722061646970697363696e6720656c69"
            "742c2073656420646f20656975736d6f642074656d706f7220696e6369646964756e74207574206c61626f"
            "726520657420646f6c6f7265206d61676e6120616c697175612e20557420656e696d206164206d696e696d"
            "2076656e69616d2c2071756973206e6f737472756420657865726369746174696f6e20756c6c616d636f20"
            "6c61626f726973206e69736920757420616c697175697020657820656120636f6d6d6f646f20636f6e7365"
            "717561742e2044756973206175746520697275726520646f6c6f7220696e20726570726568656e64657269"
            "7420696e20766f6c7570746174652076656c697420657373652063696c6c756d20646f6c6f726520657520"
            "667567696174206e756c6c612070617269617475722e204578636570746575722073696e74206f63636165"
            "63617420637570696461746174206e6f6e2070726f6964656e742c2073756e7420696e2063756c70612071"
            "7569206f666669636961206465736572756e74206d6f6c6c697420616e696d20696420657374206c61626f"
            "72756d2e000000",
            *toHexString(buffer));

        std::string s20;
        codec.deserialize(s20, buffer, 0);
        BOOST_CHECK_EQUAL(s2, s20);
    }
}

BOOST_AUTO_TEST_CASE(test_abstractType)
{
    ContractABITypeCodecSolImpl codec;
    {
        // AbstractType
        {
            // (string)

            std::string s = "Greetings!";

            bcos::bytes buffer;
            auto sPtr = Struct::newValue();
            auto str = String::newValue(s);
            sPtr->addMember(std::move(str));
            codec.serialize(*sPtr, buffer);
            BOOST_CHECK_EQUAL(
                "0000000000000000000000000000000000000000000000000000000000000020"
                "000000000000000000000000000000000000000000000000000000000000000a"
                "4772656574696e67732100000000000000000000000000000000000000000000",
                *toHexString(buffer));

            auto sPtr0 = sPtr->clone();
            sPtr0->clear();

            codec.deserialize(*sPtr0, buffer, 0);
            BOOST_CHECK_EQUAL(sPtr->toJsonString(), sPtr0->toJsonString());
        }

        {
            //(uint,bool)
            bcos::bytes buffer;
            auto sPtr = Struct::newValue();

            // uint 69
            auto u = Uint::newValue(u256(69));
            sPtr->addMember(std::move(u));

            // bool true
            auto b = Boolean::newValue(true);
            sPtr->addMember(std::move(b));

            codec.serialize(*sPtr, buffer);

            BOOST_CHECK_EQUAL(
                "0000000000000000000000000000000000000000000000000000000000000045"
                "0000000000000000000000000000000000000000000000000000000000000001",
                *toHexString(buffer));

            auto sPtr0 = sPtr->clone();
            sPtr0->clear();

            codec.deserialize(*sPtr0, buffer, 0);
            BOOST_CHECK_EQUAL(sPtr->toJsonString(), sPtr0->toJsonString());
        }


        {
            //(byte3,byte3)

            std::string s0{"abc"};
            std::string s1{"def"};

            bcos::bytes buffer;
            auto sPtr = Struct::newValue();

            // byte3 abc
            auto bs0 =
                bcos::cppsdk::abi::FixedBytes::newValue(3, bcos::bytes{s0.begin(), s0.end()});
            sPtr->addMember(std::move(bs0));

            // byte3 def
            auto bs1 =
                bcos::cppsdk::abi::FixedBytes::newValue(3, bcos::bytes{s1.begin(), s1.end()});
            sPtr->addMember(std::move(bs1));

            codec.serialize(*sPtr, buffer);

            BOOST_CHECK_EQUAL(
                "6162630000000000000000000000000000000000000000000000000000000000"
                "6465660000000000000000000000000000000000000000000000000000000000",
                *toHexString(buffer));

            auto sPtr0 = sPtr->clone();
            sPtr0->clear();

            codec.deserialize(*sPtr0, buffer, 0);
            BOOST_CHECK_EQUAL(sPtr->toJsonString(), sPtr0->toJsonString());
        }

        {
            // (bytes,bool,uint[])
            std::string s{"dave"};

            bcos::bytes buffer;
            auto sPtr = Struct::newValue();

            // bytes
            auto bs = bcos::cppsdk::abi::DynamicBytes::newValue(bcos::bytes{s.begin(), s.end()});
            sPtr->addMember(std::move(bs));

            // bool
            auto b = bcos::cppsdk::abi::Boolean::newValue(true);
            sPtr->addMember(std::move(b));

            // uint[]: 1,2,3
            auto uintList = bcos::cppsdk::abi::DynamicList::newValue();

            auto u0 = bcos::cppsdk::abi::Uint::newValue(u256(1));
            auto u1 = bcos::cppsdk::abi::Uint::newValue(u256(2));
            auto u2 = bcos::cppsdk::abi::Uint::newValue(u256(3));

            uintList->addMember(std::move(u0));
            uintList->addMember(std::move(u1));
            uintList->addMember(std::move(u2));

            sPtr->addMember(std::move(uintList));

            codec.serialize(*sPtr, buffer);

            BOOST_CHECK_EQUAL(
                "0000000000000000000000000000000000000000000000000000000000000060"
                "0000000000000000000000000000000000000000000000000000000000000001"
                "00000000000000000000000000000000000000000000000000000000000000a0"
                "0000000000000000000000000000000000000000000000000000000000000004"
                "6461766500000000000000000000000000000000000000000000000000000000"
                "0000000000000000000000000000000000000000000000000000000000000003"
                "0000000000000000000000000000000000000000000000000000000000000001"
                "0000000000000000000000000000000000000000000000000000000000000002"
                "0000000000000000000000000000000000000000000000000000000000000003",
                *toHexString(buffer));

            auto sPtr0 = sPtr->clone();
            sPtr0->clear();

            codec.deserialize(*sPtr0, buffer, 0);
            BOOST_CHECK_EQUAL(sPtr->toJsonString(), sPtr0->toJsonString());
        }

        {
            // uint[]
            auto dyList = DynamicList::newValue();
            bcos::bytes buffer;

            codec.serialize(*dyList, buffer);

            BOOST_CHECK_EQUAL("0000000000000000000000000000000000000000000000000000000000000000",
                *toHexString(buffer));

            auto sPtr0 = dyList->clone();
            sPtr0->clear();

            codec.deserialize(*dyList, buffer, 0);
            BOOST_CHECK_EQUAL(dyList->toJsonString(), sPtr0->toJsonString());
        }

        {
            // uint,uint[],byte10,bytes,
            bcos::bytes buffer;

            auto sPtr = Struct::newValue();

            // uint
            auto u = bcos::cppsdk::abi::Uint::newValue(u256(0x123));

            sPtr->addMember(std::move(u));

            // uint[]: 1,2,3
            auto uintList = bcos::cppsdk::abi::DynamicList::newValue();

            auto u0 = bcos::cppsdk::abi::Uint::newValue(u256(0x456));
            auto u1 = bcos::cppsdk::abi::Uint::newValue(u256(0x789));

            uintList->addMember(std::move(u0));
            uintList->addMember(std::move(u1));

            sPtr->addMember(std::move(uintList));

            // bytes10
            std::string s10{"1234567890"};
            auto bs10 =
                bcos::cppsdk::abi::FixedBytes::newValue(10, bcos::bytes{s10.begin(), s10.end()});

            sPtr->addMember(std::move(bs10));

            // bytes
            std::string s{"Hello, world!"};
            auto bs = bcos::cppsdk::abi::DynamicBytes::newValue(bcos::bytes{s.begin(), s.end()});
            sPtr->addMember(std::move(bs));

            codec.serialize(*sPtr, buffer);

            BOOST_CHECK_EQUAL(
                "0000000000000000000000000000000000000000000000000000000000000123000000000000000000"
                "0000000000000000000000000000000000000000000080313233343536373839300000000000000000"
                "0000000000000000000000000000000000000000000000000000000000000000000000000000000000"
                "00000000e0000000000000000000000000000000000000000000000000000000000000000200000000"
                "0000000000000000000000000000000000000000000000000000045600000000000000000000000000"
                "0000000000000000000000000000000000078900000000000000000000000000000000000000000000"
                "0000000000000000000d48656c6c6f2c20776f726c6421000000000000000000000000000000000000"
                "00",
                *toHexString(buffer));

            auto sPtr0 = sPtr->clone();
            sPtr0->clear();

            codec.deserialize(*sPtr0, buffer, 0);
            BOOST_CHECK_EQUAL(sPtr->toJsonString(), sPtr0->toJsonString());
        }

        {
            // uint,string[]
            bcos::bytes buffer;

            auto sPtr = Struct::newValue();

            // uint[][]:
            auto uintList = bcos::cppsdk::abi::DynamicList::newValue();

            auto uint0List = bcos::cppsdk::abi::DynamicList::newValue();
            auto u00 = bcos::cppsdk::abi::Uint::newValue(u256(1));
            auto u01 = bcos::cppsdk::abi::Uint::newValue(u256(2));
            uint0List->addMember(std::move(u00));
            uint0List->addMember(std::move(u01));

            auto uint1List = bcos::cppsdk::abi::DynamicList::newValue();
            auto u10 = bcos::cppsdk::abi::Uint::newValue(u256(3));
            uint1List->addMember(std::move(u10));

            uintList->addMember(std::move(uint0List));
            uintList->addMember(std::move(uint1List));

            sPtr->addMember(std::move(uintList));

            // string[]: one two three
            auto strList = bcos::cppsdk::abi::DynamicList::newValue();

            auto s0 = bcos::cppsdk::abi::String::newValue("one");
            auto s1 = bcos::cppsdk::abi::String::newValue("two");
            auto s2 = bcos::cppsdk::abi::String::newValue("three");

            strList->addMember(std::move(s0));
            strList->addMember(std::move(s1));
            strList->addMember(std::move(s2));

            sPtr->addMember(std::move(strList));

            codec.serialize(*sPtr, buffer);

            BOOST_CHECK_EQUAL(
                "0000000000000000000000000000000000000000000000000000000000000040"
                "0000000000000000000000000000000000000000000000000000000000000140"
                "0000000000000000000000000000000000000000000000000000000000000002"
                "0000000000000000000000000000000000000000000000000000000000000040"
                "00000000000000000000000000000000000000000000000000000000000000a0"
                "0000000000000000000000000000000000000000000000000000000000000002"
                "0000000000000000000000000000000000000000000000000000000000000001"
                "0000000000000000000000000000000000000000000000000000000000000002"
                "0000000000000000000000000000000000000000000000000000000000000001"
                "0000000000000000000000000000000000000000000000000000000000000003"
                "0000000000000000000000000000000000000000000000000000000000000003"
                "0000000000000000000000000000000000000000000000000000000000000060"
                "00000000000000000000000000000000000000000000000000000000000000a0"
                "00000000000000000000000000000000000000000000000000000000000000e0"
                "0000000000000000000000000000000000000000000000000000000000000003"
                "6f6e650000000000000000000000000000000000000000000000000000000000"
                "0000000000000000000000000000000000000000000000000000000000000003"
                "74776f0000000000000000000000000000000000000000000000000000000000"
                "0000000000000000000000000000000000000000000000000000000000000005"
                "7468726565000000000000000000000000000000000000000000000000000000",
                *toHexString(buffer));

            auto sPtr0 = sPtr->clone();
            sPtr0->clear();

            codec.deserialize(*sPtr0, buffer, 0);
            BOOST_CHECK_EQUAL(sPtr->toJsonString(), sPtr0->toJsonString());
        }

        {
            // int a, Info[] memory b, string memory c
            /*
                 "0": "uint256: a 100",
                 "1": "tuple(string,int256,tuple(int256,int256,int256)[])[]: b Hello
            world!,100,1,2,3,Hello world2!,200,5,6,7", "2": "string: c Hello world!"
                "2": Hello world!
                 */
            // uint,string[]
            bcos::bytes buffer;

            auto sPtr = Struct::newValue();

            auto dyList = bcos::cppsdk::abi::DynamicList::newValue();

            {
                auto item0 = Struct::newValue();

                auto s0 = bcos::cppsdk::abi::String::newValue("Hello world!");
                auto u0 = bcos::cppsdk::abi::Uint::newValue(u256(100));

                // tuple(int,int,int)[]
                auto dyList0 = DynamicList::newValue();

                // tuple {1, 2, 3}
                auto struct0 = Struct::newValue();

                auto u00 = bcos::cppsdk::abi::Uint::newValue(u256(1));
                auto u01 = bcos::cppsdk::abi::Uint::newValue(u256(2));
                auto u02 = bcos::cppsdk::abi::Uint::newValue(u256(3));

                struct0->value().push_back(std::move(u00));
                struct0->value().push_back(std::move(u01));
                struct0->value().push_back(std::move(u02));

                dyList0->addMember(std::move(struct0));

                item0->addMember(std::move(s0));
                item0->addMember(std::move(u0));
                item0->addMember(std::move(dyList0));

                dyList->addMember(std::move(item0));
            }

            {
                auto item0 = Struct::newValue();

                auto s0 = bcos::cppsdk::abi::String::newValue("Hello world2");
                auto u0 = bcos::cppsdk::abi::Uint::newValue(u256(200));


                // tuple(int,int,int)[]
                auto dyList0 = DynamicList::newValue();

                // tuple {1, 2, 3}
                auto struct0 = Struct::newValue();

                auto u00 = bcos::cppsdk::abi::Uint::newValue(u256(5));
                auto u01 = bcos::cppsdk::abi::Uint::newValue(u256(6));
                auto u02 = bcos::cppsdk::abi::Uint::newValue(u256(7));

                struct0->value().push_back(std::move(u00));
                struct0->value().push_back(std::move(u01));
                struct0->value().push_back(std::move(u02));

                dyList0->addMember(std::move(struct0));

                item0->addMember(std::move(s0));
                item0->addMember(std::move(u0));
                item0->addMember(std::move(dyList0));

                dyList->addMember(std::move(item0));
            }

            auto u = bcos::cppsdk::abi::Uint::newValue(u256(100));
            sPtr->addMember(std::move(u));

            sPtr->addMember(std::move(dyList));

            auto s = bcos::cppsdk::abi::String::newValue("Hello world!");
            sPtr->addMember(std::move(s));

            codec.serialize(*sPtr, buffer);

            BOOST_CHECK_EQUAL(
                "0000000000000000000000000000000000000000000000000000000000000064"
                "0000000000000000000000000000000000000000000000000000000000000060"
                "0000000000000000000000000000000000000000000000000000000000000300"
                "0000000000000000000000000000000000000000000000000000000000000002"
                "0000000000000000000000000000000000000000000000000000000000000040"
                "0000000000000000000000000000000000000000000000000000000000000160"
                "0000000000000000000000000000000000000000000000000000000000000060"
                "0000000000000000000000000000000000000000000000000000000000000064"
                "00000000000000000000000000000000000000000000000000000000000000a0"
                "000000000000000000000000000000000000000000000000000000000000000c"
                "48656c6c6f20776f726c64210000000000000000000000000000000000000000"
                "0000000000000000000000000000000000000000000000000000000000000001"
                "0000000000000000000000000000000000000000000000000000000000000001"
                "0000000000000000000000000000000000000000000000000000000000000002"
                "0000000000000000000000000000000000000000000000000000000000000003"
                "0000000000000000000000000000000000000000000000000000000000000060"
                "00000000000000000000000000000000000000000000000000000000000000c8"
                "00000000000000000000000000000000000000000000000000000000000000a0"
                "000000000000000000000000000000000000000000000000000000000000000c"
                "48656c6c6f20776f726c64320000000000000000000000000000000000000000"
                "0000000000000000000000000000000000000000000000000000000000000001"
                "0000000000000000000000000000000000000000000000000000000000000005"
                "0000000000000000000000000000000000000000000000000000000000000006"
                "0000000000000000000000000000000000000000000000000000000000000007"
                "000000000000000000000000000000000000000000000000000000000000000c"
                "48656c6c6f20776f726c64210000000000000000000000000000000000000000",
                *toHexString(buffer));

            auto sPtr0 = sPtr->clone();
            sPtr0->clear();

            codec.deserialize(*sPtr0, buffer, 0);
            BOOST_CHECK_EQUAL(sPtr->toJsonString(), sPtr0->toJsonString());
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
