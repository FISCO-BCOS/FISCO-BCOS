/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-utilities/RateCollector.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(RateCollectorTest)

BOOST_AUTO_TEST_CASE(factoryBuildsCollector)
{
    auto collector = RateCollectorFactory::build("module-x", 1000);
    BOOST_REQUIRE(collector);
}

BOOST_AUTO_TEST_CASE(updateReportFlushCycle)
{
    auto collector = RateCollectorFactory::build("module-y", 1000);
    BOOST_REQUIRE(collector);

    // enable so report() executes its body (rate calc) rather than early-return.
    RateCollector::enable();

    collector->update(100, true);  // success branch
    collector->update(200, true);
    collector->update(50, false);  // failed branch
    BOOST_REQUIRE_NO_THROW(collector->report());

    BOOST_REQUIRE_NO_THROW(collector->flush());  // resets the "last" counters

    // After flush, a fresh update + report still works.
    collector->update(10, true);
    BOOST_REQUIRE_NO_THROW(collector->report());

    // Disabled report() takes the early-return path.
    RateCollector::disable();
    BOOST_REQUIRE_NO_THROW(collector->report());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
