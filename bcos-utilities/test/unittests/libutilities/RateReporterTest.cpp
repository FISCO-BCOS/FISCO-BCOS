/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-utilities/ratelimiter/RateReporter.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(RateReporterTest)

BOOST_AUTO_TEST_CASE(updateReportFlushCycle)
{
    RateReporter reporter("module-r", 1000);

    reporter.update(100, true);  // success branch
    reporter.update(200, true);
    reporter.update(50, false);  // failed branch
    BOOST_REQUIRE_NO_THROW(reporter.report());

    BOOST_REQUIRE_NO_THROW(reporter.flush());

    reporter.update(10, true);
    BOOST_REQUIRE_NO_THROW(reporter.report());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
