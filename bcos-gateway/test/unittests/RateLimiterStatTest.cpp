/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-gateway/libratelimit/RateLimiterStat.h>
#include <boost/test/unit_test.hpp>

using namespace bcos::gateway;
using namespace bcos::gateway::ratelimiter;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(RateLimiterStatTest)

BOOST_AUTO_TEST_CASE(statCountersUpdate)
{
    Stat stat{};
    stat.update(100);
    stat.update(50);
    BOOST_CHECK_EQUAL(stat.totalTimes, 2U);
    BOOST_CHECK_EQUAL(stat.lastTimes, 2);
    BOOST_CHECK_EQUAL(stat.totalDataSize, 150U);
    BOOST_CHECK_EQUAL(stat.lastDataSize, 150U);

    stat.updateFailed();
    BOOST_CHECK_EQUAL(stat.totalFailedTimes, 1U);
    BOOST_CHECK_EQUAL(stat.lastFailedTimes, 1);

    stat.resetLast();
    BOOST_CHECK_EQUAL(stat.lastTimes, 0);
    BOOST_CHECK_EQUAL(stat.lastDataSize, 0U);
    BOOST_CHECK_EQUAL(stat.lastFailedTimes, 0);
    // totals are unaffected by resetLast
    BOOST_CHECK_EQUAL(stat.totalTimes, 2U);
    BOOST_CHECK_EQUAL(stat.totalDataSize, 150U);
}

BOOST_AUTO_TEST_CASE(statToStringWithActivity)
{
    Stat stat{};
    stat.update(1000);
    auto str = stat.toString("incoming", 1000);
    // With non-zero activity toString yields a populated optional.
    BOOST_CHECK(str.has_value());
}

BOOST_AUTO_TEST_CASE(keyFormatters)
{
    BOOST_CHECK_EQUAL(RateLimiterStat::toGroupKey("group0"), "group0");
    BOOST_CHECK_EQUAL(RateLimiterStat::toEndpointKey("1.2.3.4:30300"), "1.2.3.4:30300");
    BOOST_CHECK_EQUAL(RateLimiterStat::toEndpointPkgTypeKey("1.2.3.4:30300", 5), "1.2.3.4:30300|5");
    // module-key formatters include the module name via moduleIDToString.
    BOOST_CHECK(!RateLimiterStat::toModuleKey(1).empty());
    BOOST_CHECK(RateLimiterStat::toModuleKey("group0", 1).rfind("group0|", 0) == 0);
}

BOOST_AUTO_TEST_CASE(updateAndFlush)
{
    RateLimiterStat stat;
    stat.setStatInterval(60000);
    BOOST_CHECK_EQUAL(stat.statInterval(), 60000);

    stat.updateInComing("group0", 1, 100, true);
    stat.updateInComing("group0", 1, 200, false);  // a failed one
    stat.updateOutGoing("group0", 1, 300, true);
    stat.updateInComing0("1.2.3.4:30300", 5, 100, true);
    stat.updateOutGoing("1.2.3.4:30300", 50, true);

    // inAndOutStat formats the accumulated counters; must not crash.
    BOOST_REQUIRE_NO_THROW(stat.inAndOutStat(60000));
    BOOST_REQUIRE_NO_THROW(stat.flushStat());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
