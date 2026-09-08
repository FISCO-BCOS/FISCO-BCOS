/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-gateway/libamop/TopicManager.h>
#include <boost/test/unit_test.hpp>

using namespace bcos::amop;

namespace bcos::test
{
namespace
{
// subTopic() calls createAndGetServiceByClient, which in non-local mode would touch the tars
// network; local mode returns the (null) local client instead, so the in-memory topic logic is
// testable standalone.
TopicManager makeTestTopicManager()
{
    return TopicManager("rpc", nullptr, /*_localMode=*/true);
}

TopicItems items(std::initializer_list<std::string> names)
{
    TopicItems out;
    for (auto const& n : names)
    {
        out.insert(TopicItem(n));
    }
    return out;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(TopicManagerExtraTest)

BOOST_AUTO_TEST_CASE(subscribeAndQueryByClient)
{
    auto mgr = makeTestTopicManager();
    mgr.subTopic("clientA", items({"t1", "t2"}));

    TopicItems got;
    BOOST_REQUIRE(mgr.queryTopicItemsByClient("clientA", got));
    BOOST_CHECK_EQUAL(got.size(), 2U);

    TopicItems none;
    BOOST_CHECK(!mgr.queryTopicItemsByClient("unknown", none));
}

BOOST_AUTO_TEST_CASE(topicSeqIncrements)
{
    auto mgr = makeTestTopicManager();
    auto before = mgr.topicSeq();
    mgr.subTopic("clientA", items({"t1"}));  // non-empty → incTopicSeq
    BOOST_CHECK_GT(mgr.topicSeq(), before);
}

BOOST_AUTO_TEST_CASE(removeTopicsSubset)
{
    auto mgr = makeTestTopicManager();
    mgr.subTopic("clientA", items({"t1", "t2", "t3"}));
    mgr.removeTopics("clientA", {"t2"});

    TopicItems got;
    BOOST_REQUIRE(mgr.queryTopicItemsByClient("clientA", got));
    BOOST_CHECK_EQUAL(got.size(), 2U);
    BOOST_CHECK(got.find(TopicItem("t2")) == got.end());
}

BOOST_AUTO_TEST_CASE(removeTopicsByClientClearsAll)
{
    auto mgr = makeTestTopicManager();
    mgr.subTopic("clientA", items({"t1", "t2"}));
    mgr.removeTopicsByClient("clientA");

    TopicItems got;
    BOOST_CHECK(!mgr.queryTopicItemsByClient("clientA", got) || got.empty());
}

BOOST_AUTO_TEST_CASE(reverseLookupClientsByTopic)
{
    auto mgr = makeTestTopicManager();
    mgr.subTopic("clientA", items({"shared", "a-only"}));
    mgr.subTopic("clientB", items({"shared"}));

    std::vector<std::string> clients;
    mgr.queryClientsByTopic("shared", clients);
    BOOST_CHECK_EQUAL(clients.size(), 2U);

    std::vector<std::string> nodeIDs;
    BOOST_REQUIRE_NO_THROW(mgr.queryNodeIDsByTopic("shared", nodeIDs));
}

BOOST_AUTO_TEST_CASE(queryTopicsSubByClientJson)
{
    auto mgr = makeTestTopicManager();
    mgr.subTopic("clientA", items({"t1"}));
    auto json = mgr.queryTopicsSubByClient();
    BOOST_CHECK(!json.empty());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
