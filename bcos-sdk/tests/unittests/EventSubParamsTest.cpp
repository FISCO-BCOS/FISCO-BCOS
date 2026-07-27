/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-cpp-sdk/event/EventSubParams.h>
#include <boost/test/unit_test.hpp>
#include <sstream>

using namespace bcos::cppsdk::event;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(EventSubParamsTest)

BOOST_AUTO_TEST_CASE(settersGettersAndTopics)
{
    EventSubParams params;
    params.setFromBlock(10);
    params.setToBlock(20);
    params.addAddress("0xabc");
    BOOST_CHECK_EQUAL(params.fromBlock(), 10);
    BOOST_CHECK_EQUAL(params.toBlock(), 20);
    BOOST_CHECK_EQUAL(params.addresses().size(), 1U);

    BOOST_CHECK(params.addTopic(0, "t0"));
    BOOST_CHECK(params.addTopic(1, "t1"));
    BOOST_CHECK_EQUAL(params.topics().size(), 2U);
    // addTopic at an index >= EVENT_LOG_TOPICS_MAX_INDEX is rejected.
    BOOST_CHECK(!params.addTopic(1000, "too-far"));
}

BOOST_AUTO_TEST_CASE(verifyParamsValidAndBlockRange)
{
    EventSubParams params;
    BOOST_CHECK(params.verifyParams());  // defaults (-1/-1) are valid

    params.setFromBlock(5);
    params.setToBlock(10);
    BOOST_CHECK(params.verifyParams());  // from <= to

    params.setFromBlock(20);
    params.setToBlock(10);
    BOOST_CHECK(!params.verifyParams());  // from > to → invalid
}

BOOST_AUTO_TEST_CASE(verifyParamsRejectsTooManyTopicGroups)
{
    EventSubParams params;
    // Force more topic groups than EVENT_LOG_TOPICS_MAX_INDEX via direct access.
    params.topics().resize(100);
    BOOST_CHECK(!params.verifyParams());
}

BOOST_AUTO_TEST_CASE(fromJsonStringValid)
{
    EventSubParams params;
    std::string json =
        R"({"fromBlock":1,"toBlock":10,"addresses":["0xabc"],"topics":["0xdeadbeef"]})";
    BOOST_CHECK(params.fromJsonString(json));
    BOOST_CHECK_EQUAL(params.fromBlock(), 1);
    BOOST_CHECK_EQUAL(params.toBlock(), 10);
}

BOOST_AUTO_TEST_CASE(fromJsonStringMalformed)
{
    EventSubParams params;
    BOOST_CHECK(!params.fromJsonString("not json"));
}

BOOST_AUTO_TEST_CASE(streamOperatorDumps)
{
    EventSubParams params;
    params.setFromBlock(3);
    params.addAddress("0xabc");
    std::stringstream ss;
    ss << params;
    BOOST_CHECK(!ss.str().empty());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
