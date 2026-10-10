/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-utilities/BlockStat.h>
#include <boost/test/unit_test.hpp>
#include <thread>
#include <vector>

namespace bcos::test
{
namespace
{
struct DisableOnExit
{
    ~DisableOnExit() { BlockStat::disable(); }
};
}  // namespace

BOOST_FIXTURE_TEST_SUITE(BlockStatTest, DisableOnExit)

BOOST_AUTO_TEST_CASE(disabledDoesNotCount)
{
    BlockStat::disable();
    BlockStatCounters<2> counters;
    counters.add(0, 5);
    counters.add(1);
    BOOST_CHECK_EQUAL(counters.takeAndReset(0), 0U);
    BOOST_CHECK_EQUAL(counters.takeAndReset(1), 0U);
}

BOOST_AUTO_TEST_CASE(enabledAccumulatesAndResets)
{
    BlockStat::enable();
    BlockStatCounters<2> counters;
    counters.add(0, 3);
    counters.add(0, 4);
    BOOST_CHECK_EQUAL(counters.peek(0), 7U);
    BOOST_CHECK_EQUAL(counters.takeAndReset(0), 7U);
    BOOST_CHECK_EQUAL(counters.takeAndReset(0), 0U);
    BOOST_CHECK_EQUAL(counters.takeAndReset(1), 0U);
}

BOOST_AUTO_TEST_CASE(concurrentAddsSumExactly)
{
    BlockStat::enable();
    BlockStatCounters<1> counters;
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t)
    {
        threads.emplace_back([&counters]() {
            for (int i = 0; i < 1000; ++i)
            {
                counters.add(0);
            }
        });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }
    BOOST_CHECK_EQUAL(counters.takeAndReset(0), 8000U);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
