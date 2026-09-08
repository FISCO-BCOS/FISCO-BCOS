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
 * @file OpSchedulerPolicyTest.cpp
 * @brief PendingConflict classification tests
 */

#include <opstack-executor/OpSchedulerPolicy.h>

#include <boost/test/unit_test.hpp>

using bcos::executor_v1::opstack::classifyPendingConflict;
using bcos::executor_v1::opstack::PendingConflict;

BOOST_AUTO_TEST_SUITE(OpSchedulerPolicyTest)

BOOST_AUTO_TEST_CASE(NoPendingContinues)
{
    BOOST_CHECK(classifyPendingConflict(false, 0, 10, true) == PendingConflict::None);
    BOOST_CHECK(classifyPendingConflict(false, 0, 10, false) == PendingConflict::None);
}

BOOST_AUTO_TEST_CASE(OtherHeightIsRefused)
{
    BOOST_CHECK(classifyPendingConflict(true, 5, 6, true) == PendingConflict::RefuseOtherHeight);
    BOOST_CHECK(classifyPendingConflict(true, 5, 6, false) == PendingConflict::RefuseOtherHeight);
    BOOST_CHECK(classifyPendingConflict(true, 5, 4, true) == PendingConflict::RefuseOtherHeight);
}

BOOST_AUTO_TEST_CASE(SameHeightVerifyReplaces)
{
    BOOST_CHECK(classifyPendingConflict(true, 5, 5, true) == PendingConflict::ReplaceSameHeight);
}

BOOST_AUTO_TEST_CASE(SameHeightProbeKeepsPending)
{
    BOOST_CHECK(classifyPendingConflict(true, 5, 5, false) == PendingConflict::KeepProbe);
}

BOOST_AUTO_TEST_SUITE_END()
