/**
 *  Copyright (C) 2023 FISCO BCOS.
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
 * @file PrecompiledTest.cpp
 * @author: Jimmy Shi
 * @date 2024/1/11
 */

#include "vm/Precompiled.h"
#include <boost/test/unit_test.hpp>

namespace bcos::test
{
class PrecompiledTestFixture
{
public:
    PrecompiledTestFixture() {}
};

BOOST_FIXTURE_TEST_SUITE(testPercompiled, PrecompiledTestFixture)

BOOST_AUTO_TEST_CASE(ecrecoverSuccessTest)
{
    bytes params = *bcos::fromHexString(
        "aa0f7414b7f8648410f9818df3a1f43419d5c30313f430712033937ae57854c8"    // hash
        "000000000000000000000000000000000000000000000000000000000000001c"    // v
        "acd0d6c91242e514655815073f5f0e9aed671f68a4ed3e3e9d693095779f704b"    // r
        "01932751f4431c3b4c9d6fb1c826d138ee155ea72ac9013d66929f6a265386b4");  // s

    auto [success, addressBytes] = crypto::ecRecover(ref(params));
    BOOST_CHECK(success);
    auto address =
        *bcos::toHexString(addressBytes.data() + 12, addressBytes.data() + 12 + 20, "0x");
    // 0x6DA0599583855F1618B380f6782c0c5C25CB96Ec lower cases
    BOOST_CHECK_EQUAL(address, "0x6da0599583855f1618b380f6782c0c5c25cb96ec");
}

BOOST_AUTO_TEST_CASE(ecrecoverSuccessTest2)
{
    bytes params = *bcos::fromHexString(
        "aa0f7414b7f8648410f9818df3a1f43419d5c30313f430712033937ae57854c8"    // hash
        "000000000000000000000000000000000000000000000000000000000000001b"    // v
        "acd0d6c91242e514655815073f5f0e9aed671f68a4ed3e3e9d693095779f704b"    // r
        "01932751f4431c3b4c9d6fb1c826d138ee155ea72ac9013d66929f6a265386b4");  // s

    auto [success, addressBytes] = crypto::ecRecover(ref(params));
    BOOST_CHECK(success);
    auto address =
        *bcos::toHexString(addressBytes.data() + 12, addressBytes.data() + 12 + 20, "0x");
    // 0xA5b4792dcAD4fE78D13f6Abd7BA1F302945DE4f7 lower cases
    BOOST_CHECK_EQUAL(address, "0xa5b4792dcad4fe78d13f6abd7ba1f302945de4f7");
}

BOOST_AUTO_TEST_CASE(ecrecoverSuccessLargerTest)
{
    bytes params = *bcos::fromHexString(
        "aa0f7414b7f8648410f9818df3a1f43419d5c30313f430712033937ae57854c8"  // hash
        "000000000000000000000000000000000000000000000000000000000000001b"  // v
        "acd0d6c91242e514655815073f5f0e9aed671f68a4ed3e3e9d693095779f704b"  // r
        "01932751f4431c3b4c9d6fb1c826d138ee155ea72ac9013d66929f6a265386b4"  // s
        "aabbccddeeff");                                                    // no used extra data

    auto [success, addressBytes] = crypto::ecRecover(ref(params));
    BOOST_CHECK(success);
    auto address =
        *bcos::toHexString(addressBytes.data() + 12, addressBytes.data() + 12 + 20, "0x");
    // 0xA5b4792dcAD4fE78D13f6Abd7BA1F302945DE4f7 lower cases
    BOOST_CHECK_EQUAL(address, "0xa5b4792dcad4fe78d13f6abd7ba1f302945de4f7");
}

BOOST_AUTO_TEST_CASE(ecrecoverFailedTest)
{
    bytes params = *bcos::fromHexString(
        "96d107f6"  // use selector by mistake "ecrecover(bytes32,uint8,bytes32,bytes32)"
        "aa0f7414b7f8648410f9818df3a1f43419d5c30313f430712033937ae57854c8"    // hash
        "000000000000000000000000000000000000000000000000000000000000001c"    // v
        "acd0d6c91242e514655815073f5f0e9aed671f68a4ed3e3e9d693095779f704b"    // r
        "01932751f4431c3b4c9d6fb1c826d138ee155ea72ac9013d66929f6a265386b4");  // s

    auto [success, addressBytes] = crypto::ecRecover(ref(params));
    BOOST_CHECK(success);               // also success
    BOOST_CHECK(addressBytes.empty());  // but return empty
}


BOOST_AUTO_TEST_CASE(ecrecoverInvalidVTest)
{
    bytes params = *bcos::fromHexString(
        "aa0f7414b7f8648410f9818df3a1f43419d5c30313f430712033937ae57854c8"    // hash
        "000000000000000000000000000000000000000000000000000000000000001a"    // v
        "acd0d6c91242e514655815073f5f0e9aed671f68a4ed3e3e9d693095779f704b"    // r
        "01932751f4431c3b4c9d6fb1c826d138ee155ea72ac9013d66929f6a265386b4");  // s

    auto [success, addressBytes] = crypto::ecRecover(ref(params));
    BOOST_CHECK(success);               // also success
    BOOST_CHECK(addressBytes.empty());  // but return empty
}

BOOST_AUTO_TEST_CASE(ecrecoverInvalidVTest2)
{
    bytes params = *bcos::fromHexString(
        "aa0f7414b7f8648410f9818df3a1f43419d5c30313f430712033937ae57854c8"    // hash
        "000000000000000000000000000000000000000000000000000000000000001d"    // v
        "acd0d6c91242e514655815073f5f0e9aed671f68a4ed3e3e9d693095779f704b"    // r
        "01932751f4431c3b4c9d6fb1c826d138ee155ea72ac9013d66929f6a265386b4");  // s

    auto [success, addressBytes] = crypto::ecRecover(ref(params));
    BOOST_CHECK(success);               // also success
    BOOST_CHECK(addressBytes.empty());  // but return empty
}

BOOST_AUTO_TEST_CASE(ecrecoverInvalidInputLenTest)
{
    bytes params = *bcos::fromHexString(
        "aa0f7414b7f8648410f9818df3a1f43419d5c30313f430712033937ae57854c8"    // hash
        "000000000000000000000000000000000000000000000000000000000000001c"    // v
        "acd0d6c91242e514655815073f5f0e9aed671f68a4ed3e3e9d693095779f704b"    // r
        "01932751f4431c3b4c9d6fb1c826d138ee155ea72ac9013d66929f6a26538600");  // s

    auto [success, addressBytes] = crypto::ecRecover(ref(params));
    BOOST_CHECK(success);
    auto address =
        *bcos::toHexString(addressBytes.data() + 12, addressBytes.data() + 12 + 20, "0x");
    // 0x509eAd8B20064f21E35f920cB0c6d6cBC0C0Aa0d lower cases
    BOOST_CHECK_EQUAL(address, "0x509ead8b20064f21e35f920cb0c6d6cbc0c0aa0d");
}

BOOST_AUTO_TEST_CASE(ecrecoverInvalidInputLenTest2)
{
    bytes params = *bcos::fromHexString(
        "aa0f7414b7f8648410f9818df3a1f43419d5c30313f430712033937ae57854c8"    // hash
        "000000000000000000000000000000000000000000000000000000000000001c"    // v
        "acd0d6c91242e514655815073f5f0e9aed671f68a4ed3e3e9d693095779f704b");  // s

    auto [success, addressBytes] = crypto::ecRecover(ref(params));
    BOOST_CHECK(success);               // also success
    BOOST_CHECK(addressBytes.empty());  // but return empty
}

BOOST_AUTO_TEST_CASE(fixedArray)
{
    std::string_view lhs{"0000000000000000000000000000000000010003"};
    evmc_address address = unhexAddress(lhs);
    auto fixedArray = bcos::address2FixedArray(address);
    std::string_view rhs(fixedArray.data(), fixedArray.size());
    BOOST_CHECK_EQUAL(lhs, rhs);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
