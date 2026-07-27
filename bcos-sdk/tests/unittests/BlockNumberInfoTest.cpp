/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-cpp-sdk/ws/BlockNumberInfo.h>
#include <boost/test/unit_test.hpp>

using namespace bcos::cppsdk::service;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(BlockNumberInfoTest)

BOOST_AUTO_TEST_CASE(settersGetters)
{
    BlockNumberInfo info;
    info.setGroup("group0");
    info.setNode("node0");
    info.setBlockNumber(42);
    BOOST_CHECK_EQUAL(info.group(), "group0");
    BOOST_CHECK_EQUAL(info.blockNumber(), 42);
}

BOOST_AUTO_TEST_CASE(fromJsonValid)
{
    BlockNumberInfo info;
    BOOST_REQUIRE(info.fromJson(R"({"group":"g","blockNumber":99,"nodeName":"n"})"));
    BOOST_CHECK_EQUAL(info.group(), "g");
    BOOST_CHECK_EQUAL(info.blockNumber(), 99);
}

BOOST_AUTO_TEST_CASE(fromJsonMissingFields)
{
    BlockNumberInfo a;
    BOOST_CHECK(!a.fromJson(R"({"group":"g","nodeName":"n"})"));  // no blockNumber
    BlockNumberInfo b;
    BOOST_CHECK(!b.fromJson(R"({"blockNumber":1,"nodeName":"n"})"));  // no group
}

BOOST_AUTO_TEST_CASE(fromJsonMalformed)
{
    BlockNumberInfo info;
    BOOST_CHECK(!info.fromJson("not json"));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
