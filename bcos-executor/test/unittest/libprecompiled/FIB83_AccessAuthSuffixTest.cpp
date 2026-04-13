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
 * @file FIB83_AccessAuthSuffixTest.cpp
 * @brief Regression tests for FIB-83: BFS should reject paths ending with _accessAuth suffix
 * @date 2026-04-07
 */

#include "bcos-executor/src/Common.h"
#include "bcos-executor/src/precompiled/common/Utilities.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/Protocol.h"
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::protocol;

BOOST_AUTO_TEST_SUITE(FIB83_AccessAuthSuffixTest)

BOOST_AUTO_TEST_CASE(rejectAccessAuthSuffix_WithFlag)
{
    // Set up features with bugfix_auth_table_squatting enabled
    ledger::Features features;
    features.set(ledger::Features::Flag::bugfix_auth_table_squatting);

    // Paths ending with _accessAuth should be rejected when the flag is on
    BOOST_CHECK(
        !checkPathValid("/apps/contract_accessAuth", BlockVersion::V3_16_5_VERSION, &features));
    BOOST_CHECK(!checkPathValid(
        "/apps/aabbccddee1234_accessAuth", BlockVersion::V3_16_5_VERSION, &features));
    BOOST_CHECK(!checkPathValid("/apps/_accessAuth", BlockVersion::V3_16_5_VERSION, &features));
}

BOOST_AUTO_TEST_CASE(allowAccessAuthSuffix_WithoutFlag)
{
    // Without the flag, paths ending with _accessAuth should pass (backward compat)
    ledger::Features noFlag;
    BOOST_CHECK(
        checkPathValid("/apps/contract_accessAuth", BlockVersion::V3_16_5_VERSION, &noFlag));

    // Even with nullptr features (e.g. ShardingPrecompiled path), should pass
    BOOST_CHECK(checkPathValid("/apps/contract_accessAuth", BlockVersion::V3_16_5_VERSION));
    BOOST_CHECK(checkPathValid("/apps/contract_accessAuth", BlockVersion::V3_2_VERSION));
    BOOST_CHECK(checkPathValid("/apps/contract_accessAuth", BlockVersion::V3_1_VERSION));
}

BOOST_AUTO_TEST_CASE(allowNonSuffixPaths_WithFlag)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::bugfix_auth_table_squatting);

    // Paths that don't end with _accessAuth should still be valid
    BOOST_CHECK(checkPathValid("/apps/mycontract", BlockVersion::V3_16_5_VERSION, &features));
    BOOST_CHECK(
        checkPathValid("/apps/accessAuth_contract", BlockVersion::V3_16_5_VERSION, &features));
    BOOST_CHECK(checkPathValid("/apps/my_accessAuthx", BlockVersion::V3_16_5_VERSION, &features));
}

BOOST_AUTO_TEST_CASE(rejectNestedAccessAuthSuffix)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::bugfix_auth_table_squatting);

    // Intermediate path components ending with _accessAuth should also be rejected
    BOOST_CHECK(
        !checkPathValid("/apps/contract_accessAuth/sub", BlockVersion::V3_16_5_VERSION, &features));
}

BOOST_AUTO_TEST_SUITE_END()
