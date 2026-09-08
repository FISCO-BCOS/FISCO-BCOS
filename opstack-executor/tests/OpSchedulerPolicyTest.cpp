// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

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
