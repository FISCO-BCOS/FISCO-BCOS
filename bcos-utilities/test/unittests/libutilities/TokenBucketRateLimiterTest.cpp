/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "bcos-utilities/ratelimiter/TokenBucketRateLimiter.h"
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::ratelimiter;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(TokenBucketRateLimiterTest)

// A request larger than the configured QPS can never be granted.
BOOST_AUTO_TEST_CASE(overLimitNeverGranted)
{
    TokenBucketRateLimiter limiter(100);
    BOOST_CHECK(!limiter.tryAcquire(101));
    BOOST_CHECK(!limiter.acquire(101));
}

// The three tuning setters and the stored-permit fast path: after rolling
// permits back (capped at the max permits size), a small request is served
// from the stored bucket without waiting.
BOOST_AUTO_TEST_CASE(settersRollbackAndStoredFastPath)
{
    TokenBucketRateLimiter limiter(100);
    limiter.setMaxPermitsSize(200);
    limiter.setBurstTimeInterval(1000);
    limiter.setMaxBurstReqNum(50);

    // rollback adds permits but never exceeds the max permits size.
    limiter.rollback(1000);

    // Stored permits now satisfy small requests via the fast path.
    BOOST_CHECK(limiter.tryAcquire(1));
    BOOST_CHECK(limiter.acquire(1));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
