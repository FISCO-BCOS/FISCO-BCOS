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
 * @file ContractABITypeTest.cpp
 * @author: octopus
 * @date 2022-05-30
 */

#include <bcos-cpp-sdk/utilities/abi/ContractABICodec.h>
#include <bcos-cpp-sdk/utilities/abi/ContractABIType.h>
#include <bcos-cpp-sdk/utilities/abi/ContractABITypeCodec.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/FixedBytes.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/tools/old/interface.hpp>
#include <utility>

using namespace bcos;
using namespace bcos::cppsdk;
using namespace bcos::cppsdk::abi;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(ContractABITypeTest, TestPromptFixture)


BOOST_AUTO_TEST_CASE(test_contractABIType)
{
    {  // uint
        u256 u(111);
        auto value = Uint::newValue(u);
        value->setName("u");

        BOOST_CHECK(!value->dynamicType());  // false
        BOOST_CHECK(value->type() == Type::UINT);
        // BOOST_CHECK(value->name());
        BOOST_CHECK_EQUAL(value->offsetAsBytes(), MAX_BYTE_LENGTH);
        BOOST_CHECK_EQUAL(value->value(), u);
        BOOST_CHECK_EQUAL(value->toJson().asString(), "111");

        // clone test
        auto cloneValuePtr = value->clone();
        u256 cloneU(12345);
        auto cloneValue =
            std::dynamic_pointer_cast<Uint>(AbstractType::Ptr(std::move(cloneValuePtr)));

        BOOST_CHECK(value->isEqual(*cloneValue));
        cloneValue->setValue(cloneU);
        BOOST_CHECK(!value->isEqual(*cloneValue));

        BOOST_CHECK_EQUAL(cloneValue->value(), cloneU);
        BOOST_CHECK(!cloneValue->dynamicType());  // false
        BOOST_CHECK(cloneValue->type() == Type::UINT);
        BOOST_CHECK_EQUAL(cloneValue->offsetAsBytes(), MAX_BYTE_LENGTH);
        BOOST_CHECK_EQUAL(cloneValue->value(), cloneU);
        BOOST_CHECK_EQUAL(cloneValue->name(), value->name());
        BOOST_CHECK_EQUAL(cloneValue->toJson().asString(), "12345");

        // old value compare
        BOOST_CHECK_EQUAL(value->value(), u);
        BOOST_CHECK_EQUAL(value->toJson().asString(), "111");
    }

    {
        // int
        s256 s(-111);
        auto value = Int::newValue(s);
        BOOST_CHECK(!value->dynamicType());  // false
        BOOST_CHECK(value->type() == Type::INT);
        // BOOST_CHECK(value->name());
        BOOST_CHECK_EQUAL(value->offsetAsBytes(), MAX_BYTE_LENGTH);
        BOOST_CHECK_EQUAL(value->value(), s);
        BOOST_CHECK_EQUAL(value->toJson().asString(), "-111");

        // clone test
        auto cloneValuePtr = value->clone();
        s256 cloneU(-1111111111111);
        auto cloneValue =
            std::dynamic_pointer_cast<Int>(AbstractType::Ptr(std::move(cloneValuePtr)));

        BOOST_CHECK(value->isEqual(*cloneValue));
        cloneValue->setValue(cloneU);
        BOOST_CHECK(!value->isEqual(*cloneValue));

        BOOST_CHECK_EQUAL(cloneValue->value(), cloneU);
        BOOST_CHECK(!cloneValue->dynamicType());  // false
        BOOST_CHECK(cloneValue->type() == Type::INT);
        BOOST_CHECK_EQUAL(cloneValue->offsetAsBytes(), MAX_BYTE_LENGTH);
        BOOST_CHECK_EQUAL(cloneValue->value(), cloneU);
        BOOST_CHECK_EQUAL(cloneValue->name(), value->name());
        BOOST_CHECK_EQUAL(cloneValue->toJson().asString(), "-1111111111111");

        // old value compare
        BOOST_CHECK_EQUAL(value->value(), s);
        BOOST_CHECK_EQUAL(value->toJson().asString(), "-111");
    }

    {
        // Boolean
        bool b(true);
        auto value = Boolean::newValue(b);
        BOOST_CHECK(!value->dynamicType());  // false
        BOOST_CHECK(value->type() == Type::BOOL);
        // BOOST_CHECK(value->name());
        BOOST_CHECK_EQUAL(value->offsetAsBytes(), MAX_BYTE_LENGTH);
        BOOST_CHECK_EQUAL(value->value(), b);
        BOOST_CHECK_EQUAL(value->toJson().asBool(), true);

        // clone test
        auto cloneValuePtr = value->clone();
        bool cloneU(false);
        auto cloneValue =
            std::dynamic_pointer_cast<Boolean>(AbstractType::Ptr(std::move(cloneValuePtr)));


        BOOST_CHECK(value->isEqual(*cloneValue));
        cloneValue->setValue(cloneU);
        BOOST_CHECK(!value->isEqual(*cloneValue));

        BOOST_CHECK_EQUAL(cloneValue->value(), cloneU);
        BOOST_CHECK(!cloneValue->dynamicType());  // false
        BOOST_CHECK(cloneValue->type() == Type::BOOL);
        BOOST_CHECK_EQUAL(cloneValue->offsetAsBytes(), MAX_BYTE_LENGTH);
        BOOST_CHECK_EQUAL(cloneValue->value(), cloneU);
        BOOST_CHECK_EQUAL(cloneValue->name(), value->name());
        BOOST_CHECK_EQUAL(cloneValue->toJson().asBool(), false);

        // old value compare
        BOOST_CHECK_EQUAL(value->value(), b);
        BOOST_CHECK_EQUAL(value->toJson().asBool(), true);
    }

    {
        // DynamicBytes
        bcos::bytes bs{h256(2222).asBytes()};
        auto value = DynamicBytes::newValue(bs);
        BOOST_CHECK(value->dynamicType());  // false
        BOOST_CHECK(value->type() == Type::DYNAMIC_BYTES);
        // BOOST_CHECK(value->name());
        BOOST_CHECK_EQUAL(value->offsetAsBytes(), MAX_BYTE_LENGTH);
        BOOST_CHECK(value->value() == bs);
        BOOST_CHECK_EQUAL(value->toJson().asString(), "hex://" + *toHexString(bs));

        // clone test
        auto cloneValuePtr = value->clone();
        bcos::bytes cloneU{h256(3333).asBytes()};
        auto cloneValue =
            std::dynamic_pointer_cast<DynamicBytes>(AbstractType::Ptr(std::move(cloneValuePtr)));

        BOOST_CHECK(value->isEqual(*cloneValue));
        cloneValue->setValue(cloneU);
        BOOST_CHECK(!value->isEqual(*cloneValue));

        BOOST_CHECK_EQUAL(
            toHexStringWithPrefix(cloneValue->value()), toHexStringWithPrefix(cloneU));
        BOOST_CHECK(cloneValue->dynamicType());  // false
        BOOST_CHECK(cloneValue->type() == Type::DYNAMIC_BYTES);
        BOOST_CHECK_EQUAL(cloneValue->offsetAsBytes(), MAX_BYTE_LENGTH);
        BOOST_CHECK_EQUAL(
            toHexStringWithPrefix(cloneValue->value()), toHexStringWithPrefix(cloneU));
        BOOST_CHECK_EQUAL(cloneValue->name(), value->name());
        BOOST_CHECK_EQUAL(cloneValue->toJson().asString(), "hex://" + *toHexString(cloneU));

        // old value compare
        BOOST_CHECK_EQUAL(toHexStringWithPrefix(value->value()), toHexStringWithPrefix(bs));
        BOOST_CHECK_EQUAL(value->toJson().asString(), "hex://" + *toHexString(bs));
    }

    {
        // FixedBytes
        bcos::bytes bs{h160(2222).asBytes()};
        auto value = bcos::cppsdk::abi::FixedBytes::newValue(20, bs);
        BOOST_CHECK(!value->dynamicType());  // false
        BOOST_CHECK(value->type() == Type::FIXED_BYTES);
        // BOOST_CHECK(value->name());
        BOOST_CHECK_EQUAL(value->offsetAsBytes(), MAX_BYTE_LENGTH);
        BOOST_CHECK_EQUAL(toHexStringWithPrefix(value->value()), toHexStringWithPrefix(bs));
        BOOST_CHECK_EQUAL(value->toJson().asString(), "hex://" + *toHexString(bs));

        // clone test
        auto cloneValuePtr = value->clone();
        bcos::bytes cloneU{h160(3333).asBytes()};
        auto cloneValue = std::dynamic_pointer_cast<bcos::cppsdk::abi::FixedBytes>(
            AbstractType::Ptr(std::move(cloneValuePtr)));

        BOOST_CHECK(value->isEqual(*cloneValue));
        cloneValue->setValue(cloneU);
        BOOST_CHECK(!value->isEqual(*cloneValue));

        BOOST_CHECK_EQUAL(
            toHexStringWithPrefix(cloneValue->value()), toHexStringWithPrefix(cloneU));
        BOOST_CHECK(!cloneValue->dynamicType());  // false
        BOOST_CHECK(cloneValue->type() == Type::FIXED_BYTES);
        BOOST_CHECK_EQUAL(cloneValue->offsetAsBytes(), MAX_BYTE_LENGTH);
        BOOST_CHECK_EQUAL(
            toHexStringWithPrefix(cloneValue->value()), toHexStringWithPrefix(cloneU));
        BOOST_CHECK_EQUAL(cloneValue->name(), value->name());
        BOOST_CHECK_EQUAL(cloneValue->toJson().asString(), "hex://" + *toHexString(cloneU));

        // old value compare
        BOOST_CHECK_EQUAL(toHexStringWithPrefix(value->value()), toHexStringWithPrefix(bs));
        BOOST_CHECK_EQUAL(value->toJson().asString(), "hex://" + *toHexString(bs));
    }

    {
        // Addr
        std::string addr{"0x000000000000000000001"};
        auto value = bcos::cppsdk::abi::Addr::newValue(addr);
        BOOST_CHECK(!value->dynamicType());  // false
        BOOST_CHECK(value->type() == Type::ADDRESS);
        // BOOST_CHECK(value->name());
        BOOST_CHECK_EQUAL(value->offsetAsBytes(), MAX_BYTE_LENGTH);
        BOOST_CHECK_EQUAL(value->value(), addr);
        BOOST_CHECK_EQUAL(value->toJson().asString(), addr);

        // clone test
        auto cloneValuePtr = value->clone();
        std::string cloneU{"0x000000000000000000002"};
        auto cloneValue =
            std::dynamic_pointer_cast<Addr>(AbstractType::Ptr(std::move(cloneValuePtr)));

        BOOST_CHECK(value->isEqual(*cloneValue));
        cloneValue->setValue(cloneU);
        BOOST_CHECK(!value->isEqual(*cloneValue));

        BOOST_CHECK_EQUAL(cloneValue->value(), cloneU);
        BOOST_CHECK(!cloneValue->dynamicType());  // false
        BOOST_CHECK(cloneValue->type() == Type::ADDRESS);
        BOOST_CHECK_EQUAL(cloneValue->offsetAsBytes(), MAX_BYTE_LENGTH);
        BOOST_CHECK_EQUAL(cloneValue->value(), cloneU);
        BOOST_CHECK_EQUAL(cloneValue->name(), value->name());
        BOOST_CHECK_EQUAL(cloneValue->toJson().asString(), cloneU);

        // old value compare
        BOOST_CHECK_EQUAL(value->value(), addr);
        BOOST_CHECK_EQUAL(value->toJson().asString(), addr);
    }

    {
        // String
        std::string str = "Hello, fisco-bcos 3.0";
        auto value = bcos::cppsdk::abi::String::newValue(str);
        BOOST_CHECK(value->dynamicType());  // true
        BOOST_CHECK(value->type() == Type::STRING);
        // BOOST_CHECK(value->name());
        BOOST_CHECK_EQUAL(value->offsetAsBytes(), MAX_BYTE_LENGTH);
        BOOST_CHECK_EQUAL(value->value(), str);
        BOOST_CHECK_EQUAL(value->toJson().asString(), str);

        // clone test
        auto cloneValuePtr = value->clone();
        std::string cloneU{"0x000000000000000000002"};
        auto cloneValue =
            std::dynamic_pointer_cast<String>(AbstractType::Ptr(std::move(cloneValuePtr)));

        BOOST_CHECK(value->isEqual(*cloneValue));
        cloneValue->setValue(cloneU);
        BOOST_CHECK(!value->isEqual(*cloneValue));

        BOOST_CHECK_EQUAL(cloneValue->value(), cloneU);
        BOOST_CHECK(cloneValue->dynamicType());  // false
        BOOST_CHECK(cloneValue->type() == Type::STRING);
        BOOST_CHECK_EQUAL(cloneValue->offsetAsBytes(), MAX_BYTE_LENGTH);
        BOOST_CHECK_EQUAL(cloneValue->value(), cloneU);
        BOOST_CHECK_EQUAL(cloneValue->name(), value->name());
        BOOST_CHECK_EQUAL(cloneValue->toJson().asString(), cloneU);

        // old value compare
        BOOST_CHECK_EQUAL(value->value(), str);
        BOOST_CHECK_EQUAL(value->toJson().asString(), str);
    }

    {  // FixedList int[4]

        uint dimension = 4;

        auto value = bcos::cppsdk::abi::FixedList::newValue(dimension);

        value->addMember(Int::newValue(0));
        value->addMember(Int::newValue(1));
        value->addMember(Int::newValue(2));
        value->addMember(Int::newValue(3));

        BOOST_CHECK(!value->dynamicType());  // false
        BOOST_CHECK(value->type() == Type::FIXED_LIST);
        // BOOST_CHECK(value->name());
        BOOST_CHECK_EQUAL(value->offsetAsBytes(), MAX_BYTE_LENGTH * 4);
        BOOST_CHECK_EQUAL(value->value().size(), 4);
        BOOST_CHECK_EQUAL(value->toJson().size(), 4);

        BOOST_CHECK_EQUAL(std::dynamic_pointer_cast<Int>(value->value()[0])->value(), s256(0));
        BOOST_CHECK_EQUAL(std::dynamic_pointer_cast<Int>(value->value()[1])->value(), s256(1));
        BOOST_CHECK_EQUAL(std::dynamic_pointer_cast<Int>(value->value()[2])->value(), s256(2));
        BOOST_CHECK_EQUAL(std::dynamic_pointer_cast<Int>(value->value()[3])->value(), s256(3));

        auto cloneValuePtr = value->clone();
        BOOST_CHECK(value->isEqual(*cloneValuePtr));
    }


    {  // FixedList string[4]

        uint dimension = 4;

        auto value = bcos::cppsdk::abi::FixedList::newValue(dimension);

        value->addMember(String::newValue("0"));
        value->addMember(String::newValue("1"));
        value->addMember(String::newValue("2"));
        value->addMember(String::newValue("3"));

        BOOST_CHECK(value->dynamicType());  // true
        BOOST_CHECK(value->type() == Type::FIXED_LIST);
        // BOOST_CHECK(value->name());
        BOOST_CHECK_EQUAL(value->offsetAsBytes(), MAX_BYTE_LENGTH);
        BOOST_CHECK_EQUAL(value->value().size(), 4);
        BOOST_CHECK_EQUAL(value->toJson().size(), 4);

        BOOST_CHECK_EQUAL(
            std::dynamic_pointer_cast<String>(value->value()[0])->value(), std::string("0"));
        BOOST_CHECK_EQUAL(
            std::dynamic_pointer_cast<String>(value->value()[1])->value(), std::string("1"));
        BOOST_CHECK_EQUAL(
            std::dynamic_pointer_cast<String>(value->value()[2])->value(), std::string("2"));
        BOOST_CHECK_EQUAL(
            std::dynamic_pointer_cast<String>(value->value()[3])->value(), std::string("3"));

        auto cloneValuePtr = value->clone();
        BOOST_CHECK(value->isEqual(*cloneValuePtr));
    }


    {  // DynamicList int[]
        auto value = bcos::cppsdk::abi::DynamicList::newValue();

        std::vector<s256> ss{-1, -2, -3, -4};

        value->addMember(Int::newValue(ss[0]));
        value->addMember(Int::newValue(ss[1]));
        value->addMember(Int::newValue(ss[2]));
        value->addMember(Int::newValue(ss[3]));

        BOOST_CHECK(value->dynamicType());  // true
        BOOST_CHECK(value->type() == Type::DYNAMIC_LIST);
        // BOOST_CHECK(value->name());
        BOOST_CHECK_EQUAL(value->offsetAsBytes(), MAX_BYTE_LENGTH);
        BOOST_CHECK_EQUAL(value->value().size(), 4);
        BOOST_CHECK_EQUAL(value->toJson().size(), 4);

        BOOST_CHECK_EQUAL(std::dynamic_pointer_cast<Int>(value->value()[0])->value(), ss[0]);
        BOOST_CHECK_EQUAL(std::dynamic_pointer_cast<Int>(value->value()[1])->value(), ss[1]);
        BOOST_CHECK_EQUAL(std::dynamic_pointer_cast<Int>(value->value()[2])->value(), ss[2]);
        BOOST_CHECK_EQUAL(std::dynamic_pointer_cast<Int>(value->value()[3])->value(), ss[3]);

        auto cloneValuePtr = value->clone();
        BOOST_CHECK(value->isEqual(*cloneValuePtr));
    }

    {  // DynamicList int[]
        auto value = bcos::cppsdk::abi::DynamicList::newValue();

        std::vector<s256> ss{-1, -2, -3, -4};

        value->addMember(Int::newValue(ss[0]));
        value->addMember(Int::newValue(ss[1]));
        value->addMember(Int::newValue(ss[2]));
        value->addMember(Int::newValue(ss[3]));

        BOOST_CHECK(value->dynamicType());  // true
        BOOST_CHECK(value->type() == Type::DYNAMIC_LIST);
        // BOOST_CHECK(value->name());
        BOOST_CHECK_EQUAL(value->offsetAsBytes(), MAX_BYTE_LENGTH);
        BOOST_CHECK_EQUAL(value->value().size(), 4);
        BOOST_CHECK_EQUAL(value->toJson().size(), 4);

        BOOST_CHECK_EQUAL(std::dynamic_pointer_cast<Int>(value->value()[0])->value(), ss[0]);
        BOOST_CHECK_EQUAL(std::dynamic_pointer_cast<Int>(value->value()[1])->value(), ss[1]);
        BOOST_CHECK_EQUAL(std::dynamic_pointer_cast<Int>(value->value()[2])->value(), ss[2]);
        BOOST_CHECK_EQUAL(std::dynamic_pointer_cast<Int>(value->value()[3])->value(), ss[3]);

        auto cloneValuePtr = value->clone();
        BOOST_CHECK(value->isEqual(*cloneValuePtr));
    }

    {
        {
            // Struct: (uint)
            auto value = bcos::cppsdk::abi::Struct::newValue();

            auto u = u256(111);
            value->addMember(Uint::newValue(u));

            BOOST_CHECK(!value->dynamicType());  // false
            BOOST_CHECK(value->type() == Type::STRUCT);
            // BOOST_CHECK(value->name());
            BOOST_CHECK_EQUAL(value->offsetAsBytes(), MAX_BYTE_LENGTH);
            BOOST_CHECK_EQUAL(value->value().size(), 1);
            BOOST_CHECK_EQUAL(value->toJson().size(), 1);

            // solidity
            ContractABITypeCodecSolImpl codec;
            bcos::bytes buffer;
            codec.serialize(*value, buffer);

            auto cloneStruct = value->clone();
            BOOST_CHECK(value->isEqual(*cloneStruct));

            cloneStruct->clear();
            codec.deserialize(*cloneStruct, buffer, 0);

            BOOST_CHECK(value->isEqual(*cloneStruct));
            BOOST_CHECK_EQUAL(value->toJsonString(), cloneStruct->toJsonString());
        }

        {
            // Struct: (string)
            auto value = bcos::cppsdk::abi::Struct::newValue();

            auto str = std::string("111");
            value->addMember(String::newValue(str));

            BOOST_CHECK(value->dynamicType());  // false
            BOOST_CHECK(value->type() == Type::STRUCT);
            // BOOST_CHECK(value->name());
            BOOST_CHECK_EQUAL(value->offsetAsBytes(), MAX_BYTE_LENGTH);
            BOOST_CHECK_EQUAL(value->value().size(), 1);
            BOOST_CHECK_EQUAL(value->toJson().size(), 1);

            // solidity
            ContractABITypeCodecSolImpl codec;
            bcos::bytes buffer;
            codec.serialize(*value, buffer);

            auto cloneStruct = value->clone();
            BOOST_CHECK(value->isEqual(*cloneStruct));

            cloneStruct->clear();
            codec.deserialize(*cloneStruct, buffer, 0);

            BOOST_CHECK(value->isEqual(*cloneStruct));
            BOOST_CHECK_EQUAL(value->toJsonString(), cloneStruct->toJsonString());
        }

        {
            // Struct: (uint,string)
            auto value = bcos::cppsdk::abi::Struct::newValue();

            auto u = u256(111);
            auto str = std::string("111");

            value->addMember(Uint::newValue(u));
            value->addMember(String::newValue(str));

            BOOST_CHECK(value->dynamicType());  // false
            BOOST_CHECK(value->type() == Type::STRUCT);
            // BOOST_CHECK(value->name());
            BOOST_CHECK_EQUAL(value->offsetAsBytes(), MAX_BYTE_LENGTH);
            BOOST_CHECK_EQUAL(value->value().size(), 2);
            BOOST_CHECK_EQUAL(value->toJson().size(), 2);

            auto cloneValuePtr = value->clone();
            BOOST_CHECK(value->isEqual(*cloneValuePtr));

            // solidity
            ContractABITypeCodecSolImpl codec;
            bcos::bytes buffer;
            codec.serialize(*value, buffer);

            auto cloneStruct = value->clone();
            BOOST_CHECK(value->isEqual(*cloneStruct));

            cloneStruct->clear();
            codec.deserialize(*cloneStruct, buffer, 0);

            BOOST_CHECK(value->isEqual(*cloneStruct));
            BOOST_CHECK_EQUAL(value->toJsonString(), cloneStruct->toJsonString());
        }

        {
            // Struct: (uint,int,bool,address,bytes32)
            auto value = bcos::cppsdk::abi::Struct::newValue();

            auto u = u256(111);
            auto s = s256(-111);
            auto b = false;
            auto addr = std::string("0x0000000000000000000000000000000000000011");
            bcos::bytes bs32{h256(2222).asBytes()};

            value->addMember(Uint::newValue(u));
            value->addMember(Int::newValue(s));
            value->addMember(Boolean::newValue(b));
            value->addMember(Addr::newValue(addr));
            value->addMember(bcos::cppsdk::abi::FixedBytes::newValue(32, bs32));

            BOOST_CHECK(!value->dynamicType());  // false
            BOOST_CHECK(value->type() == Type::STRUCT);
            // BOOST_CHECK(value->name());
            BOOST_CHECK_EQUAL(value->offsetAsBytes(), MAX_BYTE_LENGTH * 5);
            BOOST_CHECK_EQUAL(value->value().size(), 5);
            BOOST_CHECK_EQUAL(value->toJson().size(), 5);

            // solidity
            ContractABITypeCodecSolImpl codec;
            bcos::bytes buffer;
            codec.serialize(*value, buffer);

            auto cloneStruct = value->clone();
            BOOST_CHECK(value->isEqual(*cloneStruct));

            cloneStruct->clear();
            codec.deserialize(*cloneStruct, buffer, 0);

            BOOST_CHECK(value->isEqual(*cloneStruct));
            BOOST_CHECK_EQUAL(value->toJsonString(), cloneStruct->toJsonString());
        }

        {
            // Struct: (uint,int,bool,address,bytes20,string)
            auto value = bcos::cppsdk::abi::Struct::newValue();

            auto u = u256(111);
            auto s = s256(-111);
            auto b = false;
            auto addr = std::string("0x0000000000000000000000000000000000000011");
            bcos::bytes bs20{h160(2222).asBytes()};
            auto str = "12345";

            value->addMember(Uint::newValue(u));
            value->addMember(Int::newValue(s));
            value->addMember(Boolean::newValue(b));
            value->addMember(Addr::newValue(addr));
            value->addMember(bcos::cppsdk::abi::FixedBytes::newValue(20, bs20));
            value->addMember(bcos::cppsdk::abi::String::newValue(str));

            BOOST_CHECK(value->dynamicType());  // false
            BOOST_CHECK(value->type() == Type::STRUCT);
            // BOOST_CHECK(value->name());
            BOOST_CHECK_EQUAL(value->offsetAsBytes(), MAX_BYTE_LENGTH);
            BOOST_CHECK_EQUAL(value->value().size(), 6);
            BOOST_CHECK_EQUAL(value->toJson().size(), 6);

            auto cloneValuePtr = value->clone();
            BOOST_CHECK(value->isEqual(*cloneValuePtr));

            // solidity
            ContractABITypeCodecSolImpl codec;
            bcos::bytes buffer;
            codec.serialize(*value, buffer);

            auto cloneStruct = value->clone();
            BOOST_CHECK(value->isEqual(*cloneStruct));

            cloneStruct->clear();
            codec.deserialize(*cloneStruct, buffer, 0);

            BOOST_CHECK(value->isEqual(*cloneStruct));
            BOOST_CHECK_EQUAL(value->toJsonString(), cloneStruct->toJsonString());
        }

        {
            // Struct0: (uint,int,bool,address,bytes20,bytes,string)
            // Struct1: (int[],string[],address[3])
            // Struct(Struct0,Struct1)

            // Struct0: 111,-111,false,0x11,"111","1234567890,""
            auto struct0 = bcos::cppsdk::abi::Struct::newValue();
            auto u = u256(111);
            auto s = s256(-111);
            auto b = false;
            auto addr = std::string("0x0000000000000000000000000000000000000011");
            auto bsn = h160{1111}.asBytes();
            auto bs = h512{1234567890}.asBytes();
            auto str = "12345";

            struct0->addMember(Uint::newValue(u));
            struct0->addMember(Int::newValue(s));
            struct0->addMember(Boolean::newValue(b));
            struct0->addMember(Addr::newValue(addr));
            struct0->addMember(bcos::cppsdk::abi::FixedBytes::newValue(20, bsn));
            struct0->addMember(bcos::cppsdk::abi::DynamicBytes::newValue(bs));
            struct0->addMember(bcos::cppsdk::abi::String::newValue(str));

            struct0->setName("struct0");

            BOOST_CHECK(struct0->dynamicType());  // false
            BOOST_CHECK(struct0->type() == Type::STRUCT);
            // BOOST_CHECK(value->name());
            BOOST_CHECK_EQUAL(struct0->offsetAsBytes(), MAX_BYTE_LENGTH);
            BOOST_CHECK_EQUAL(struct0->value().size(), 7);

            // int[]
            auto intDyList = bcos::cppsdk::abi::DynamicList::newValue();
            intDyList->addMember(Int::newValue(s));
            intDyList->addMember(Int::newValue(s));
            intDyList->addMember(Int::newValue(s));
            intDyList->setName("intDyList");

            // string[]
            auto strDyList = bcos::cppsdk::abi::DynamicList::newValue();
            strDyList->addMember(String::newValue(str));
            strDyList->addMember(String::newValue(str));
            strDyList->addMember(String::newValue(str));
            strDyList->setName("strDyList");


            // address[3]
            auto addrFixedList = bcos::cppsdk::abi::FixedList::newValue(3);
            addrFixedList->addMember(Addr::newValue(addr));
            addrFixedList->addMember(Addr::newValue(addr));
            addrFixedList->addMember(Addr::newValue(addr));
            addrFixedList->setName("addrFixedList");


            auto struct1 = bcos::cppsdk::abi::Struct::newValue();
            struct1->addMember(std::move(intDyList));
            struct1->addMember(std::move(strDyList));
            struct1->addMember(std::move(addrFixedList));

            struct1->setName("struct1");

            BOOST_CHECK(struct1->kvValue().find("intDyList") != struct1->kvValue().end());
            BOOST_CHECK(struct1->kvValue().find("strDyList") != struct1->kvValue().end());
            BOOST_CHECK(struct1->kvValue().find("addrFixedList") != struct1->kvValue().end());

            BOOST_CHECK(struct1->dynamicType());  // false
            BOOST_CHECK(struct1->type() == Type::STRUCT);
            // BOOST_CHECK(value->name());
            BOOST_CHECK_EQUAL(struct1->offsetAsBytes(), MAX_BYTE_LENGTH);
            BOOST_CHECK_EQUAL(struct1->value().size(), 3);

            auto struct_ = bcos::cppsdk::abi::Struct::newValue();
            struct_->addMember(std::move(struct0));
            struct_->addMember(std::move(struct1));

            BOOST_CHECK(struct_->dynamicType());  // false
            BOOST_CHECK(struct_->type() == Type::STRUCT);
            BOOST_CHECK_EQUAL(struct_->offsetAsBytes(), MAX_BYTE_LENGTH);

            BOOST_CHECK(struct_->kvValue().find("struct1") != struct_->kvValue().end());
            BOOST_CHECK(struct_->kvValue().find("struct0") != struct_->kvValue().end());

            // clone test
            auto structClone =
                std::dynamic_pointer_cast<Struct>(AbstractType::Ptr(struct_->clone()));

            BOOST_CHECK(structClone->isEqual(*struct_));

            BOOST_CHECK(structClone->dynamicType());  // false
            BOOST_CHECK(structClone->type() == Type::STRUCT);
            BOOST_CHECK_EQUAL(structClone->offsetAsBytes(), MAX_BYTE_LENGTH);

            BOOST_CHECK(structClone->kvValue().find("struct1") != structClone->kvValue().end());
            BOOST_CHECK(structClone->kvValue().find("struct0") != structClone->kvValue().end());
            BOOST_CHECK_EQUAL(structClone->toJson().size(), 2);

            {
                // solidity
                ContractABITypeCodecSolImpl codec;
                bcos::bytes buffer;
                codec.serialize(*struct_, buffer);

                BOOST_CHECK_EQUAL(
                    "000000000000000000000000000000000000000000000000000000000000004000000000000000"
                    "000000000000000000000000000000000000000000000001c00000000000000000000000000000"
                    "00000000000000000000000000000000006fffffffffffffffffffffffffffffffffffffffffff"
                    "ffffffffffffffffffff9100000000000000000000000000000000000000000000000000000000"
                    "000000000000000000000000000000000000000000000000000000000000000000000011000000"
                    "000000000000000000000000000000045700000000000000000000000000000000000000000000"
                    "000000000000000000000000000000000000000000e00000000000000000000000000000000000"
                    "000000000000000000000000000140000000000000000000000000000000000000000000000000"
                    "000000000000004000000000000000000000000000000000000000000000000000000000000000"
                    "0000000000000000000000000000000000000000000000000000000000499602d2000000000000"
                    "000000000000000000000000000000000000000000000000000531323334350000000000000000"
                    "000000000000000000000000000000000000000000000000000000000000000000000000000000"
                    "0000000000000000000000a0000000000000000000000000000000000000000000000000000000"
                    "000000012000000000000000000000000000000000000000000000000000000000000000110000"
                    "000000000000000000000000000000000000000000000000000000000011000000000000000000"
                    "000000000000000000000000000000000000000000001100000000000000000000000000000000"
                    "00000000000000000000000000000003ffffffffffffffffffffffffffffffffffffffffffffff"
                    "ffffffffffffffff91ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                    "ff91ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff910000000000"
                    "000000000000000000000000000000000000000000000000000003000000000000000000000000"
                    "000000000000000000000000000000000000006000000000000000000000000000000000000000"
                    "000000000000000000000000a00000000000000000000000000000000000000000000000000000"
                    "0000000000e0000000000000000000000000000000000000000000000000000000000000000531"
                    "323334350000000000000000000000000000000000000000000000000000000000000000000000"
                    "000000000000000000000000000000000000000000000005313233343500000000000000000000"
                    "000000000000000000000000000000000000000000000000000000000000000000000000000000"
                    "000000000000000000053132333435000000000000000000000000000000000000000000000000"
                    "000000",
                    *toHexString(buffer));

                auto cloneStruct = struct_->clone();
                BOOST_CHECK(struct_->isEqual(*cloneStruct));

                cloneStruct->clear();
                codec.deserialize(*cloneStruct, buffer, 0);

                BOOST_CHECK(struct_->isEqual(*cloneStruct));
                BOOST_CHECK_EQUAL(struct_->toJsonString(), cloneStruct->toJsonString());
            }

            {
                // liquid
            }
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
