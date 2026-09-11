/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file FeaturesGenesisOnlyTest.cpp
 * @brief feature_l2_ethereum_compat is genesis-only: Features::validate rejects it, so the
 *        governance setSystemConfig path cannot turn L2 mode on for a running chain, while
 *        genesis loading -- which calls set() directly -- is untouched.
 */
#include "bcos-framework/ledger/Features.h"
#include "bcos-tool/Exceptions.h"
#include <bcos-utilities/Exceptions.h>
#include <boost/exception/get_error_info.hpp>
#include <boost/test/unit_test.hpp>
#include <string>

using namespace bcos::ledger;

BOOST_AUTO_TEST_SUITE(FeaturesGenesisOnlyTest)

/// SystemConfigPrecompiled::validate reaches Features::validate through the string overload
/// (it holds the raw SYS_CONFIG key), so both overloads must refuse.
BOOST_AUTO_TEST_CASE(governanceCannotEnableL2Mode)
{
    Features features;
    BOOST_CHECK_THROW(features.validate(Features::Flag::feature_l2_ethereum_compat),
        bcos::tool::InvalidSetFeature);
    BOOST_CHECK_THROW(
        features.validate("feature_l2_ethereum_compat"), bcos::tool::InvalidSetFeature);

    // The refusal is unconditional: already being on does not make it settable again.
    Features alreadyL2;
    alreadyL2.set(Features::Flag::feature_l2_ethereum_compat);
    BOOST_CHECK_THROW(alreadyL2.validate(Features::Flag::feature_l2_ethereum_compat),
        bcos::tool::InvalidSetFeature);
}

/// BOOST_CHECK_THROW above pins only the type; a wrong-field throw of the same type would pass.
/// The message has to tell an operator where the flag does belong, since SystemConfigPrecompiled
/// surfaces the errinfo_comment as the failing transaction's reason.
BOOST_AUTO_TEST_CASE(refusalNamesTheGenesisSection)
{
    auto namesGenesis = [](bcos::tool::InvalidSetFeature const& e) {
        auto const* msg = boost::get_error_info<bcos::errinfo_comment>(e);
        return msg != nullptr && msg->find("genesis-only") != std::string::npos &&
               msg->find("config.genesis") != std::string::npos;
    };
    Features features;
    BOOST_CHECK_EXCEPTION(features.validate(Features::Flag::feature_l2_ethereum_compat),
        bcos::tool::InvalidSetFeature, namesGenesis);
}

/// Genesis loading does not go through validate(): set() still works, and the flag reads back.
BOOST_AUTO_TEST_CASE(genesisSetIsUnaffected)
{
    Features features;
    BOOST_CHECK(!features.get(Features::Flag::feature_l2_ethereum_compat));
    BOOST_CHECK_NO_THROW(features.set(Features::Flag::feature_l2_ethereum_compat));
    BOOST_CHECK(features.get(Features::Flag::feature_l2_ethereum_compat));

    Features byName;
    BOOST_CHECK_NO_THROW(byName.set("feature_l2_ethereum_compat"));
    BOOST_CHECK(byName.get(Features::Flag::feature_l2_ethereum_compat));
}

/// Negative control: the rule is about this one flag, not about validate() rejecting whatever
/// it is handed. A neighbouring L2-adjacent flag with no dependency still validates.
BOOST_AUTO_TEST_CASE(otherFeaturesStillValidate)
{
    Features features;
    BOOST_CHECK_NO_THROW(features.validate(Features::Flag::feature_op_jovian));
    BOOST_CHECK_NO_THROW(features.validate(Features::Flag::feature_balance));
}

BOOST_AUTO_TEST_SUITE_END()
