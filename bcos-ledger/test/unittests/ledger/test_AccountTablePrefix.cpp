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
 * @file test_AccountTablePrefix.cpp
 * @brief Matrix test for the system-address table-prefix policy (accountTablePrefix):
 *        every c_systemTxsAddress member routes to /sys/ under legacy semantics and to
 *        /apps/ under systemAsUser (v2/Ethereum) semantics; ordinary addresses always
 *        route to /apps/.
 */
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <boost/test/unit_test.hpp>
#include <string_view>

using namespace bcos;
using namespace bcos::ledger;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(AccountTablePrefixTest)

BOOST_AUTO_TEST_CASE(systemAddressesFollowFlag)
{
    for (auto const& address : precompiled::c_systemTxsAddress)
    {
        BOOST_TEST_INFO("address=" << address);
        BOOST_CHECK_EQUAL(
            account::accountTablePrefix(address, /*systemAsUser=*/false), SYS_DIRECTORY::SYS_APPS);
        BOOST_CHECK_EQUAL(
            account::accountTablePrefix(address, /*systemAsUser=*/true), SYS_DIRECTORY::USER_APPS);
    }
}

BOOST_AUTO_TEST_CASE(userAddressesAlwaysApps)
{
    // Two ordinary 40-hex addresses that are NOT in c_systemTxsAddress.
    constexpr std::string_view user1 = "f39fd6e51aad88f6f4ce6ab8827279cfffb92266";
    constexpr std::string_view user2 = "00000000000000000000000000000000000abcde";
    for (auto address : {user1, user2})
    {
        BOOST_TEST_INFO("address=" << address);
        BOOST_CHECK_EQUAL(
            account::accountTablePrefix(address, /*systemAsUser=*/false), SYS_DIRECTORY::USER_APPS);
        BOOST_CHECK_EQUAL(
            account::accountTablePrefix(address, /*systemAsUser=*/true), SYS_DIRECTORY::USER_APPS);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
