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
BOOST_AUTO_TEST_SUITE(ConsensusRenderTest)

BOOST_AUTO_TEST_CASE(leaderRowIsMarked)
{
    FakeRpc rpc;
    tui::Model model;
    Connection connection;
    connection.call = rpc.call();
    connection.source = "rpc";
    connection.group = "group0";
    tui::Refresher refresher(connection, LocalFallbacks{}, Thresholds{}, model, nullptr, 100000);
    refresher.runOnce();
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80), ftxui::Dimension::Fixed(24));
    ftxui::Render(screen, tui::renderConsensus(model));
    auto text = screen.ToString();
    BOOST_CHECK(text.find("3a1f02") != std::string::npos);  // node list rendered
    // the leader row (index 2) carries the '*' marker
    auto leaderRow = text.find("3a1f02");
    auto lineEnd = text.find('\n', leaderRow);
    BOOST_CHECK(text.substr(leaderRow, lineEnd - leaderRow).find('*') != std::string::npos);
    auto otherRow = text.find("3a1f01");
    auto otherEnd = text.find('\n', otherRow);
    BOOST_CHECK(text.substr(otherRow, otherEnd - otherRow).find('*') == std::string::npos);
    BOOST_CHECK(text.find("TIMEOUT") == std::string::npos);
    auto syncText = [&]() {
        auto s = ftxui::Screen::Create(ftxui::Dimension::Fixed(80), ftxui::Dimension::Fixed(24));
        ftxui::Render(s, tui::renderSync(model));
        return s.ToString();
    }();
    BOOST_CHECK(syncText.find("idle") != std::string::npos);
    BOOST_CHECK(syncText.find("3a1f03") != std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
