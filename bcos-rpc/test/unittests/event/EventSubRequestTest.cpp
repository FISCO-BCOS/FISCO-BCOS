/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-rpc/event/EventSubRequest.h>
#include <boost/test/unit_test.hpp>

using namespace bcos::event;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(EventSubRequestTest)

BOOST_AUTO_TEST_CASE(unsubRequestRoundTrip)
{
    EventSubUnsubRequest req;
    req.setId("req-1");
    req.setGroup("group0");

    std::string json = req.generateJson();
    EventSubUnsubRequest parsed;
    BOOST_REQUIRE(parsed.fromJson(json));
    BOOST_CHECK_EQUAL(parsed.id(), "req-1");
    BOOST_CHECK_EQUAL(parsed.group(), "group0");
}

BOOST_AUTO_TEST_CASE(unsubFromJsonMissingIdFails)
{
    EventSubUnsubRequest req;
    BOOST_CHECK(!req.fromJson(R"({"group":"group0"})"));
}

BOOST_AUTO_TEST_CASE(unsubFromJsonMissingGroupFails)
{
    EventSubUnsubRequest req;
    BOOST_CHECK(!req.fromJson(R"({"id":"x"})"));
}

BOOST_AUTO_TEST_CASE(unsubFromJsonMalformedFails)
{
    EventSubUnsubRequest req;
    BOOST_CHECK(!req.fromJson("}{ not json"));
}

BOOST_AUTO_TEST_CASE(subFromJsonFullParams)
{
    EventSubRequest req;
    std::string json = R"({
        "id":"sub-1",
        "group":"group0",
        "params":{
            "fromBlock":10,
            "toBlock":20,
            "addresses":["0xca5ed56862869c25da0bdf186e634aac6c6361ee"],
            "topics":["0x91c95f04198617c60eaf2180fbca88fc192db379657df0e412a9f7dd4ebbe95d"]
        }
    })";
    BOOST_REQUIRE(req.fromJson(json));
    BOOST_CHECK_EQUAL(req.id(), "sub-1");
    BOOST_CHECK_EQUAL(req.group(), "group0");
    BOOST_REQUIRE(req.params());
    BOOST_CHECK_EQUAL(req.params()->fromBlock(), 10);
    BOOST_CHECK_EQUAL(req.params()->toBlock(), 20);
    BOOST_CHECK_EQUAL(req.params()->addresses().size(), 1U);
}

BOOST_AUTO_TEST_CASE(subFromJsonMissingParamsFails)
{
    EventSubRequest req;
    BOOST_CHECK(!req.fromJson(R"({"id":"x","group":"g"})"));
}

BOOST_AUTO_TEST_CASE(subFromJsonDefaultsWhenParamFieldsAbsent)
{
    EventSubRequest req;
    // params present but empty — fromBlock/toBlock keep their -1 defaults.
    BOOST_REQUIRE(req.fromJson(R"({"id":"x","group":"g","params":{}})"));
    BOOST_REQUIRE(req.params());
    BOOST_CHECK_EQUAL(req.params()->fromBlock(), -1);
    BOOST_CHECK_EQUAL(req.params()->toBlock(), -1);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
