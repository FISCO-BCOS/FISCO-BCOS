/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-rpc/event/EventSubResponse.h>
#include <boost/test/unit_test.hpp>

using namespace bcos::event;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(EventSubResponseTest)

BOOST_AUTO_TEST_CASE(settersAndGenerateJson)
{
    EventSubResponse resp;
    resp.setId("resp-1");
    resp.setStatus(0);
    BOOST_CHECK_EQUAL(resp.status(), 0);

    auto json = resp.generateJson();
    BOOST_CHECK(!json.empty());
}

BOOST_AUTO_TEST_CASE(roundTripFromJson)
{
    EventSubResponse out;
    out.setId("resp-2");
    out.setStatus(5);
    auto json = out.generateJson();

    EventSubResponse parsed;
    BOOST_REQUIRE(parsed.fromJson(json));
    BOOST_CHECK_EQUAL(parsed.status(), 5);
}

BOOST_AUTO_TEST_CASE(fromJsonMalformed)
{
    EventSubResponse resp;
    BOOST_CHECK(!resp.fromJson("not json"));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
