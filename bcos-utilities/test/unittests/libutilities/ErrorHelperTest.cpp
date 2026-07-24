/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "bcos-utilities/Error.h"
#include <boost/test/unit_test.hpp>
#include <stdexcept>

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(ErrorHelperTest)

// buildError base + the two chaining overloads (previous Error / previous
// std::exception), the accessors, toString, and the empty-message path.
BOOST_AUTO_TEST_CASE(buildErrorAndAccessors)
{
    auto base = bcos::Error::buildError("ctx", 1, "base message");
    BOOST_CHECK_EQUAL(base.errorCode(), 1);
    BOOST_CHECK_EQUAL(base.errorMessage(), "base message");
    BOOST_CHECK(base.toString().find("base message") != std::string::npos);

    // Chained on a previous Error.
    auto withPrev = bcos::Error::buildError("ctx2", 2, "second", base);
    BOOST_CHECK_EQUAL(withPrev.errorCode(), 2);
    BOOST_CHECK_EQUAL(withPrev.errorMessage(), "second");

    // Chained on a previous std::exception.
    auto withStd = bcos::Error::buildError("ctx3", 3, "third", std::runtime_error("boom"));
    BOOST_CHECK_EQUAL(withStd.errorCode(), 3);

    // A default-constructed Error reports the empty-info fallbacks.
    bcos::Error empty;
    BOOST_CHECK_EQUAL(empty.errorCode(), 0);
    BOOST_CHECK(empty.errorMessage().empty());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
