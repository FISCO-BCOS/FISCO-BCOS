/*
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
 * @file test_FeatureL2Flag.cpp
 * @brief round-trip test for the feature_l2_ethereum_compat flag (A6.0)
 */
#include <bcos-framework/ledger/Features.h>
#include <boost/test/unit_test.hpp>

using namespace bcos::ledger;

BOOST_AUTO_TEST_SUITE(FeatureL2FlagTest)

BOOST_AUTO_TEST_CASE(StringRoundTrip)
{
    auto flag = Features::string2Flag("feature_l2_ethereum_compat");
    BOOST_CHECK(flag == Features::Flag::feature_l2_ethereum_compat);
    Features features;
    BOOST_CHECK(!features.get(flag));
    features.set(flag);
    BOOST_CHECK(features.get(flag));
}

BOOST_AUTO_TEST_SUITE_END()
