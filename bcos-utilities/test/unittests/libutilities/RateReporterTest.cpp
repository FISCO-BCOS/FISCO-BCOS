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
#include <boost/asio/io_context.hpp>
#include <boost/test/unit_test.hpp>

using namespace bcos;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(RateReporterTest)

BOOST_AUTO_TEST_CASE(updateReportFlushCycle)
{
    // RateReporter borrows an external io_context now (it used to create its own Timer thread).
    // Declared first so it outlives the reporter; never run, so the report timer never fires and
    // the update/report/flush calls below stay deterministic.
    boost::asio::io_context ioContext;
    RateReporter reporter(ioContext, "module-r", 1000);

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
