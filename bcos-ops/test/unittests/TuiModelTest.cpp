/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "fixtures/FakeRpc.h"
#include <bcos-ops/tui/Refresher.h>
#include <boost/test/unit_test.hpp>

namespace bcos::ops::test
{
BOOST_AUTO_TEST_SUITE(TuiModelTest)

BOOST_AUTO_TEST_CASE(onePassFillsTheModel)
{
    FakeRpc rpc;
    Connection connection;
    connection.call = rpc.call();
    connection.source = "rpc";
    connection.group = "group0";
    tui::Model model;
    int updates = 0;
    tui::Refresher refresher(
        connection, LocalFallbacks{15000, 3000}, Thresholds{}, model, [&updates]() { ++updates; },
        100000);
    refresher.runOnce();
    BOOST_CHECK_EQUAL(updates, 1);
    std::lock_guard<std::mutex> lock(model.mutex);
    BOOST_REQUIRE(model.status.blockNumber);
    BOOST_CHECK_EQUAL(*model.status.blockNumber, 128);
    BOOST_CHECK_EQUAL(model.checks.size(), 5U);
    BOOST_CHECK_EQUAL(model.source, "rpc");
    BOOST_CHECK(model.error.empty());
    BOOST_CHECK_EQUAL(model.consensus["leaderIndex"].asInt(), 2);  // string-wrapped result
                                                                   // unwrapped
    BOOST_CHECK_EQUAL(model.sync["peers"].size(), 3U);
}

BOOST_AUTO_TEST_CASE(failingConnectionSetsErrorAndClearsOnSuccess)
{
    tui::Model model;
    Connection broken;
    broken.call = [](std::string_view, Json::Value const&) -> Json::Value {
        throw OpsError(c_exitUsage, "connect refused");
    };
    broken.source = "rpc";
    broken.group = "group0";
    tui::Refresher refresher(broken, LocalFallbacks{}, Thresholds{}, model, nullptr, 100000);
    refresher.runOnce();
    {
        std::lock_guard<std::mutex> lock(model.mutex);
        // collectFromRpc never throws: every RPC failure lands in reasons; the model stays usable
        BOOST_CHECK(!model.status.blockNumber);
        BOOST_CHECK(model.status.reasons.contains("blockNumber"));
    }
    FakeRpc rpc;
    Connection good;
    good.call = rpc.call();
    good.source = "rpc";
    good.group = "group0";
    tui::Refresher second(good, LocalFallbacks{}, Thresholds{}, model, nullptr, 100000);
    second.runOnce();
    std::lock_guard<std::mutex> lock(model.mutex);
    BOOST_CHECK(model.error.empty());
    BOOST_CHECK_EQUAL(*model.status.blockNumber, 128);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
