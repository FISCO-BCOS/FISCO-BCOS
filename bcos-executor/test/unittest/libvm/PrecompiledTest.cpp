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

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
