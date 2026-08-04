/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "bcos-protocol/amop/TopicItem.h"
#include <boost/test/unit_test.hpp>

using namespace bcos::protocol;

BOOST_AUTO_TEST_SUITE(TopicItemTest)

BOOST_AUTO_TEST_CASE(constructAndAccess)
{
    TopicItem empty;
    BOOST_CHECK(empty.topicName().empty());

    TopicItem named("alpha");
    BOOST_CHECK_EQUAL(named.topicName(), "alpha");

    named.setTopicName("beta");
    BOOST_CHECK_EQUAL(named.topicName(), "beta");
}

BOOST_AUTO_TEST_CASE(orderingPlacesIntoSet)
{
    TopicItems items;
    items.insert(TopicItem("banana"));
    items.insert(TopicItem("apple"));
    items.insert(TopicItem("apple"));  // duplicate dropped by set semantics
    items.insert(TopicItem("cherry"));

    BOOST_REQUIRE_EQUAL(items.size(), 3U);
    auto it = items.begin();
    BOOST_CHECK_EQUAL((it++)->topicName(), "apple");
    BOOST_CHECK_EQUAL((it++)->topicName(), "banana");
    BOOST_CHECK_EQUAL((it++)->topicName(), "cherry");
}

BOOST_AUTO_TEST_CASE(parseSubTopicsJsonHappyPath)
{
    std::string json = R"({"topics":["a","b","c"]})";
    TopicItems items;
    bool ok = parseSubTopicsJson(json, items);
    BOOST_REQUIRE(ok);
    BOOST_REQUIRE_EQUAL(items.size(), 3U);
    BOOST_CHECK(items.find(TopicItem("a")) != items.end());
    BOOST_CHECK(items.find(TopicItem("b")) != items.end());
    BOOST_CHECK(items.find(TopicItem("c")) != items.end());
}

BOOST_AUTO_TEST_CASE(parseSubTopicsJsonEmptyTopicsArray)
{
    std::string json = R"({"topics":[]})";
    TopicItems items;
    bool ok = parseSubTopicsJson(json, items);
    BOOST_REQUIRE(ok);
    BOOST_CHECK(items.empty());
}

BOOST_AUTO_TEST_CASE(parseSubTopicsJsonMalformed)
{
    TopicItems items;
    BOOST_CHECK(!parseSubTopicsJson("not json at all", items));
}

BOOST_AUTO_TEST_CASE(parseSubTopicsJsonObjectInArrayReturnsFalse)
{
    // root["topics"][i] is an object — asString() on an object throws in
    // JsonCpp; the catch block must convert that into a false return.
    std::string json = R"({"topics":[{"foo":"bar"}]})";
    TopicItems items;
    BOOST_CHECK(!parseSubTopicsJson(json, items));
}

BOOST_AUTO_TEST_SUITE_END()
