/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-framework/protocol/LogEntry.h>
#include <bcos-rpc/event/EventSubMatcher.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::event;

namespace bcos::test
{
namespace
{
// matches(params, logEntry) is protected in EventSubMatcher; expose it.
class TestMatcher : public EventSubMatcher
{
public:
    using EventSubMatcher::matches;
};

protocol::LogEntry makeLog(const std::string& address, const std::vector<h256>& topics)
{
    return protocol::LogEntry(bcos::bytes(address.begin(), address.end()),
        bcos::h256s(topics.begin(), topics.end()), bcos::bytes{});
}
}  // namespace

BOOST_AUTO_TEST_SUITE(EventSubMatcherTest)

BOOST_AUTO_TEST_CASE(emptyParamsMatchAnything)
{
    TestMatcher matcher;
    auto params = std::make_shared<EventSubParams>();
    auto log = makeLog("addr1", {h256(1)});
    BOOST_CHECK(matcher.matches(params, log));
}

BOOST_AUTO_TEST_CASE(addressFilter)
{
    TestMatcher matcher;
    auto params = std::make_shared<EventSubParams>();
    params->addAddress("addr1");

    BOOST_CHECK(matcher.matches(params, makeLog("addr1", {})));   // address in set
    BOOST_CHECK(!matcher.matches(params, makeLog("addr2", {})));  // address not in set
}

BOOST_AUTO_TEST_CASE(topicFilter)
{
    TestMatcher matcher;
    auto params = std::make_shared<EventSubParams>();
    h256 wanted(0x42);
    params->addTopic(0, wanted.hex());

    // log with matching topic[0]
    BOOST_CHECK(matcher.matches(params, makeLog("addr", {wanted})));
    // log with a different topic[0]
    BOOST_CHECK(!matcher.matches(params, makeLog("addr", {h256(0x99)})));
    // log with no topics → cannot satisfy a topic[0] filter
    BOOST_CHECK(!matcher.matches(params, makeLog("addr", {})));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
