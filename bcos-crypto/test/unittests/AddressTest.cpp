/**
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file AddressTest.cpp
 * @author: kyonGuo
 * @date 2024/8/29
 */

/**
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
 * @brief test cases for keccak256/sm3
 * @file HashTest.h
 * @date 2021.03.04
 */
#include <bcos-crypto/ChecksumAddress.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>
#include <string>

using namespace bcos;
using namespace crypto;
using namespace std::string_view_literals;
namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(AddressTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(testCreatLegacyEVMAddress)
{
    {
        auto sender = fromHex("fbe0afcd7658ba86be41922059dd879c192d4c73"sv);
        auto newAddress = newLegacyEVMAddress(ref(sender), u256(0));
        BOOST_CHECK_EQUAL(newAddress, "c669eaad75042be84daaf9b461b0e868b9ac1871");
    }

    {
        auto sender = fromHex("f39Fd6e51aad88F6F4ce6aB8827279cffFb92266"sv);
        auto newAddress = newLegacyEVMAddress(ref(sender), u256(0));
        BOOST_CHECK_EQUAL(newAddress, "5fbdb2315678afecb367f032d93f642f64180aa3");
    }
}

BOOST_AUTO_TEST_CASE(testCreate2Address)
{
    // test cases from https://eips.ethereum.org/EIPS/eip-1014
    auto keccak256 = std::make_shared<Keccak256>();
    {
        bcos::bytes initCode = fromHexWithPrefix("0x00"sv);
        u256 salt("0x0000000000000000000000000000000000000000000000000000000000000000");
        auto address = newCreate2EVMAddress(
            keccak256, "0x0000000000000000000000000000000000000000"sv, ref(initCode), salt);
        toCheckSumAddress(address, keccak256);
        BOOST_CHECK_EQUAL(address, "4D1A2e2bB4F88F0250f26Ffff098B0b30B26BF38");
    }

    {
        bcos::bytes initCode = fromHexWithPrefix("0x00"sv);
        u256 salt("0x0000000000000000000000000000000000000000000000000000000000000000");
        auto address = newCreate2EVMAddress(
            keccak256, "0xdeadbeef00000000000000000000000000000000"sv, ref(initCode), salt);
        toCheckSumAddress(address, keccak256);
        BOOST_CHECK_EQUAL(address, "B928f69Bb1D91Cd65274e3c79d8986362984fDA3");
    }

    {
        bcos::bytes initCode = fromHexWithPrefix("0x00"sv);
        u256 salt("0x000000000000000000000000feed000000000000000000000000000000000000");
        auto address = newCreate2EVMAddress(
            keccak256, "0xdeadbeef00000000000000000000000000000000"sv, ref(initCode), salt);
        toCheckSumAddress(address, keccak256);
        BOOST_CHECK_EQUAL(address, "D04116cDd17beBE565EB2422F2497E06cC1C9833");
    }

    {
        bcos::bytes initCode = fromHexWithPrefix("0xdeadbeef"sv);
        u256 salt("0x0000000000000000000000000000000000000000000000000000000000000000");
        auto address = newCreate2EVMAddress(
            keccak256, "0x0000000000000000000000000000000000000000"sv, ref(initCode), salt);
        toCheckSumAddress(address, keccak256);
        BOOST_CHECK_EQUAL(address, "70f2b2914A2a4b783FaEFb75f459A580616Fcb5e");
    }

    {
        bcos::bytes initCode = fromHexWithPrefix("0xdeadbeef"sv);
        u256 salt("0x00000000000000000000000000000000000000000000000000000000cafebabe");
        auto address = newCreate2EVMAddress(
            keccak256, "0x00000000000000000000000000000000deadbeef"sv, ref(initCode), salt);
        toCheckSumAddress(address, keccak256);
        BOOST_CHECK_EQUAL(address, "60f3f640a8508fC6a86d45DF051962668E1e8AC7");
    }

    {
        bcos::bytes initCode = fromHexWithPrefix(
            "0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef"sv);
        u256 salt("0x00000000000000000000000000000000000000000000000000000000cafebabe");
        auto address = newCreate2EVMAddress(
            keccak256, "0x00000000000000000000000000000000deadbeef"sv, ref(initCode), salt);
        toCheckSumAddress(address, keccak256);
        BOOST_CHECK_EQUAL(address, "1d8bfDC5D46DC4f61D6b6115972536eBE6A8854C");
    }

    {
        bcos::bytes initCode = fromHexWithPrefix("0x"sv);
        u256 salt("0x0000000000000000000000000000000000000000000000000000000000000000");
        auto address = newCreate2EVMAddress(
            keccak256, "0x0000000000000000000000000000000000000000"sv, ref(initCode), salt);
        toCheckSumAddress(address, keccak256);
        BOOST_CHECK_EQUAL(address, "E33C0C7F7df4809055C3ebA6c09CFe4BaF1BD9e0");
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
