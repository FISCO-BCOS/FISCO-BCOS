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
 * @file ContractEventTopicTest.cpp
 * @author: octopus
 * @date 2022-02-24
 */
#include <bcos-cpp-sdk/utilities/abi/ContractEventTopic.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <sys/types.h>
#include <boost/test/tools/old/interface.hpp>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace bcos;
using namespace bcos::codec::abi;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(ContractEventTopicTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(test_EventTopic_Keccak256)
{
    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    auto contactEventTopic = std::make_shared<bcos::codec::abi::ContractEventTopic>(hashImpl);

    // int
    {
        s256 i0 = 0;
        BOOST_CHECK_EQUAL("0x0000000000000000000000000000000000000000000000000000000000000000",
            contactEventTopic->i256ToTopic(i0));

        s256 i1 = 1;
        BOOST_CHECK_EQUAL("0x0000000000000000000000000000000000000000000000000000000000000001",
            contactEventTopic->i256ToTopic(i1));

        s256 i2 = 12345;
        BOOST_CHECK_EQUAL("0x0000000000000000000000000000000000000000000000000000000000003039",
            contactEventTopic->i256ToTopic(i2));

        s256 i3 = std::numeric_limits<int>::max();
        BOOST_CHECK_EQUAL("0x000000000000000000000000000000000000000000000000000000007fffffff",
            contactEventTopic->i256ToTopic(i3));

        s256 i4 = -1;
        BOOST_CHECK_EQUAL("0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
            contactEventTopic->i256ToTopic(i4));

        s256 i5 = -1234567890;
        BOOST_CHECK_EQUAL("0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffb669fd2e",
            contactEventTopic->i256ToTopic(i5));
    }

    // uint
    {
        u256 u0 = 0;
        BOOST_CHECK_EQUAL("0x0000000000000000000000000000000000000000000000000000000000000000",
            contactEventTopic->u256ToTopic(u0));

        u256 u1 = 1;
        BOOST_CHECK_EQUAL("0x0000000000000000000000000000000000000000000000000000000000000001",
            contactEventTopic->u256ToTopic(u1));

        u256 u2 = 12345;
        BOOST_CHECK_EQUAL("0x0000000000000000000000000000000000000000000000000000000000003039",
            contactEventTopic->u256ToTopic(u2));

        u256 u3 = std::numeric_limits<uint>::max();
        BOOST_CHECK_EQUAL("0x00000000000000000000000000000000000000000000000000000000ffffffff",
            contactEventTopic->u256ToTopic(u3));

        u256 u4 = -1;
        BOOST_CHECK_EQUAL("0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
            contactEventTopic->u256ToTopic(u4));

        u256 u5 = 1234567890;
        BOOST_CHECK_EQUAL("0x00000000000000000000000000000000000000000000000000000000499602d2",
            contactEventTopic->u256ToTopic(u5));
    }

    // string
    {
        std::string s0 = "";
        BOOST_CHECK_EQUAL("0xc5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470",
            contactEventTopic->stringToTopic(s0));

        std::string s1 = "HelloWorld";
        BOOST_CHECK_EQUAL("0x7c5ea36004851c764c44143b1dcb59679b11c9a68e5f41497f6cf3d480715331",
            contactEventTopic->stringToTopic(s1));

        std::string s2 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        BOOST_CHECK_EQUAL("0x6bf4107d5e7ac7a9c23a4e8d6581b098e5323fe49df3596168d3710d50526dad",
            contactEventTopic->stringToTopic(s2));
    }

    // bytesN
    {
        bcos::bytes bs0;
        BOOST_CHECK_EQUAL("0x0000000000000000000000000000000000000000000000000000000000000000",
            contactEventTopic->bytesNToTopic(bs0));

        bcos::bytes bs1(10, '1');
        BOOST_CHECK_EQUAL("0x3131313131313131313100000000000000000000000000000000000000000000",
            contactEventTopic->bytesNToTopic(bs1));

        bcos::bytes bs2(32, '1');
        BOOST_CHECK_EQUAL("0x3131313131313131313131313131313131313131313131313131313131313131",
            contactEventTopic->bytesNToTopic(bs2));

        bcos::bytes bs3(33, '1');
        BOOST_CHECK_THROW(contactEventTopic->bytesNToTopic(bs3), bcos::InvalidParameter);
    }

    // bytes
    {
        bcos::bytes bs0;
        BOOST_CHECK_EQUAL("0xc5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470",
            contactEventTopic->bytesToTopic(bs0));

        std::string hex = "0xc5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470";
        BOOST_CHECK_EQUAL("0xe166801d00a45901e2b3ca692a6a95e367d4a976218b485546a2da464b6c88b5",
            contactEventTopic->bytesToTopic(bcos::bytes(hex.begin(), hex.end())));
    }
}

BOOST_AUTO_TEST_CASE(test_EventTopic_SM3)
{
    auto hashImpl = std::make_shared<bcos::crypto::SM3>();
    auto contactEventTopic = std::make_shared<bcos::codec::abi::ContractEventTopic>(hashImpl);

    // int
    {
        s256 i0 = 0;
        BOOST_CHECK_EQUAL("0x0000000000000000000000000000000000000000000000000000000000000000",
            contactEventTopic->i256ToTopic(i0));

        s256 i1 = 1;
        BOOST_CHECK_EQUAL("0x0000000000000000000000000000000000000000000000000000000000000001",
            contactEventTopic->i256ToTopic(i1));

        s256 i2 = 12345;
        BOOST_CHECK_EQUAL("0x0000000000000000000000000000000000000000000000000000000000003039",
            contactEventTopic->i256ToTopic(i2));

        s256 i3 = std::numeric_limits<int>::max();
        BOOST_CHECK_EQUAL("0x000000000000000000000000000000000000000000000000000000007fffffff",
            contactEventTopic->i256ToTopic(i3));

        s256 i4 = -1;
        BOOST_CHECK_EQUAL("0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
            contactEventTopic->i256ToTopic(i4));

        s256 i5 = -1234567890;
        BOOST_CHECK_EQUAL("0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffb669fd2e",
            contactEventTopic->i256ToTopic(i5));
    }

    // uint
    {
        u256 u0 = 0;
        BOOST_CHECK_EQUAL("0x0000000000000000000000000000000000000000000000000000000000000000",
            contactEventTopic->u256ToTopic(u0));

        u256 u1 = 1;
        BOOST_CHECK_EQUAL("0x0000000000000000000000000000000000000000000000000000000000000001",
            contactEventTopic->u256ToTopic(u1));

        u256 u2 = 12345;
        BOOST_CHECK_EQUAL("0x0000000000000000000000000000000000000000000000000000000000003039",
            contactEventTopic->u256ToTopic(u2));

        u256 u3 = std::numeric_limits<uint>::max();
        BOOST_CHECK_EQUAL("0x00000000000000000000000000000000000000000000000000000000ffffffff",
            contactEventTopic->u256ToTopic(u3));

        u256 u4 = -1;
        BOOST_CHECK_EQUAL("0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
            contactEventTopic->u256ToTopic(u4));

        u256 u5 = 1234567890;
        BOOST_CHECK_EQUAL("0x00000000000000000000000000000000000000000000000000000000499602d2",
            contactEventTopic->u256ToTopic(u5));
    }

    // string
    {
        std::string s0 = "";
        BOOST_CHECK_EQUAL("0x1ab21d8355cfa17f8e61194831e81a8f22bec8c728fefb747ed035eb5082aa2b",
            contactEventTopic->stringToTopic(s0));

        std::string s1 = "HelloWorld";
        BOOST_CHECK_EQUAL("0x44526eeba9235bae33f2bab8ff1f9ca8965b59d58be82af8111f336a00c1c432",
            contactEventTopic->stringToTopic(s1));

        std::string s2 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        BOOST_CHECK_EQUAL("0x58cc358e7880b06962f996be258e454af7eecfd3455831dd690566c4bbe025b5",
            contactEventTopic->stringToTopic(s2));
    }

    // bytesN
    {
        bcos::bytes bs0;
        BOOST_CHECK_EQUAL("0x0000000000000000000000000000000000000000000000000000000000000000",
            contactEventTopic->bytesNToTopic(bs0));

        bcos::bytes bs1(10, '1');
        BOOST_CHECK_EQUAL("0x3131313131313131313100000000000000000000000000000000000000000000",
            contactEventTopic->bytesNToTopic(bs1));

        bcos::bytes bs2(32, '1');
        BOOST_CHECK_EQUAL("0x3131313131313131313131313131313131313131313131313131313131313131",
            contactEventTopic->bytesNToTopic(bs2));

        bcos::bytes bs3(33, '1');
        BOOST_CHECK_THROW(contactEventTopic->bytesNToTopic(bs3), bcos::InvalidParameter);
    }

    // bytes
    {
        bcos::bytes bs0;
        BOOST_CHECK_EQUAL("0x1ab21d8355cfa17f8e61194831e81a8f22bec8c728fefb747ed035eb5082aa2b",
            contactEventTopic->bytesToTopic(bs0));

        std::string hex = "0xc5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470";
        BOOST_CHECK_EQUAL("0x2cd6eb8aa7aed20ed9665df40f7b3ea261fb6555473d33aea100fe4cb5eda8f9",
            contactEventTopic->bytesToTopic(bcos::bytes(hex.begin(), hex.end())));
    }
}
BOOST_AUTO_TEST_SUITE_END()
