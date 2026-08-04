/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "bcos-utilities/IOServicePool.h"
#include <boost/test/unit_test.hpp>

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(IOServicePoolTest)

// start() spins up the worker threads, getIOService() hands them out
// round-robin (wrapping past the last one), and stop() joins them back.
BOOST_AUTO_TEST_CASE(startRoundRobinStop)
{
    bcos::IOServicePool pool(2);
    pool.start();
    auto first = pool.getIOService();
    auto second = pool.getIOService();
    auto wrapped = pool.getIOService();  // index wraps back to the first service
    BOOST_CHECK(first);
    BOOST_CHECK(second);
    BOOST_CHECK(wrapped);
    BOOST_CHECK(first != second);
    BOOST_CHECK(first == wrapped);
    pool.stop();
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
