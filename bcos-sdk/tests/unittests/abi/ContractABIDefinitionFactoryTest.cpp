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
 * @file ContractABIDefinitionTest.cpp
 * @author: octopus
 * @date 2022-05-30
 */

#include "bcos-cpp-sdk/utilities/abi/ContractABIDefinitionFactory.h"
#include "bcos-cpp-sdk/utilities/abi/ContractABICodec.h"
#include "bcos-cpp-sdk/utilities/abi/ContractABIDefinition.h"
#include "bcos-cpp-sdk/utilities/abi/ContractABITypeCodec.h"
#include <bcos-cpp-sdk/utilities/abi/ContractABIType.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/FixedBytes.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/tools/old/interface.hpp>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <utility>

using namespace bcos;
using namespace bcos::cppsdk;
using namespace bcos::cppsdk::abi;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(ContractABIDefinitionTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(test_buildMethod_buildMethodBySignature)
{
    ContractABIDefinitionFactory factory;

    BOOST_CHECK_THROW(factory.buildMethod(std::string("func)")), std::runtime_error);
    BOOST_CHECK_THROW(factory.buildMethod(std::string("func(")), std::runtime_error);
    BOOST_CHECK_THROW(factory.buildMethod(std::string("()")), std::runtime_error);
    BOOST_CHECK_THROW(
        factory.buildMethod(std::string("f(tuple(string,int256))")), std::runtime_error);
    // BOOST_CHECK_THROW(factory.buildMethod(std::string("f(a,int256)")), std::runtime_error);

    auto func = factory.buildMethod(
        std::string("func(int256, uint256,bool,bytes20 ,bytes,string, int256[3], "
                    "uint256[3],bool[3],bytes20[3],bytes[3],string[3], int256[] , "
                    "uint256[],bool[],bytes20[],bytes[],string[])"));

    BOOST_CHECK_EQUAL(func->inputs().size(), 18);

    BOOST_CHECK_EQUAL(func->inputs()[0]->type(), "int256");
    BOOST_CHECK_EQUAL(func->inputs()[1]->type(), "uint256");
    BOOST_CHECK_EQUAL(func->inputs()[2]->type(), "bool");
    BOOST_CHECK_EQUAL(func->inputs()[3]->type(), "bytes20");
    BOOST_CHECK_EQUAL(func->inputs()[4]->type(), "bytes");
    BOOST_CHECK_EQUAL(func->inputs()[5]->type(), "string");

    BOOST_CHECK_EQUAL(func->inputs()[6]->type(), "int256[3]");
    BOOST_CHECK_EQUAL(func->inputs()[7]->type(), "uint256[3]");
    BOOST_CHECK_EQUAL(func->inputs()[8]->type(), "bool[3]");
    BOOST_CHECK_EQUAL(func->inputs()[9]->type(), "bytes20[3]");
    BOOST_CHECK_EQUAL(func->inputs()[10]->type(), "bytes[3]");
    BOOST_CHECK_EQUAL(func->inputs()[11]->type(), "string[3]");

    BOOST_CHECK_EQUAL(func->inputs()[12]->type(), "int256[]");
    BOOST_CHECK_EQUAL(func->inputs()[13]->type(), "uint256[]");
    BOOST_CHECK_EQUAL(func->inputs()[14]->type(), "bool[]");
    BOOST_CHECK_EQUAL(func->inputs()[15]->type(), "bytes20[]");
    BOOST_CHECK_EQUAL(func->inputs()[16]->type(), "bytes[]");
    BOOST_CHECK_EQUAL(func->inputs()[17]->type(), "string[]");

    auto inputs = factory.buildInput(*func);
    BOOST_CHECK(inputs->type() == Type::STRUCT);

    BOOST_CHECK_EQUAL(inputs->memberSize(), 18);

    BOOST_CHECK(inputs->value()[0]->type() == Type::INT);
    BOOST_CHECK(inputs->value()[1]->type() == Type::UINT);
    BOOST_CHECK(inputs->value()[2]->type() == Type::BOOL);
    BOOST_CHECK(inputs->value()[3]->type() == Type::FIXED_BYTES);
    BOOST_CHECK(inputs->value()[4]->type() == Type::DYNAMIC_BYTES);
    BOOST_CHECK(inputs->value()[5]->type() == Type::STRING);

    BOOST_CHECK(inputs->value()[6]->type() == Type::FIXED_LIST);
    BOOST_CHECK(std::dynamic_pointer_cast<FixedList>(inputs->value()[6])->dimension() == 3);
    BOOST_CHECK(
        std::dynamic_pointer_cast<FixedList>(inputs->value()[6])->value()[0]->type() == Type::INT);

    BOOST_CHECK(inputs->value()[7]->type() == Type::FIXED_LIST);
    BOOST_CHECK(std::dynamic_pointer_cast<FixedList>(inputs->value()[7])->dimension() == 3);
    BOOST_CHECK(
        std::dynamic_pointer_cast<FixedList>(inputs->value()[7])->value()[0]->type() == Type::UINT);

    BOOST_CHECK(inputs->value()[8]->type() == Type::FIXED_LIST);
    BOOST_CHECK(std::dynamic_pointer_cast<FixedList>(inputs->value()[8])->dimension() == 3);
    BOOST_CHECK(
        std::dynamic_pointer_cast<FixedList>(inputs->value()[8])->value()[0]->type() == Type::BOOL);

    BOOST_CHECK(inputs->value()[9]->type() == Type::FIXED_LIST);
    BOOST_CHECK(std::dynamic_pointer_cast<FixedList>(inputs->value()[9])->dimension() == 3);
    BOOST_CHECK(std::dynamic_pointer_cast<FixedList>(inputs->value()[9])->value()[0]->type() ==
                Type::FIXED_BYTES);

    BOOST_CHECK(inputs->value()[10]->type() == Type::FIXED_LIST);
    BOOST_CHECK(std::dynamic_pointer_cast<FixedList>(inputs->value()[10])->dimension() == 3);
    BOOST_CHECK(std::dynamic_pointer_cast<FixedList>(inputs->value()[10])->value()[0]->type() ==
                Type::DYNAMIC_BYTES);

    BOOST_CHECK(inputs->value()[11]->type() == Type::FIXED_LIST);
    BOOST_CHECK(std::dynamic_pointer_cast<FixedList>(inputs->value()[11])->dimension() == 3);
    BOOST_CHECK(std::dynamic_pointer_cast<FixedList>(inputs->value()[11])->value()[0]->type() ==
                Type::STRING);

    BOOST_CHECK(inputs->value()[12]->type() == Type::DYNAMIC_LIST);
    BOOST_CHECK(std::dynamic_pointer_cast<DynamicList>(inputs->value()[12])->value()[0]->type() ==
                Type::INT);

    BOOST_CHECK(inputs->value()[13]->type() == Type::DYNAMIC_LIST);
    BOOST_CHECK(std::dynamic_pointer_cast<DynamicList>(inputs->value()[13])->value()[0]->type() ==
                Type::UINT);

    BOOST_CHECK(inputs->value()[14]->type() == Type::DYNAMIC_LIST);
    BOOST_CHECK(std::dynamic_pointer_cast<DynamicList>(inputs->value()[14])->value()[0]->type() ==
                Type::BOOL);

    BOOST_CHECK(inputs->value()[15]->type() == Type::DYNAMIC_LIST);
    BOOST_CHECK(std::dynamic_pointer_cast<DynamicList>(inputs->value()[15])->value()[0]->type() ==
                Type::FIXED_BYTES);

    BOOST_CHECK(inputs->value()[16]->type() == Type::DYNAMIC_LIST);
    BOOST_CHECK(std::dynamic_pointer_cast<DynamicList>(inputs->value()[16])->value()[0]->type() ==
                Type::DYNAMIC_BYTES);

    BOOST_CHECK(inputs->value()[17]->type() == Type::DYNAMIC_LIST);
    BOOST_CHECK(std::dynamic_pointer_cast<DynamicList>(inputs->value()[17])->value()[0]->type() ==
                Type::STRING);

    BOOST_CHECK_EQUAL(func->getMethodSignatureAsString(),
        "func(int256,uint256,bool,bytes20,bytes,string,int256[3],uint256[3],bool[3],bytes20[3],"
        "bytes[3],string[3],int256[],uint256[],bool[],bytes20[],bytes[],string[])");
}

BOOST_AUTO_TEST_CASE(test_buildBaseAbstractType)
{
    bcos::cppsdk::abi::ContractABIDefinitionFactory contractABIDefinitionFactory;
    {
        NamedType::ConstPtr _namedType = std::make_shared<NamedType>("a", "uint[]");
        NamedTypeHelper::Ptr _namedTypeHelper =
            std::make_shared<NamedTypeHelper>(_namedType->type());

        auto tv = contractABIDefinitionFactory.buildBaseAbstractType(_namedTypeHelper);

        BOOST_CHECK(tv->type() == Type::UINT);
        BOOST_CHECK(static_cast<Uint&>(*tv).extent() == 256);
    }

    {
        NamedType::ConstPtr _namedType = std::make_shared<NamedType>("a", "uint8[]");
        NamedTypeHelper::Ptr _namedTypeHelper =
            std::make_shared<NamedTypeHelper>(_namedType->type());

        auto tv = contractABIDefinitionFactory.buildBaseAbstractType(_namedTypeHelper);

        BOOST_CHECK(tv->type() == Type::UINT);
        BOOST_CHECK(static_cast<Uint&>(*tv).extent() == 8);
    }

    {
        NamedType::ConstPtr _namedType = std::make_shared<NamedType>("a", "uint256[]");
        NamedTypeHelper::Ptr _namedTypeHelper =
            std::make_shared<NamedTypeHelper>(_namedType->type());

        auto tv = contractABIDefinitionFactory.buildBaseAbstractType(_namedTypeHelper);

        BOOST_CHECK(tv->type() == Type::UINT);
        BOOST_CHECK(static_cast<Uint&>(*tv).extent() == 256);
    }

    {
        NamedType::ConstPtr _namedType = std::make_shared<NamedType>("a", "uint1");
        NamedTypeHelper::Ptr _namedTypeHelper =
            std::make_shared<NamedTypeHelper>(_namedType->type());

        BOOST_CHECK_THROW(contractABIDefinitionFactory.buildBaseAbstractType(_namedTypeHelper),
            std::runtime_error);
    }

    {
        NamedType::ConstPtr _namedType = std::make_shared<NamedType>("a", "int[]");
        NamedTypeHelper::Ptr _namedTypeHelper =
            std::make_shared<NamedTypeHelper>(_namedType->type());

        auto tv = contractABIDefinitionFactory.buildBaseAbstractType(_namedTypeHelper);

        BOOST_CHECK(tv->type() == Type::INT);
        BOOST_CHECK(static_cast<Int&>(*tv).extent() == 256);
    }

    {
        NamedType::ConstPtr _namedType = std::make_shared<NamedType>("a", "int32[]");
        NamedTypeHelper::Ptr _namedTypeHelper =
            std::make_shared<NamedTypeHelper>(_namedType->type());

        auto tv = contractABIDefinitionFactory.buildBaseAbstractType(_namedTypeHelper);

        BOOST_CHECK(tv->type() == Type::INT);
        BOOST_CHECK(static_cast<Int&>(*tv).extent() == 32);
    }

    {
        NamedType::ConstPtr _namedType = std::make_shared<NamedType>("a", "int255[]");
        NamedTypeHelper::Ptr _namedTypeHelper =
            std::make_shared<NamedTypeHelper>(_namedType->type());

        BOOST_CHECK_THROW(contractABIDefinitionFactory.buildBaseAbstractType(_namedTypeHelper),
            std::runtime_error);
    }

    {
        NamedType::ConstPtr _namedType = std::make_shared<NamedType>("a", "bool[]");
        NamedTypeHelper::Ptr _namedTypeHelper =
            std::make_shared<NamedTypeHelper>(_namedType->type());

        auto tv = contractABIDefinitionFactory.buildBaseAbstractType(_namedTypeHelper);

        BOOST_CHECK(tv->type() == Type::BOOL);
    }

    {
        NamedType::ConstPtr _namedType = std::make_shared<NamedType>("a", "address[]");
        NamedTypeHelper::Ptr _namedTypeHelper =
            std::make_shared<NamedTypeHelper>(_namedType->type());

        auto tv = contractABIDefinitionFactory.buildBaseAbstractType(_namedTypeHelper);

        BOOST_CHECK(tv->type() == Type::ADDRESS);
    }

    {
        NamedType::ConstPtr _namedType = std::make_shared<NamedType>("a", "string[]");
        NamedTypeHelper::Ptr _namedTypeHelper =
            std::make_shared<NamedTypeHelper>(_namedType->type());

        auto tv = contractABIDefinitionFactory.buildBaseAbstractType(_namedTypeHelper);

        BOOST_CHECK(tv->type() == Type::STRING);
    }

    {
        NamedType::ConstPtr _namedType = std::make_shared<NamedType>("a", "bytes[]");
        NamedTypeHelper::Ptr _namedTypeHelper =
            std::make_shared<NamedTypeHelper>(_namedType->type());

        auto tv = contractABIDefinitionFactory.buildBaseAbstractType(_namedTypeHelper);

        BOOST_CHECK(tv->type() == Type::DYNAMIC_BYTES);
    }

    {
        NamedType::ConstPtr _namedType = std::make_shared<NamedType>("a", "bytes32[]");
        NamedTypeHelper::Ptr _namedTypeHelper =
            std::make_shared<NamedTypeHelper>(_namedType->type());

        auto tv = contractABIDefinitionFactory.buildBaseAbstractType(_namedTypeHelper);

        BOOST_CHECK(tv->type() == Type::FIXED_BYTES);
        BOOST_CHECK(static_cast<bcos::cppsdk::abi::FixedBytes&>(*tv).fixedN() == 32);
    }

    {
        NamedType::ConstPtr _namedType = std::make_shared<NamedType>("a", "bytes1[]");
        NamedTypeHelper::Ptr _namedTypeHelper =
            std::make_shared<NamedTypeHelper>(_namedType->type());

        auto tv = contractABIDefinitionFactory.buildBaseAbstractType(_namedTypeHelper);

        BOOST_CHECK(tv->type() == Type::FIXED_BYTES);
        BOOST_CHECK(static_cast<bcos::cppsdk::abi::FixedBytes&>(*tv).fixedN() == 1);
    }

    {
        NamedType::ConstPtr _namedType = std::make_shared<NamedType>("a", "bytes33[]");
        NamedTypeHelper::Ptr _namedTypeHelper =
            std::make_shared<NamedTypeHelper>(_namedType->type());

        BOOST_CHECK_THROW(contractABIDefinitionFactory.buildBaseAbstractType(_namedTypeHelper),
            std::runtime_error);
    }
}

BOOST_AUTO_TEST_CASE(test_loadABI_HelloWorld_failed)
{
    std::string abi =
        "[{\"constant\":false,\"inputs\":[{\"name\":\"n\",\"type\":\"string\"}],\"name\":\"set\","
        "\"outputs\":[],\"payable\":false,\"stateMutability\":\"nonpayable\",\"type\":\"function\"}"
        ",{\"constant\":true,\"inputs\":[],\"name\":\"get\",\"outputs\":[{\"name\":\"\",\"type\":"
        "\"string\"}],\"payable\":false,\"stateMutability\":\"view\",\"type\":\"function\"},{"
        "\"inputs\":[],\"payable\":false,\"stateMutability\":\"nonpayable\",\"type\":"
        "\"constructor\"}]";

    ContractABIDefinitionFactory contractABIDefinitionFactory;
    {
        std::string validJsonABI = "HelloWorld";
        BOOST_CHECK_THROW(contractABIDefinitionFactory.buildABI(validJsonABI), std::runtime_error);
    }
}

BOOST_AUTO_TEST_CASE(test_loadABI_HelloWorld)
{
    std::string abi =
        "[{\"constant\":false,\"inputs\":[{\"name\":\"n\",\"type\":\"string\"}],\"name\":\"set\","
        "\"outputs\":[],\"payable\":false,\"stateMutability\":\"nonpayable\",\"type\":\"function\"}"
        ",{\"constant\":true,\"inputs\":[],\"name\":\"get\",\"outputs\":[{\"name\":\"\",\"type\":"
        "\"string\"}],\"payable\":false,\"stateMutability\":\"view\",\"type\":\"function\"},{"
        "\"inputs\":[],\"payable\":false,\"stateMutability\":\"nonpayable\",\"type\":"
        "\"constructor\"}]";

    /*
    [
     {
       "constant": false,
       "inputs": [
         {
           "name": "n",
           "type": "string"
         }
       ],
       "name": "set",
       "outputs": [

       ],
       "payable": false,
       "stateMutability": "nonpayable",
       "type": "function"
     },
     {
       "constant": true,
       "inputs": [

       ],
       "name": "get",
       "outputs": [
         {
           "name": "",
           "type": "string"
         }
       ],
       "payable": false,
       "stateMutability": "view",
       "type": "function"
     },
     {
       "inputs": [

       ],
       "payable": false,
       "stateMutability": "nonpayable",
       "type": "constructor"
     }
    ]
    */

    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();

    ContractABIDefinitionFactory contractABIDefinitionFactory;
    auto func = contractABIDefinitionFactory.buildABI(abi);

    BOOST_CHECK_EQUAL(func->methods().size(), 3);
    BOOST_CHECK_EQUAL(func->methodNames().size(), 3);

    {
        auto getHelloFunc = contractABIDefinitionFactory.buildMethod(abi, "getHello");
        BOOST_CHECK(getHelloFunc.empty());
    }

    {
        auto methods = contractABIDefinitionFactory.buildMethod(abi, "constructor");
        BOOST_CHECK_EQUAL(methods.size(), 1);
        BOOST_CHECK_EQUAL(methods[0]->type(), "constructor");

        auto it = func->methods().find("constructor");
        // constructor
        BOOST_CHECK(it != func->methods().end());
        BOOST_CHECK_EQUAL(it->second.size(), 1);

        auto constructor = it->second[0];

        BOOST_CHECK_EQUAL(constructor->name(), "");
        BOOST_CHECK_EQUAL(constructor->type(), "constructor");
        BOOST_CHECK_EQUAL(constructor->constant(), false);
        BOOST_CHECK_EQUAL(constructor->anonymous(), false);
        BOOST_CHECK_EQUAL(constructor->isEvent(), false);
        BOOST_CHECK_EQUAL(constructor->payable(), false);
        BOOST_CHECK_EQUAL(constructor->stateMutability(), "nonpayable");
        BOOST_CHECK_EQUAL(constructor->inputs().size(), 0);
        BOOST_CHECK_EQUAL(constructor->outputs().size(), 0);
        BOOST_CHECK_EQUAL(constructor->getMethodSignatureAsString(), "()");

        auto inputStruct = contractABIDefinitionFactory.buildInput(*constructor);
        BOOST_CHECK_EQUAL(inputStruct->memberSize(), 0);
        BOOST_CHECK_EQUAL(inputStruct->value().size(), 0);
        BOOST_CHECK_EQUAL(inputStruct->kvValue().size(), 0);

        auto outputStruct = contractABIDefinitionFactory.buildOutput(*constructor);
        BOOST_CHECK_EQUAL(outputStruct->memberSize(), 0);
        BOOST_CHECK_EQUAL(outputStruct->value().size(), 0);
        BOOST_CHECK_EQUAL(outputStruct->kvValue().size(), 0);
    }

    {
        auto methods = contractABIDefinitionFactory.buildMethod(abi, "set");
        BOOST_CHECK_EQUAL(methods.size(), 1);
        BOOST_CHECK_EQUAL(methods[0]->name(), "set");

        // set
        auto it = func->methods().find("set");
        // constructor
        BOOST_CHECK(it != func->methods().end());
        BOOST_CHECK_EQUAL(it->second.size(), 1);

        BOOST_CHECK_EQUAL(func->getMethod("set", true).size(), 1);
        BOOST_CHECK_EQUAL(func->getMethod("set", false).size(), 1);

        BOOST_CHECK_THROW(func->getEvent("set", true), std::runtime_error);
        BOOST_CHECK_THROW(func->getEvent("set", false), std::runtime_error);

        auto setFunc = it->second[0];

        auto set = it->second[0];

        BOOST_CHECK_EQUAL(set->name(), "set");
        BOOST_CHECK_EQUAL(set->type(), "function");
        BOOST_CHECK_EQUAL(set->constant(), false);
        BOOST_CHECK_EQUAL(set->anonymous(), false);
        BOOST_CHECK_EQUAL(set->isEvent(), false);
        BOOST_CHECK_EQUAL(set->payable(), false);
        BOOST_CHECK_EQUAL(set->stateMutability(), "nonpayable");
        BOOST_CHECK_EQUAL(set->inputs().size(), 1);
        BOOST_CHECK_EQUAL(set->outputs().size(), 0);
        BOOST_CHECK_EQUAL(set->getMethodSignatureAsString(), "set(string)");
        BOOST_CHECK_EQUAL(set->getMethodIDAsString(hashImpl), "4ed3885e");

        auto inputStruct = contractABIDefinitionFactory.buildInput(*set);
        BOOST_CHECK_EQUAL(inputStruct->memberSize(), 1);
        BOOST_CHECK_EQUAL(inputStruct->value().size(), 1);

        BOOST_CHECK(inputStruct->value()[0]->type() == Type::STRING);
        BOOST_CHECK_EQUAL(inputStruct->value()[0]->name(), "n");
        BOOST_CHECK_EQUAL(inputStruct->value()[0]->dynamicType(), true);
        BOOST_CHECK_EQUAL(inputStruct->value()[0]->offsetAsBytes(), MAX_BYTE_LENGTH);

        BOOST_CHECK_EQUAL(inputStruct->kvValue().size(), 1);
        BOOST_CHECK(inputStruct->kvValue().find("n") != inputStruct->kvValue().end());

        auto outputStruct = contractABIDefinitionFactory.buildOutput(*set);
        BOOST_CHECK_EQUAL(outputStruct->memberSize(), 0);
        BOOST_CHECK_EQUAL(outputStruct->value().size(), 0);
        BOOST_CHECK_EQUAL(outputStruct->kvValue().size(), 0);
    }

    {
        auto methods = contractABIDefinitionFactory.buildMethod(abi, "get");
        BOOST_CHECK_EQUAL(methods.size(), 1);
        BOOST_CHECK_EQUAL(methods[0]->name(), "get");

        // get
        auto it = func->methods().find("get");
        // get
        BOOST_CHECK(it != func->methods().end());
        BOOST_CHECK_EQUAL(it->second.size(), 1);

        auto get = it->second[0];

        BOOST_CHECK_EQUAL(get->name(), "get");
        BOOST_CHECK_EQUAL(get->type(), "function");
        BOOST_CHECK_EQUAL(get->constant(), true);
        BOOST_CHECK_EQUAL(get->anonymous(), false);
        BOOST_CHECK_EQUAL(get->isEvent(), false);
        BOOST_CHECK_EQUAL(get->payable(), false);
        BOOST_CHECK_EQUAL(get->stateMutability(), "view");
        BOOST_CHECK_EQUAL(get->inputs().size(), 0);
        BOOST_CHECK_EQUAL(get->outputs().size(), 1);
        BOOST_CHECK_EQUAL(get->getMethodSignatureAsString(), "get()");
        BOOST_CHECK_EQUAL(get->getMethodIDAsString(hashImpl), "6d4ce63c");

        BOOST_CHECK_EQUAL(func->getMethod("get", true).size(), 1);
        BOOST_CHECK_EQUAL(func->getMethod("get", false).size(), 1);

        BOOST_CHECK_THROW(func->getEvent("get", true), std::runtime_error);
        BOOST_CHECK_THROW(func->getEvent("get", false), std::runtime_error);

        auto inputStruct = contractABIDefinitionFactory.buildInput(*get);
        BOOST_CHECK_EQUAL(inputStruct->memberSize(), 0);
        BOOST_CHECK_EQUAL(inputStruct->value().size(), 0);
        BOOST_CHECK_EQUAL(inputStruct->kvValue().size(), 0);

        auto outputStruct = contractABIDefinitionFactory.buildOutput(*get);
        BOOST_CHECK_EQUAL(outputStruct->memberSize(), 1);
        BOOST_CHECK_EQUAL(outputStruct->value().size(), 1);
        BOOST_CHECK_EQUAL(outputStruct->kvValue().size(), 0);

        BOOST_CHECK(outputStruct->value()[0]->type() == Type::STRING);
        BOOST_CHECK_EQUAL(outputStruct->value()[0]->name(), "");
        BOOST_CHECK_EQUAL(outputStruct->value()[0]->dynamicType(), true);
        BOOST_CHECK_EQUAL(outputStruct->value()[0]->offsetAsBytes(), MAX_BYTE_LENGTH);
        BOOST_CHECK(outputStruct->kvValue().find("n") == inputStruct->kvValue().end());
    }

    {
        auto getHelloFunc = contractABIDefinitionFactory.buildMethod(abi, "getHello");
        BOOST_CHECK(getHelloFunc.empty());
    }
}

BOOST_AUTO_TEST_CASE(test_loadABI_TestStruct)
{
    /*
     pragma solidity >0.4.24 <0.6.11;
     pragma experimental ABIEncoderV2;

     contract StructTest {

         struct Person {
             std::string name;
             uint age;
             Account account;
         }

         struct Account {
             address accountAddr;
             int points;
         }

         Person[] data;

         function get(Person[] memory person) public pure returns(Person[] memory) {

             Person[] memory ps = new Person[](person.length);
             for (uint i = 0; i < person.length; i++) {
                 ps[i] = person[i];
             }

             return ps;
         }

         function register(Person memory person) public returns(Person[] ) {
             Account memory account = Account(msg.sender, 100);
             Person memory tmp = Person(person.name, person.age+1, account);
             data.push(tmp);

             return data;
         }
     }
     */
    std::string abi =
        "[{\"constant\":true,\"inputs\":[{\"components\":[{\"name\":\"name\",\"type\":\"string\"},{"
        "\"name\":\"age\",\"type\":\"uint256\"},{\"components\":[{\"name\":\"accountAddr\","
        "\"type\":\"address\"},{\"name\":\"points\",\"type\":\"int256\"}],\"name\":\"account\","
        "\"type\":\"tuple\"}],\"name\":\"person\",\"type\":\"tuple[]\"}],\"name\":\"get\","
        "\"outputs\":[{\"components\":[{\"name\":\"name\",\"type\":\"string\"},{\"name\":\"age\","
        "\"type\":\"uint256\"},{\"components\":[{\"name\":\"accountAddr\",\"type\":\"address\"},{"
        "\"name\":\"points\",\"type\":\"int256\"}],\"name\":\"account\",\"type\":\"tuple\"}],"
        "\"name\":\"\",\"type\":\"tuple[]\"}],\"payable\":false,\"stateMutability\":\"pure\","
        "\"type\":\"function\"},{\"constant\":false,\"inputs\":[{\"components\":[{\"name\":"
        "\"name\",\"type\":\"string\"},{\"name\":\"age\",\"type\":\"uint256\"},{\"components\":[{"
        "\"name\":\"accountAddr\",\"type\":\"address\"},{\"name\":\"points\",\"type\":\"int256\"}],"
        "\"name\":\"account\",\"type\":\"tuple\"}],\"name\":\"person\",\"type\":\"tuple\"}],"
        "\"name\":\"register\",\"outputs\":[{\"components\":[{\"name\":\"name\",\"type\":"
        "\"string\"},{\"name\":\"age\",\"type\":\"uint256\"},{\"components\":[{\"name\":"
        "\"accountAddr\",\"type\":\"address\"},{\"name\":\"points\",\"type\":\"int256\"}],\"name\":"
        "\"account\",\"type\":\"tuple\"}],\"name\":\"\",\"type\":\"tuple[]\"}],\"payable\":false,"
        "\"stateMutability\":\"nonpayable\",\"type\":\"function\"}]";


    /*
    [
      {
        "constant": true,
        "inputs": [
          {
            "components": [
              {
                "name": "name",
                "type": "string"
              },
              {
                "name": "age",
                "type": "uint256"
              },
              {
                "components": [
                  {
                    "name": "accountAddr",
                    "type": "address"
                  },
                  {
                    "name": "points",
                    "type": "int256"
                  }
                ],
                "name": "account",
                "type": "tuple"
              }
            ],
            "name": "person",
            "type": "tuple[]"
          }
        ],
        "name": "get",
        "outputs": [
          {
            "components": [
              {
                "name": "name",
                "type": "string"
              },
              {
                "name": "age",
                "type": "uint256"
              },
              {
                "components": [
                  {
                    "name": "accountAddr",
                    "type": "address"
                  },
                  {
                    "name": "points",
                    "type": "int256"
                  }
                ],
                "name": "account",
                "type": "tuple"
              }
            ],
            "name": "",
            "type": "tuple[]"
          }
        ],
        "payable": false,
        "stateMutability": "pure",
        "type": "function"
      },
      {
        "constant": false,
        "inputs": [
          {
            "components": [
              {
                "name": "name",
                "type": "string"
              },
              {
                "name": "age",
                "type": "uint256"
              },
              {
                "components": [
                  {
                    "name": "accountAddr",
                    "type": "address"
                  },
                  {
                    "name": "points",
                    "type": "int256"
                  }
                ],
                "name": "account",
                "type": "tuple"
              }
            ],
            "name": "person",
            "type": "tuple"
          }
        ],
        "name": "register",
        "outputs": [
          {
            "components": [
              {
                "name": "name",
                "type": "string"
              },
              {
                "name": "age",
                "type": "uint256"
              },
              {
                "components": [
                  {
                    "name": "accountAddr",
                    "type": "address"
                  },
                  {
                    "name": "points",
                    "type": "int256"
                  }
                ],
                "name": "account",
                "type": "tuple"
              }
            ],
            "name": "",
            "type": "tuple[]"
          }
        ],
        "payable": false,
        "stateMutability": "nonpayable",
        "type": "function"
      }
    ]
    */
    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();

    ContractABIDefinitionFactory factory;
    auto contract = factory.buildABI(abi);

    BOOST_CHECK_EQUAL(contract->getMethod("get", true).size(), 1);
    BOOST_CHECK_EQUAL(contract->getMethod("get", false).size(), 1);

    BOOST_CHECK_THROW(contract->getEvent("get", true), std::runtime_error);
    BOOST_CHECK_THROW(contract->getEvent("get", false), std::runtime_error);

    BOOST_CHECK_EQUAL(contract->getMethod("register", true).size(), 1);
    BOOST_CHECK_EQUAL(contract->getMethod("register", false).size(), 1);

    BOOST_CHECK_THROW(contract->getEvent("register", true), std::runtime_error);
    BOOST_CHECK_THROW(contract->getEvent("register", false), std::runtime_error);

    BOOST_CHECK_EQUAL(contract->methods().size(), 2);
    BOOST_CHECK_EQUAL(contract->methodNames().size(), 2);

    {
        auto methods = factory.buildMethod(abi, "get");
        BOOST_CHECK_EQUAL(methods.size(), 1);
        BOOST_CHECK_EQUAL(methods[0]->name(), "get");

        auto it = contract->methods().find("get");
        // constructor
        BOOST_CHECK(it != contract->methods().end());
        BOOST_CHECK_EQUAL(it->second.size(), 1);

        auto getFunc = it->second[0];

        BOOST_CHECK_EQUAL(
            getFunc->getMethodSignatureAsString(), "get((string,uint256,(address,int256))[])");
        BOOST_CHECK_EQUAL(getFunc->getMethodIDAsString(hashImpl), "cb1ac188");
        BOOST_CHECK_EQUAL(getFunc->constant(), true);
        BOOST_CHECK_EQUAL(getFunc->anonymous(), false);
        BOOST_CHECK_EQUAL(getFunc->isEvent(), false);
        BOOST_CHECK_EQUAL(getFunc->payable(), false);
        BOOST_CHECK_EQUAL(getFunc->name(), "get");
        BOOST_CHECK_EQUAL(getFunc->type(), "function");
        BOOST_CHECK_EQUAL(getFunc->stateMutability(), "pure");
        BOOST_CHECK_EQUAL(getFunc->inputs().size(), 1);
        BOOST_CHECK_EQUAL(getFunc->outputs().size(), 1);

        auto inputStruct = factory.buildInput(*getFunc);
        BOOST_CHECK_EQUAL(inputStruct->memberSize(), 1);
        BOOST_CHECK_EQUAL(inputStruct->value().size(), 1);

        BOOST_CHECK(inputStruct->value()[0]->type() == Type::DYNAMIC_LIST);
        BOOST_CHECK_EQUAL(inputStruct->value()[0]->name(), "person");
        BOOST_CHECK_EQUAL(inputStruct->value()[0]->dynamicType(), true);
        BOOST_CHECK_EQUAL(inputStruct->value()[0]->offsetAsBytes(), MAX_BYTE_LENGTH);

        BOOST_CHECK_EQUAL(inputStruct->kvValue().size(), 1);
        BOOST_CHECK(inputStruct->kvValue().find("person") != inputStruct->kvValue().end());

        auto outputStruct = factory.buildOutput(*getFunc);
        BOOST_CHECK_EQUAL(outputStruct->memberSize(), 1);
        BOOST_CHECK_EQUAL(outputStruct->value().size(), 1);
        BOOST_CHECK_EQUAL(outputStruct->kvValue().size(), 0);

        BOOST_CHECK(outputStruct->value()[0]->type() == Type::DYNAMIC_LIST);
        // BOOST_CHECK_EQUAL(outputStruct->value()[0]->name(), "person");
        BOOST_CHECK_EQUAL(outputStruct->value()[0]->dynamicType(), true);
        BOOST_CHECK_EQUAL(outputStruct->value()[0]->offsetAsBytes(), MAX_BYTE_LENGTH);
    }

    {
        auto methods = factory.buildMethod(abi, "register");
        BOOST_CHECK_EQUAL(methods.size(), 1);
        BOOST_CHECK_EQUAL(methods[0]->name(), "register");

        auto it = contract->methods().find("register");
        // constructor
        BOOST_CHECK(it != contract->methods().end());
        BOOST_CHECK_EQUAL(it->second.size(), 1);

        auto registerFunc = it->second[0];

        BOOST_CHECK_EQUAL(registerFunc->getMethodSignatureAsString(),
            "register((string,uint256,(address,int256)))");
        BOOST_CHECK_EQUAL(registerFunc->getMethodIDAsString(hashImpl), "f420f505");

        BOOST_CHECK_EQUAL(registerFunc->constant(), false);
        BOOST_CHECK_EQUAL(registerFunc->anonymous(), false);
        BOOST_CHECK_EQUAL(registerFunc->isEvent(), false);
        BOOST_CHECK_EQUAL(registerFunc->payable(), false);
        BOOST_CHECK_EQUAL(registerFunc->name(), "register");
        BOOST_CHECK_EQUAL(registerFunc->type(), "function");
        BOOST_CHECK_EQUAL(registerFunc->stateMutability(), "nonpayable");
        BOOST_CHECK_EQUAL(registerFunc->inputs().size(), 1);
        BOOST_CHECK_EQUAL(registerFunc->outputs().size(), 1);

        {
            auto inputStruct = factory.buildInput(*registerFunc);
            BOOST_CHECK_EQUAL(inputStruct->memberSize(), 1);
            BOOST_CHECK_EQUAL(inputStruct->value().size(), 1);
            BOOST_CHECK_EQUAL(inputStruct->kvValue().size(), 1);
            BOOST_CHECK(inputStruct->kvValue().find("person") != inputStruct->kvValue().end());

            auto inputParams0 = inputStruct->value()[0];
            BOOST_CHECK(inputParams0->type() == Type::STRUCT);
            // BOOST_CHECK_EQUAL(outputStruct->value()[0]->name(), "");
            BOOST_CHECK_EQUAL(inputParams0->dynamicType(), true);
            BOOST_CHECK_EQUAL(inputParams0->offsetAsBytes(), MAX_BYTE_LENGTH);

            // (string,uint256,(address,int256))
            auto p0 = std::dynamic_pointer_cast<Struct>(inputParams0);

            // string
            BOOST_CHECK_EQUAL(p0->memberSize(), 3);
            BOOST_CHECK_EQUAL(p0->value().size(), 3);
            BOOST_CHECK_EQUAL(p0->kvValue().size(), 3);

            BOOST_CHECK_EQUAL(p0->value()[0]->name(), "name");
            BOOST_CHECK(p0->value()[0]->type() == Type::STRING);
            BOOST_CHECK_EQUAL(p0->value()[0]->dynamicType(), true);
            BOOST_CHECK_EQUAL(p0->value()[0]->offsetAsBytes(), MAX_BYTE_LENGTH);

            // uint256
            BOOST_CHECK_EQUAL(p0->value()[1]->name(), "age");
            BOOST_CHECK(p0->value()[1]->type() == Type::UINT);
            BOOST_CHECK_EQUAL(p0->value()[1]->dynamicType(), false);
            BOOST_CHECK_EQUAL(p0->value()[1]->offsetAsBytes(), MAX_BYTE_LENGTH);

            // (address,int256)
            BOOST_CHECK_EQUAL(p0->value()[2]->name(), "account");
            BOOST_CHECK(p0->value()[2]->type() == Type::STRUCT);
            BOOST_CHECK_EQUAL(p0->value()[2]->dynamicType(), false);
            BOOST_CHECK_EQUAL(p0->value()[2]->offsetAsBytes(), 2 * MAX_BYTE_LENGTH);

            // (address,int256)
            auto p00 = std::dynamic_pointer_cast<Struct>(p0->value()[2]);
            BOOST_CHECK_EQUAL(p00->memberSize(), 2);
            BOOST_CHECK_EQUAL(p00->value().size(), 2);
            BOOST_CHECK_EQUAL(p00->kvValue().size(), 2);
            BOOST_CHECK(p00->kvValue().find("accountAddr") != p00->kvValue().end());
            BOOST_CHECK(p00->kvValue().find("points") != p00->kvValue().end());

            // address
            BOOST_CHECK_EQUAL(p00->value()[0]->name(), "accountAddr");
            BOOST_CHECK(p00->value()[0]->type() == Type::ADDRESS);
            BOOST_CHECK_EQUAL(p00->value()[0]->dynamicType(), false);
            BOOST_CHECK_EQUAL(p00->value()[0]->offsetAsBytes(), MAX_BYTE_LENGTH);

            // int256
            BOOST_CHECK_EQUAL(p00->value()[1]->name(), "points");
            BOOST_CHECK(p00->value()[1]->type() == Type::INT);
            BOOST_CHECK_EQUAL(p00->value()[1]->dynamicType(), false);
            BOOST_CHECK_EQUAL(p00->value()[1]->offsetAsBytes(), MAX_BYTE_LENGTH);
        }

        {
            auto outputStruct = factory.buildOutput(*registerFunc);
            BOOST_CHECK_EQUAL(outputStruct->memberSize(), 1);
            BOOST_CHECK_EQUAL(outputStruct->value().size(), 1);
            BOOST_CHECK_EQUAL(outputStruct->kvValue().size(), 0);

            BOOST_CHECK(outputStruct->value()[0]->type() == Type::DYNAMIC_LIST);
            // BOOST_CHECK_EQUAL(outputStruct->value()[0]->name(), "");
            BOOST_CHECK_EQUAL(outputStruct->value()[0]->dynamicType(), true);
            BOOST_CHECK_EQUAL(outputStruct->value()[0]->offsetAsBytes(), MAX_BYTE_LENGTH);
            BOOST_CHECK(outputStruct->kvValue().find("person") == outputStruct->kvValue().end());

            auto outputParams0 = outputStruct->value()[0];
            BOOST_CHECK(outputParams0->type() == Type::DYNAMIC_LIST);
            // BOOST_CHECK_EQUAL(outputStruct->value()[0]->name(), "");
            BOOST_CHECK_EQUAL(outputParams0->dynamicType(), true);
            BOOST_CHECK_EQUAL(outputParams0->offsetAsBytes(), MAX_BYTE_LENGTH);

            // (string,uint256,(address,int256))[]
            auto p0 = std::dynamic_pointer_cast<DynamicList>(outputParams0);
            BOOST_CHECK_EQUAL(p0->name(), "");
            BOOST_CHECK_EQUAL(p0->dynamicType(), true);
            BOOST_CHECK(p0->type() == Type::DYNAMIC_LIST);
            BOOST_CHECK_EQUAL(p0->value().size(), 1);
            BOOST_CHECK_EQUAL(p0->offsetAsBytes(), MAX_BYTE_LENGTH);

            // (string,uint256,(address,int256))
            BOOST_CHECK(p0->value()[0]->type() == Type::STRUCT);
            BOOST_CHECK_EQUAL(p0->value()[0]->dynamicType(), true);
            BOOST_CHECK_EQUAL(p0->value()[0]->offsetAsBytes(), MAX_BYTE_LENGTH);

            auto p00 = std::dynamic_pointer_cast<Struct>(p0->value()[0]);

            BOOST_CHECK_EQUAL(p00->memberSize(), 3);
            BOOST_CHECK_EQUAL(p00->value().size(), 3);
            BOOST_CHECK_EQUAL(p00->kvValue().size(), 3);
            BOOST_CHECK(p00->kvValue().find("name") != p00->kvValue().end());
            BOOST_CHECK(p00->kvValue().find("age") != p00->kvValue().end());
            BOOST_CHECK(p00->kvValue().find("account") != p00->kvValue().end());

            // string
            BOOST_CHECK_EQUAL(p00->value()[0]->dynamicType(), true);
            BOOST_CHECK(p00->value()[0]->type() == Type::STRING);
            BOOST_CHECK_EQUAL(p00->value()[0]->name(), "name");
            BOOST_CHECK_EQUAL(p00->value()[0]->offsetAsBytes(), MAX_BYTE_LENGTH);

            // uint256
            BOOST_CHECK_EQUAL(p00->value()[1]->dynamicType(), false);
            BOOST_CHECK(p00->value()[1]->type() == Type::UINT);
            BOOST_CHECK_EQUAL(p00->value()[1]->name(), "age");
            BOOST_CHECK_EQUAL(p00->value()[1]->offsetAsBytes(), MAX_BYTE_LENGTH);

            // (address,int256)
            BOOST_CHECK_EQUAL(p00->value()[2]->dynamicType(), false);
            BOOST_CHECK(p00->value()[2]->type() == Type::STRUCT);
            BOOST_CHECK_EQUAL(p00->value()[2]->name(), "account");
            BOOST_CHECK_EQUAL(p00->value()[2]->offsetAsBytes(), 2 * MAX_BYTE_LENGTH);

            // (address,int256)
            auto p000 = std::dynamic_pointer_cast<Struct>(p00->value()[2]);
            BOOST_CHECK_EQUAL(p000->dynamicType(), false);
            BOOST_CHECK_EQUAL(p000->memberSize(), 2);
            BOOST_CHECK_EQUAL(p000->value().size(), 2);
            BOOST_CHECK_EQUAL(p000->kvValue().size(), 2);
            BOOST_CHECK(p000->kvValue().find("accountAddr") != p00->kvValue().end());
            BOOST_CHECK(p000->kvValue().find("points") != p00->kvValue().end());

            BOOST_CHECK(p000->value()[0]->type() == Type::ADDRESS);
            BOOST_CHECK_EQUAL(p000->value()[0]->dynamicType(), false);
            BOOST_CHECK_EQUAL(p000->value()[0]->offsetAsBytes(), MAX_BYTE_LENGTH);
            BOOST_CHECK_EQUAL(p000->value()[0]->name(), "accountAddr");

            BOOST_CHECK(p000->value()[1]->type() == Type::INT);
            BOOST_CHECK_EQUAL(p000->value()[1]->dynamicType(), false);
            BOOST_CHECK_EQUAL(p000->value()[1]->offsetAsBytes(), MAX_BYTE_LENGTH);
            BOOST_CHECK_EQUAL(p000->value()[1]->name(), "points");
        }
    }
}

BOOST_AUTO_TEST_CASE(test_loadABI_KVTableTest)
{
    std::string abi =
        "[{\"constant\":true,\"inputs\":[{\"name\":\"id\",\"type\":\"string\"}],\"name\":\"get\","
        "\"outputs\":[{\"name\":\"\",\"type\":\"bool\"},{\"name\":\"\",\"type\":\"int256\"},{"
        "\"name\":\"\",\"type\":\"string\"}],\"payable\":false,\"stateMutability\":\"view\","
        "\"type\":\"function\"},{\"constant\":true,\"inputs\":[{\"name\":\"age\",\"type\":"
        "\"uint256\"}],\"name\":\"get\",\"outputs\":[{\"name\":\"\",\"type\":\"bool\"},{\"name\":"
        "\"\",\"type\":\"int256\"},{\"name\":\"\",\"type\":\"string\"}],\"payable\":false,"
        "\"stateMutability\":\"view\",\"type\":\"function\"},{\"constant\":false,\"inputs\":[{"
        "\"name\":\"id\",\"type\":\"string\"},{\"name\":\"item_price\",\"type\":\"int256\"},{"
        "\"name\":\"item_name\",\"type\":\"string\"}],\"name\":\"set\",\"outputs\":[{\"name\":\"\","
        "\"type\":\"int256\"}],\"payable\":false,\"stateMutability\":\"nonpayable\",\"type\":"
        "\"function\"},{\"inputs\":[],\"payable\":false,\"stateMutability\":\"nonpayable\","
        "\"type\":\"constructor\"},{\"anonymous\":false,\"inputs\":[{\"indexed\":false,\"name\":"
        "\"count\",\"type\":\"int256\"}],\"name\":\"SetResult\",\"type\":\"event\"}]";

    /*
    [
      {
        "constant": true,
        "inputs": [
          {
            "name": "id",
            "type": "string"
          }
        ],
        "name": "get",
        "outputs": [
          {
            "name": "",
            "type": "bool"
          },
          {
            "name": "",
            "type": "int256"
          },
          {
            "name": "",
            "type": "string"
          }
        ],
        "payable": false,
        "stateMutability": "view",
        "type": "function"
      },
      {
        "constant": true,
        "inputs": [
          {
            "name": "age",
            "type": "uint256"
          }
        ],
        "name": "get",
        "outputs": [
          {
            "name": "",
            "type": "bool"
          },
          {
            "name": "",
            "type": "int256"
          },
          {
            "name": "",
            "type": "string"
          }
        ],
        "payable": false,
        "stateMutability": "view",
        "type": "function"
      },
      {
        "constant": false,
        "inputs": [
          {
            "name": "id",
            "type": "string"
          },
          {
            "name": "item_price",
            "type": "int256"
          },
          {
            "name": "item_name",
            "type": "string"
          }
        ],
        "name": "set",
        "outputs": [
          {
            "name": "",
            "type": "int256"
          }
        ],
        "payable": false,
        "stateMutability": "nonpayable",
        "type": "function"
      },
      {
        "inputs": [

        ],
        "payable": false,
        "stateMutability": "nonpayable",
        "type": "constructor"
      },
      {
        "anonymous": false,
        "inputs": [
          {
            "indexed": false,
            "name": "count",
            "type": "int256"
          }
        ],
        "name": "SetResult",
        "type": "event"
      }
    ]
     */

    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();

    ContractABIDefinitionFactory factory;
    auto contract = factory.buildABI(abi);

    BOOST_CHECK_EQUAL(contract->getMethod("get", true).size(), 2);
    BOOST_CHECK_THROW(contract->getMethod("get", false), std::runtime_error);

    BOOST_CHECK_EQUAL(contract->getEvent("SetResult", true).size(), 1);
    BOOST_CHECK_EQUAL(contract->getEvent("SetResult", false).size(), 1);

    BOOST_CHECK_EQUAL(contract->getMethod("set", true).size(), 1);
    BOOST_CHECK_EQUAL(contract->getMethod("set", false).size(), 1);

    BOOST_CHECK_EQUAL(contract->methods().size(), 3);
    BOOST_CHECK_EQUAL(contract->methodNames().size(), 3);

    // constructor
    {
        auto methods = factory.buildMethod(abi, "constructor");
        BOOST_CHECK_EQUAL(methods.size(), 1);
        BOOST_CHECK_EQUAL(methods[0]->type(), "constructor");

        auto constructor = contract->getMethod("constructor")[0];

        BOOST_CHECK_EQUAL(constructor->constant(), false);
        BOOST_CHECK_EQUAL(constructor->payable(), false);
        BOOST_CHECK_EQUAL(constructor->name(), "");
        BOOST_CHECK_EQUAL(constructor->anonymous(), false);
        BOOST_CHECK_EQUAL(constructor->stateMutability(), "nonpayable");
        BOOST_CHECK_EQUAL(constructor->inputs().size(), 0);
        BOOST_CHECK_EQUAL(constructor->outputs().size(), 0);

        bcos::cppsdk::abi::ContractABIDefinitionFactory contractABIDefinitionFactory;
        auto inputStruct = contractABIDefinitionFactory.buildInput(*constructor);
        BOOST_CHECK_EQUAL(inputStruct->memberSize(), 0);
        BOOST_CHECK_EQUAL(inputStruct->value().size(), 0);
        BOOST_CHECK_EQUAL(inputStruct->kvValue().size(), 0);

        auto outputStruct = contractABIDefinitionFactory.buildOutput(*constructor);
        BOOST_CHECK_EQUAL(outputStruct->memberSize(), 0);
        BOOST_CHECK_EQUAL(outputStruct->value().size(), 0);
        BOOST_CHECK_EQUAL(outputStruct->kvValue().size(), 0);
    }

    // get
    {
        auto methods = factory.buildMethod(abi, "get");
        BOOST_CHECK_EQUAL(methods.size(), 2);
        BOOST_CHECK_EQUAL(methods[0]->name(), "get");
        BOOST_CHECK_EQUAL(methods[1]->name(), "get");

        auto gets = contract->getMethod("get", true);

        // overwrite method
        BOOST_CHECK_EQUAL(gets.size(), 2);
        auto get0 = gets[0];
        auto get1 = gets[1];

        {
            auto get = gets[0];
            BOOST_CHECK_EQUAL(get->constant(), true);
            BOOST_CHECK_EQUAL(get->payable(), false);
            BOOST_CHECK_EQUAL(get->name(), "get");
            BOOST_CHECK_EQUAL(get->type(), "function");
            BOOST_CHECK_EQUAL(get->anonymous(), false);
            BOOST_CHECK_EQUAL(get->stateMutability(), "view");
            BOOST_CHECK_EQUAL(get->inputs().size(), 1);
            BOOST_CHECK_EQUAL(get->outputs().size(), 3);

            bcos::cppsdk::abi::ContractABIDefinitionFactory contractABIDefinitionFactory;
            auto inputStruct = contractABIDefinitionFactory.buildInput(*get);
            BOOST_CHECK_EQUAL(inputStruct->memberSize(), 1);
            BOOST_CHECK_EQUAL(inputStruct->value().size(), 1);
            BOOST_CHECK_EQUAL(inputStruct->kvValue().size(), 1);

            BOOST_CHECK_EQUAL(inputStruct->value()[0]->name(), "id");
            BOOST_CHECK(inputStruct->value()[0]->type() == Type::STRING);
            BOOST_CHECK(inputStruct->value()[0]->dynamicType());

            auto outputStruct = contractABIDefinitionFactory.buildOutput(*get);
            BOOST_CHECK_EQUAL(outputStruct->memberSize(), 3);
            BOOST_CHECK_EQUAL(outputStruct->value().size(), 3);
            BOOST_CHECK_EQUAL(outputStruct->kvValue().size(), 0);

            BOOST_CHECK_EQUAL(outputStruct->value()[0]->name(), "");
            BOOST_CHECK(outputStruct->value()[0]->type() == Type::BOOL);
            BOOST_CHECK_EQUAL(outputStruct->value()[1]->name(), "");
            BOOST_CHECK(outputStruct->value()[1]->type() == Type::INT);
            BOOST_CHECK_EQUAL(outputStruct->value()[2]->name(), "");
            BOOST_CHECK(outputStruct->value()[2]->type() == Type::STRING);
        }

        {
            auto get = gets[1];
            BOOST_CHECK_EQUAL(get->constant(), true);
            BOOST_CHECK_EQUAL(get->payable(), false);
            BOOST_CHECK_EQUAL(get->name(), "get");
            BOOST_CHECK_EQUAL(get->type(), "function");
            BOOST_CHECK_EQUAL(get->anonymous(), false);
            BOOST_CHECK_EQUAL(get->stateMutability(), "view");
            BOOST_CHECK_EQUAL(get->inputs().size(), 1);
            BOOST_CHECK_EQUAL(get->outputs().size(), 3);

            bcos::cppsdk::abi::ContractABIDefinitionFactory contractABIDefinitionFactory;
            auto inputStruct = contractABIDefinitionFactory.buildInput(*get);
            BOOST_CHECK_EQUAL(inputStruct->memberSize(), 1);
            BOOST_CHECK_EQUAL(inputStruct->value().size(), 1);
            BOOST_CHECK_EQUAL(inputStruct->kvValue().size(), 1);

            BOOST_CHECK_EQUAL(inputStruct->value()[0]->name(), "age");
            BOOST_CHECK(inputStruct->value()[0]->type() == Type::UINT);

            auto outputStruct = contractABIDefinitionFactory.buildOutput(*get);
            BOOST_CHECK_EQUAL(outputStruct->memberSize(), 3);
            BOOST_CHECK_EQUAL(outputStruct->value().size(), 3);
            BOOST_CHECK_EQUAL(outputStruct->kvValue().size(), 0);

            BOOST_CHECK_EQUAL(outputStruct->value()[0]->name(), "");
            BOOST_CHECK(outputStruct->value()[0]->type() == Type::BOOL);
            BOOST_CHECK_EQUAL(outputStruct->value()[1]->name(), "");
            BOOST_CHECK(outputStruct->value()[1]->type() == Type::INT);
            BOOST_CHECK_EQUAL(outputStruct->value()[2]->name(), "");
            BOOST_CHECK(outputStruct->value()[2]->type() == Type::STRING);
        }
    }

    // set
    {
        auto methods = factory.buildMethod(abi, "set");
        BOOST_CHECK_EQUAL(methods.size(), 1);
        BOOST_CHECK_EQUAL(methods[0]->name(), "set");

        auto set = contract->getMethod("set")[0];

        BOOST_CHECK_EQUAL(set->constant(), false);
        BOOST_CHECK_EQUAL(set->payable(), false);
        BOOST_CHECK_EQUAL(set->name(), "set");
        BOOST_CHECK_EQUAL(set->type(), "function");
        BOOST_CHECK_EQUAL(set->anonymous(), false);
        BOOST_CHECK_EQUAL(set->stateMutability(), "nonpayable");
        BOOST_CHECK_EQUAL(set->inputs().size(), 3);
        BOOST_CHECK_EQUAL(set->outputs().size(), 1);

        bcos::cppsdk::abi::ContractABIDefinitionFactory contractABIDefinitionFactory;
        auto inputStruct = contractABIDefinitionFactory.buildInput(*set);

        BOOST_CHECK_EQUAL(inputStruct->memberSize(), 3);
        BOOST_CHECK_EQUAL(inputStruct->value().size(), 3);
        BOOST_CHECK_EQUAL(inputStruct->kvValue().size(), 3);

        BOOST_CHECK(inputStruct->kvValue().find("id") != inputStruct->kvValue().end());
        BOOST_CHECK(inputStruct->kvValue().find("item_price") != inputStruct->kvValue().end());
        BOOST_CHECK(inputStruct->kvValue().find("item_name") != inputStruct->kvValue().end());


        BOOST_CHECK_EQUAL(inputStruct->value()[0]->name(), "id");
        BOOST_CHECK(inputStruct->value()[0]->type() == Type::STRING);
        BOOST_CHECK_EQUAL(inputStruct->value()[1]->name(), "item_price");
        BOOST_CHECK(inputStruct->value()[1]->type() == Type::INT);
        BOOST_CHECK_EQUAL(inputStruct->value()[2]->name(), "item_name");
        BOOST_CHECK(inputStruct->value()[2]->type() == Type::STRING);

        auto outputStruct = contractABIDefinitionFactory.buildOutput(*set);
        BOOST_CHECK_EQUAL(outputStruct->memberSize(), 1);
        BOOST_CHECK_EQUAL(outputStruct->value().size(), 1);
        BOOST_CHECK_EQUAL(outputStruct->kvValue().size(), 0);

        BOOST_CHECK_EQUAL(outputStruct->value()[0]->name(), "");
        BOOST_CHECK(outputStruct->value()[0]->type() == Type::INT);
    }

    // SetResult
    {
        auto setResult = contract->getEvent("SetResult")[0];

        BOOST_CHECK_EQUAL(setResult->constant(), false);
        BOOST_CHECK_EQUAL(setResult->payable(), false);
        BOOST_CHECK_EQUAL(setResult->name(), "SetResult");
        BOOST_CHECK_EQUAL(setResult->isEvent(), true);
        BOOST_CHECK_EQUAL(setResult->type(), "event");
        BOOST_CHECK_EQUAL(setResult->anonymous(), false);
        BOOST_CHECK_EQUAL(setResult->stateMutability(), "");
        BOOST_CHECK_EQUAL(setResult->inputs().size(), 1);
        BOOST_CHECK_EQUAL(setResult->outputs().size(), 0);

        bcos::cppsdk::abi::ContractABIDefinitionFactory contractABIDefinitionFactory;
        auto inputStruct = contractABIDefinitionFactory.buildInput(*setResult);

        BOOST_CHECK_EQUAL(inputStruct->memberSize(), 1);
        BOOST_CHECK_EQUAL(inputStruct->value().size(), 1);
        BOOST_CHECK_EQUAL(inputStruct->kvValue().size(), 1);

        BOOST_CHECK(inputStruct->kvValue().find("count") != inputStruct->kvValue().end());

        BOOST_CHECK_EQUAL(inputStruct->value()[0]->name(), "count");
        BOOST_CHECK(inputStruct->value()[0]->type() == Type::INT);

        auto outputStruct = contractABIDefinitionFactory.buildOutput(*setResult);
        BOOST_CHECK_EQUAL(outputStruct->memberSize(), 0);
        BOOST_CHECK_EQUAL(outputStruct->value().size(), 0);
        BOOST_CHECK_EQUAL(outputStruct->kvValue().size(), 0);
    }
}

BOOST_AUTO_TEST_CASE(test_loadABI_SolTypes)
{
    /*
    [
      {
        "constant": false,
        "inputs": [
          {
            "name": "_u",
            "type": "uint256[]"
          },
          {
            "name": "_b",
            "type": "bool[]"
          },
          {
            "name": "_addr",
            "type": "address[]"
          },
          {
            "name": "_bs32",
            "type": "bytes32[]"
          },
          {
            "name": "_s",
            "type": "string[]"
          },
          {
            "name": "_bs",
            "type": "bytes[]"
          }
        ],
        "name": "setDynamicValue",
        "outputs": [

        ],
        "payable": false,
        "stateMutability": "nonpayable",
        "type": "function"
      },
      {
        "constant": false,
        "inputs": [
          {
            "name": "_u",
            "type": "uint256[3]"
          },
          {
            "name": "_b",
            "type": "bool[3]"
          },
          {
            "name": "_addr",
            "type": "address[3]"
          },
          {
            "name": "_bs32",
            "type": "bytes32[3]"
          },
          {
            "name": "_s",
            "type": "string[3]"
          },
          {
            "name": "_bs",
            "type": "bytes[3]"
          }
        ],
        "name": "setFixedValue",
        "outputs": [

        ],
        "payable": false,
        "stateMutability": "nonpayable",
        "type": "function"
      },
      {
        "constant": false,
        "inputs": [
          {
            "name": "_u",
            "type": "uint256"
          },
          {
            "name": "_b",
            "type": "bool"
          },
          {
            "name": "_addr",
            "type": "address"
          },
          {
            "name": "_bs32",
            "type": "bytes32"
          },
          {
            "name": "_s",
            "type": "string"
          },
          {
            "name": "_bs",
            "type": "bytes"
          }
        ],
        "name": "setValue",
        "outputs": [

        ],
        "payable": false,
        "stateMutability": "nonpayable",
        "type": "function"
      },
      {
        "constant": true,
        "inputs": [

        ],
        "name": "getDynamicValue",
        "outputs": [
          {
            "name": "_u",
            "type": "uint256[]"
          },
          {
            "name": "_b",
            "type": "bool[]"
          },
          {
            "name": "_addr",
            "type": "address[]"
          },
          {
            "name": "_bs32",
            "type": "bytes32[]"
          },
          {
            "name": "_s",
            "type": "string[]"
          },
          {
            "name": "_bs",
            "type": "bytes[]"
          }
        ],
        "payable": false,
        "stateMutability": "view",
        "type": "function"
      },
      {
        "constant": true,
        "inputs": [

        ],
        "name": "getFixedValue",
        "outputs": [
          {
            "name": "_u",
            "type": "uint256[3]"
          },
          {
            "name": "_b",
            "type": "bool[3]"
          },
          {
            "name": "_addr",
            "type": "address[3]"
          },
          {
            "name": "_bs32",
            "type": "bytes32[3]"
          },
          {
            "name": "_s",
            "type": "string[3]"
          },
          {
            "name": "_bs",
            "type": "bytes[3]"
          }
        ],
        "payable": false,
        "stateMutability": "view",
        "type": "function"
      },
      {
        "constant": true,
        "inputs": [

        ],
        "name": "getValue",
        "outputs": [
          {
            "name": "_u",
            "type": "uint256"
          },
          {
            "name": "_b",
            "type": "bool"
          },
          {
            "name": "_addr",
            "type": "address"
          },
          {
            "name": "_bs32",
            "type": "bytes32"
          },
          {
            "name": "_s",
            "type": "string"
          },
          {
            "name": "_bs",
            "type": "bytes"
          }
        ],
        "payable": false,
        "stateMutability": "view",
        "type": "function"
      }
    ]*/

    /*
    {
        "20965255": "getValue()",
        "ed4d0e39": "getDynamicValue()",
        "c1cee39a": "getFixedValue()",
        "dfed87e3": "setDynamicValue(uint256[],bool[],address[],bytes32[],string[],bytes[])",
        "63e5584b": "setFixedValue(uint256[3],bool[3],address[3],bytes32[3],string[3],bytes[3])",
        "11cfbe17": "setValue(uint256,bool,address,bytes32,string,bytes)"
    }
    */

    std::string abi =
        "[{\"constant\":false,\"inputs\":[{\"name\":\"_u\",\"type\":\"uint256[]\"},{\"name\":\"_"
        "b\",\"type\":\"bool[]\"},{\"name\":\"_addr\",\"type\":\"address[]\"},{\"name\":\"_bs32\","
        "\"type\":\"bytes32[]\"},{\"name\":\"_s\",\"type\":\"string[]\"},{\"name\":\"_bs\","
        "\"type\":\"bytes[]\"}],\"name\":\"setDynamicValue\",\"outputs\":[],\"payable\":false,"
        "\"stateMutability\":\"nonpayable\",\"type\":\"function\"},{\"constant\":false,\"inputs\":["
        "{\"name\":\"_u\",\"type\":\"uint256[3]\"},{\"name\":\"_b\",\"type\":\"bool[3]\"},{"
        "\"name\":\"_addr\",\"type\":\"address[3]\"},{\"name\":\"_bs32\",\"type\":\"bytes32[3]\"},{"
        "\"name\":\"_s\",\"type\":\"string[3]\"},{\"name\":\"_bs\",\"type\":\"bytes[3]\"}],"
        "\"name\":\"setFixedValue\",\"outputs\":[],\"payable\":false,\"stateMutability\":"
        "\"nonpayable\",\"type\":\"function\"},{\"constant\":false,\"inputs\":[{\"name\":\"_u\","
        "\"type\":\"uint256\"},{\"name\":\"_b\",\"type\":\"bool\"},{\"name\":\"_addr\",\"type\":"
        "\"address\"},{\"name\":\"_bs32\",\"type\":\"bytes32\"},{\"name\":\"_s\",\"type\":"
        "\"string\"},{\"name\":\"_bs\",\"type\":\"bytes\"}],\"name\":\"setValue\",\"outputs\":[],"
        "\"payable\":false,\"stateMutability\":\"nonpayable\",\"type\":\"function\"},{\"constant\":"
        "true,\"inputs\":[],\"name\":\"getDynamicValue\",\"outputs\":[{\"name\":\"_u\",\"type\":"
        "\"uint256[]\"},{\"name\":\"_b\",\"type\":\"bool[]\"},{\"name\":\"_addr\",\"type\":"
        "\"address[]\"},{\"name\":\"_bs32\",\"type\":\"bytes32[]\"},{\"name\":\"_s\",\"type\":"
        "\"string[]\"},{\"name\":\"_bs\",\"type\":\"bytes[]\"}],\"payable\":false,"
        "\"stateMutability\":\"view\",\"type\":\"function\"},{\"constant\":true,\"inputs\":[],"
        "\"name\":\"getFixedValue\",\"outputs\":[{\"name\":\"_u\",\"type\":\"uint256[3]\"},{"
        "\"name\":\"_b\",\"type\":\"bool[3]\"},{\"name\":\"_addr\",\"type\":\"address[3]\"},{"
        "\"name\":\"_bs32\",\"type\":\"bytes32[3]\"},{\"name\":\"_s\",\"type\":\"string[3]\"},{"
        "\"name\":\"_bs\",\"type\":\"bytes[3]\"}],\"payable\":false,\"stateMutability\":\"view\","
        "\"type\":\"function\"},{\"constant\":true,\"inputs\":[],\"name\":\"getValue\",\"outputs\":"
        "[{\"name\":\"_u\",\"type\":\"uint256\"},{\"name\":\"_b\",\"type\":\"bool\"},{\"name\":\"_"
        "addr\",\"type\":\"address\"},{\"name\":\"_bs32\",\"type\":\"bytes32\"},{\"name\":\"_s\","
        "\"type\":\"string\"},{\"name\":\"_bs\",\"type\":\"bytes\"}],\"payable\":false,"
        "\"stateMutability\":\"view\",\"type\":\"function\"}]";

    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    ContractABIDefinitionFactory factory;
    auto contractABIDefinition = factory.buildABI(abi);

    auto names = contractABIDefinition->methodNames();
    auto ids = contractABIDefinition->methods();

    BOOST_CHECK_EQUAL(ids.size(), 6);
    BOOST_CHECK_EQUAL(names.size(), 6);

    BOOST_CHECK(std::find(names.begin(), names.end(), "getValue") != names.end());
    BOOST_CHECK(std::find(names.begin(), names.end(), "getDynamicValue") != names.end());
    BOOST_CHECK(std::find(names.begin(), names.end(), "getFixedValue") != names.end());
    BOOST_CHECK(std::find(names.begin(), names.end(), "setDynamicValue") != names.end());
    BOOST_CHECK(std::find(names.begin(), names.end(), "setFixedValue") != names.end());
    BOOST_CHECK(std::find(names.begin(), names.end(), "setValue") != names.end());

    {
        auto methods = factory.buildMethod(abi, "getValue");
        BOOST_CHECK_EQUAL(methods.size(), 1);
        BOOST_CHECK_EQUAL(methods[0]->name(), "getValue");

        std::string methodID = "20965255";
        auto method = contractABIDefinition->getMethod("getValue", false);
        BOOST_CHECK_EQUAL(method.size(), 1);

        BOOST_CHECK_EQUAL(method[0]->name(), "getValue");
        BOOST_CHECK_EQUAL(method[0]->type(), "function");
        BOOST_CHECK_EQUAL(method[0]->constant(), true);
        BOOST_CHECK_EQUAL(method[0]->anonymous(), false);
        BOOST_CHECK_EQUAL(method[0]->isEvent(), false);
        BOOST_CHECK_EQUAL(method[0]->payable(), false);
        BOOST_CHECK_EQUAL(method[0]->stateMutability(), "view");
        BOOST_CHECK_EQUAL(method[0]->inputs().size(), 0);
        BOOST_CHECK_EQUAL(method[0]->outputs().size(), 6);
        BOOST_CHECK_EQUAL(method[0]->getMethodSignatureAsString(), "getValue()");
        BOOST_CHECK_EQUAL(method[0]->getMethodIDAsString(hashImpl), methodID);
        BOOST_CHECK_EQUAL(
            contractABIDefinition->getMethodByMethodID(methodID, hashImpl)->name(), "getValue");
    }

    {
        std::string methodID = "ed4d0e39";
        auto method = contractABIDefinition->getMethod("getDynamicValue", false);
        BOOST_CHECK_EQUAL(method.size(), 1);

        BOOST_CHECK_EQUAL(method[0]->name(), "getDynamicValue");
        BOOST_CHECK_EQUAL(method[0]->type(), "function");
        BOOST_CHECK_EQUAL(method[0]->constant(), true);
        BOOST_CHECK_EQUAL(method[0]->anonymous(), false);
        BOOST_CHECK_EQUAL(method[0]->isEvent(), false);
        BOOST_CHECK_EQUAL(method[0]->payable(), false);
        BOOST_CHECK_EQUAL(method[0]->stateMutability(), "view");
        BOOST_CHECK_EQUAL(method[0]->inputs().size(), 0);
        BOOST_CHECK_EQUAL(method[0]->outputs().size(), 6);
        BOOST_CHECK_EQUAL(method[0]->getMethodSignatureAsString(), "getDynamicValue()");
        BOOST_CHECK_EQUAL(method[0]->getMethodIDAsString(hashImpl), methodID);
        BOOST_CHECK_EQUAL(contractABIDefinition->getMethodByMethodID(methodID, hashImpl)->name(),
            "getDynamicValue");
    }

    {
        auto methods = factory.buildMethod(abi, "getFixedValue");
        BOOST_CHECK_EQUAL(methods.size(), 1);
        BOOST_CHECK_EQUAL(methods[0]->name(), "getFixedValue");

        std::string methodID = "c1cee39a";
        auto method = contractABIDefinition->getMethod("getFixedValue", false);
        BOOST_CHECK_EQUAL(method.size(), 1);

        BOOST_CHECK_EQUAL(method[0]->name(), "getFixedValue");
        BOOST_CHECK_EQUAL(method[0]->type(), "function");
        BOOST_CHECK_EQUAL(method[0]->constant(), true);
        BOOST_CHECK_EQUAL(method[0]->anonymous(), false);
        BOOST_CHECK_EQUAL(method[0]->isEvent(), false);
        BOOST_CHECK_EQUAL(method[0]->payable(), false);
        BOOST_CHECK_EQUAL(method[0]->stateMutability(), "view");
        BOOST_CHECK_EQUAL(method[0]->inputs().size(), 0);
        BOOST_CHECK_EQUAL(method[0]->outputs().size(), 6);
        BOOST_CHECK_EQUAL(method[0]->getMethodSignatureAsString(), "getFixedValue()");
        BOOST_CHECK_EQUAL(method[0]->getMethodIDAsString(hashImpl), methodID);

        BOOST_CHECK_EQUAL(contractABIDefinition->getMethodByMethodID(methodID, hashImpl)->name(),
            "getFixedValue");
    }

    {
        auto methods = factory.buildMethod(abi, "setDynamicValue");
        BOOST_CHECK_EQUAL(methods.size(), 1);
        BOOST_CHECK_EQUAL(methods[0]->name(), "setDynamicValue");

        std::string methodID = "dfed87e3";
        auto method = contractABIDefinition->getMethod("setDynamicValue", false);
        BOOST_CHECK_EQUAL(method.size(), 1);

        BOOST_CHECK_EQUAL(method[0]->name(), "setDynamicValue");
        BOOST_CHECK_EQUAL(method[0]->type(), "function");
        BOOST_CHECK_EQUAL(method[0]->constant(), false);
        BOOST_CHECK_EQUAL(method[0]->anonymous(), false);
        BOOST_CHECK_EQUAL(method[0]->isEvent(), false);
        BOOST_CHECK_EQUAL(method[0]->payable(), false);
        BOOST_CHECK_EQUAL(method[0]->stateMutability(), "nonpayable");
        BOOST_CHECK_EQUAL(method[0]->inputs().size(), 6);
        BOOST_CHECK_EQUAL(method[0]->outputs().size(), 0);
        BOOST_CHECK_EQUAL(method[0]->getMethodSignatureAsString(),
            "setDynamicValue(uint256[],bool[],address[],bytes32[],string[],bytes[])");
        BOOST_CHECK_EQUAL(method[0]->getMethodIDAsString(hashImpl), methodID);

        BOOST_CHECK_EQUAL(contractABIDefinition->getMethodByMethodID(methodID, hashImpl)->name(),
            "setDynamicValue");

        std::string params = "[[],[],[],[],[],[]]";

        auto solcImpl = std::make_shared<ContractABITypeCodecSolImpl>();
        ContractABICodec codec(hashImpl, solcImpl);

        auto buffer = codec.encodeMethod(*method[0], params);
        BOOST_CHECK_EQUAL(
            "dfed87e300000000000000000000000000000000000000000000000000000000000000c000000000000000"
            "000000000000000000000000000000000000000000000000e0000000000000000000000000000000000000"
            "00000000000000000000000001000000000000000000000000000000000000000000000000000000000000"
            "00012000000000000000000000000000000000000000000000000000000000000001400000000000000000"
            "00000000000000000000000000000000000000000000016000000000000000000000000000000000000000"
            "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
            "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
            "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
            "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
            "00",
            *toHexString(buffer));
        auto decodeParams = codec.decodeMethodInput(*method[0], buffer);
        BOOST_CHECK_EQUAL(decodeParams, params);
    }

    {
        auto methods = factory.buildMethod(abi, "setFixedValue");
        BOOST_CHECK_EQUAL(methods.size(), 1);
        BOOST_CHECK_EQUAL(methods[0]->name(), "setFixedValue");

        std::string methodID = "63e5584b";
        auto method = contractABIDefinition->getMethod("setFixedValue", false);
        BOOST_CHECK_EQUAL(method.size(), 1);

        BOOST_CHECK_EQUAL(method[0]->name(), "setFixedValue");
        BOOST_CHECK_EQUAL(method[0]->type(), "function");
        BOOST_CHECK_EQUAL(method[0]->constant(), false);
        BOOST_CHECK_EQUAL(method[0]->anonymous(), false);
        BOOST_CHECK_EQUAL(method[0]->isEvent(), false);
        BOOST_CHECK_EQUAL(method[0]->payable(), false);
        BOOST_CHECK_EQUAL(method[0]->stateMutability(), "nonpayable");
        BOOST_CHECK_EQUAL(method[0]->inputs().size(), 6);
        BOOST_CHECK_EQUAL(method[0]->outputs().size(), 0);
        BOOST_CHECK_EQUAL(method[0]->getMethodSignatureAsString(),
            "setFixedValue(uint256[3],bool[3],address[3],bytes32[3],string[3],bytes[3])");
        BOOST_CHECK_EQUAL(method[0]->getMethodIDAsString(hashImpl), methodID);

        BOOST_CHECK_EQUAL(contractABIDefinition->getMethodByMethodID(methodID, hashImpl)->name(),
            "setFixedValue");
    }

    {
        auto methods = factory.buildMethod(abi, "setValue");
        BOOST_CHECK_EQUAL(methods.size(), 1);
        BOOST_CHECK_EQUAL(methods[0]->name(), "setValue");

        std::string methodID = "11cfbe17";
        auto method = contractABIDefinition->getMethod("setValue", false);
        BOOST_CHECK_EQUAL(method.size(), 1);

        BOOST_CHECK_EQUAL(method[0]->name(), "setValue");
        BOOST_CHECK_EQUAL(method[0]->type(), "function");
        BOOST_CHECK_EQUAL(method[0]->constant(), false);
        BOOST_CHECK_EQUAL(method[0]->anonymous(), false);
        BOOST_CHECK_EQUAL(method[0]->isEvent(), false);
        BOOST_CHECK_EQUAL(method[0]->payable(), false);
        BOOST_CHECK_EQUAL(method[0]->stateMutability(), "nonpayable");
        BOOST_CHECK_EQUAL(method[0]->inputs().size(), 6);
        BOOST_CHECK_EQUAL(method[0]->outputs().size(), 0);
        BOOST_CHECK_EQUAL(method[0]->getMethodSignatureAsString(),
            "setValue(uint256,bool,address,bytes32,string,bytes)");
        BOOST_CHECK_EQUAL(method[0]->getMethodIDAsString(hashImpl), methodID);

        BOOST_CHECK_EQUAL(
            contractABIDefinition->getMethodByMethodID(methodID, hashImpl)->name(), "setValue");
    }
}

BOOST_AUTO_TEST_SUITE_END()
