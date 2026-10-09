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
#include <bcos-ops/tui/views/Views.h>
#include <boost/test/unit_test.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

namespace bcos::ops::test
{
namespace
{
std::string render(ftxui::Element _element)
{
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80), ftxui::Dimension::Fixed(24));
    ftxui::Render(screen, _element);
    return screen.ToString();
}

void fillModel(FakeRpc& _rpc, tui::Model& model)
{
    Connection connection;
    connection.call = _rpc.call();
    connection.source = "rpc";
    connection.group = "group0";
    tui::Refresher refresher(
        connection, LocalFallbacks{15000, 3000}, Thresholds{}, model, nullptr, 100000);
    refresher.runOnce();
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OverviewRenderTest)

BOOST_AUTO_TEST_CASE(healthyOverviewShowsAllOk)
{
    FakeRpc rpc;
    tui::Model model;
    fillModel(rpc, model);
    auto text = render(tui::renderOverview(model));
    BOOST_CHECK(text.find("checks 5 ok") != std::string::npos);
    BOOST_CHECK(text.find("128") != std::string::npos);
    auto top = render(tui::renderTopBar(model));
    BOOST_CHECK(top.find("source rpc") != std::string::npos);
    BOOST_CHECK(top.find("3a1f00") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(timeoutOverviewShowsOneFail)
{
    FakeRpc rpc;
    rpc.setConsensus("timeout", true);
    tui::Model model;
    fillModel(rpc, model);
    auto text = render(tui::renderOverview(model));
    BOOST_CHECK(text.find("checks 1 fail") != std::string::npos);
    BOOST_CHECK(text.find("consensus_stable") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(logViewFiltersLines)
{
    tui::Model model;
    model.logTail.push_back(*parseLine(
        "info|2026-10-10 05:00:03.000000|x|[CONSENSUS][PBFT]ViewChangeTriggered,reason=restart"));
    model.logTail.push_back(
        *parseLine("info|2026-10-10 05:00:04.000000|x|[TXPOOL]TxsRemoved,number=2"));
    auto all = render(tui::renderLog(model, ""));
    BOOST_CHECK(all.find("ViewChangeTriggered") != std::string::npos);
    BOOST_CHECK(all.find("TxsRemoved") != std::string::npos);
    auto filtered = render(tui::renderLog(model, "ViewChange"));
    BOOST_CHECK(filtered.find("ViewChangeTriggered") != std::string::npos);
    BOOST_CHECK(filtered.find("TxsRemoved") == std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
