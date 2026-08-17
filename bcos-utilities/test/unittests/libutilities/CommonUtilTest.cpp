/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-utilities/Common.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(CommonUtilTest)

BOOST_AUTO_TEST_CASE(timeAndDate)
{
    BOOST_CHECK_GT(utcTimeUs(), 0U);
    BOOST_CHECK_GT(utcSteadyTimeUs(), 0U);
    BOOST_CHECK(!getCurrentDateTime().empty());
}

BOOST_AUTO_TEST_CASE(intConversions)
{
    BOOST_CHECK_EQUAL(u2s(s2u(s256(-5))), s256(-5));
    BOOST_CHECK_EQUAL(s2u(u2s(u256(5))), u256(5));

    BOOST_CHECK(isHexStrWithPrefix("0x1234"));
    BOOST_CHECK(!isHexStrWithPrefix("1234"));

    BOOST_CHECK_EQUAL(hex2u("0x10"), u256(16));
    BOOST_CHECK_EQUAL(hex2u("10"), u256(16));
}

BOOST_AUTO_TEST_CASE(stringChecks)
{
    BOOST_CHECK(isalNumStr("abc123"));
    BOOST_CHECK(!isalNumStr("abc 123"));

    BOOST_CHECK(isNumStr("123"));
    BOOST_CHECK(!isNumStr("12a"));
    BOOST_CHECK(!isNumStr(""));
}

BOOST_AUTO_TEST_CASE(rateMath)
{
    BOOST_CHECK_GT(calcAvgRate(1024U * 1024U, 1000), 0);
    BOOST_CHECK_EQUAL(calcAvgRate(100, 0), 0);  // zero interval → 0
    BOOST_CHECK_GT(calcAvgQPS(1000, 1000), 0U);
    BOOST_CHECK_EQUAL(calcAvgQPS(100, 0), 0U);  // zero interval → 0
    BOOST_CHECK_EQUAL(toMillisecond(2), 2000);
}

BOOST_AUTO_TEST_CASE(threadName)
{
    BOOST_CHECK_NO_THROW(pthread_setThreadName("bcos-test"));
    BOOST_CHECK_NO_THROW(pthread_getThreadName());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
