/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file NodeRenderTest.cpp
 */
#include "bcos-ops/tui/Model.h"
#include "bcos-ops/tui/Refresher.h"
#include "bcos-ops/tui/views/Pages.h"
#include "fixtures/FakeRpc.h"
#include "fixtures/Render.h"
#include <boost/test/unit_test.hpp>
#include <ftxui/screen/screen.hpp>

namespace bcos::ops::test
{
namespace
{
void nodeFill(FakeRpc& _rpc, tui::Model& _model)
{
    Connection connection;
    connection.call = _rpc.call();
    connection.source = "rpc";
    connection.group = "group0";
    tui::Refresher refresher(
        connection, LocalFallbacks{15000, 3000}, Thresholds{}, _model, nullptr, 100000);
    refresher.runOnce();
}
}  // namespace

BOOST_AUTO_TEST_SUITE(NodeRenderTest)

BOOST_AUTO_TEST_CASE(nodePageShowsIdentitySyncAndChecks)
{
    FakeRpc rpc;
    tui::Model model;
    nodeFill(rpc, model);
    auto text = renderText(tui::renderNode(model));
    BOOST_CHECK(text.find("3a1f00") != std::string::npos);
    BOOST_CHECK(text.find("sealer") != std::string::npos);
    BOOST_CHECK(text.find("3.18.0") != std::string::npos);
    BOOST_CHECK(text.find("chain0") != std::string::npos);
    BOOST_CHECK(text.find("idle") != std::string::npos);
    BOOST_CHECK(text.find("3a1f03") != std::string::npos);  // a peer row
    BOOST_CHECK(text.find("local 128") != std::string::npos);
    BOOST_CHECK(text.find("height_advancing") != std::string::npos);
    BOOST_CHECK(text.find("consensus_stable") != std::string::npos);
    BOOST_CHECK(text.find("FAIL") == std::string::npos);
    rpc.setConsensus("timeout", true);
    nodeFill(rpc, model);
    auto failing = renderText(tui::renderNode(model));
    BOOST_CHECK(failing.find("FAIL") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(consensusPageMarksTheLeader)
{
    FakeRpc rpc;
    tui::Model model;
    nodeFill(rpc, model);
    auto text = renderText(tui::renderConsensus(model));
    BOOST_CHECK(text.find("3a1f02") != std::string::npos);
    auto leaderRow = text.find("3a1f02");
    auto lineEnd = text.find('\n', leaderRow);
    BOOST_CHECK(text.substr(leaderRow, lineEnd - leaderRow).find('*') != std::string::npos);
    auto otherRow = text.find("3a1f01");
    auto otherEnd = text.find('\n', otherRow);
    BOOST_CHECK(text.substr(otherRow, otherEnd - otherRow).find('*') == std::string::npos);
    BOOST_CHECK(text.find("TIMEOUT") == std::string::npos);
    BOOST_CHECK(text.find("view 0") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(helpersFormatAgesClocksAndStatus)
{
    BOOST_CHECK_EQUAL(tui::ageText(2000), "2s");
    BOOST_CHECK_EQUAL(tui::ageText(125000), "2m");
    BOOST_CHECK_EQUAL(tui::ageText(7200000), "2h");
    BOOST_CHECK_EQUAL(tui::ageText(-1), "-");
    BOOST_CHECK_EQUAL(tui::clockText(0), "--:--");
    BOOST_CHECK_EQUAL(tui::clockText(1760000000000).size(), 5U);
    BOOST_CHECK_EQUAL(tui::txStatusWord(-1), "*");
    BOOST_CHECK_EQUAL(tui::txStatusWord(0), "ok");
    BOOST_CHECK_EQUAL(tui::txStatusWord(16), "FAIL");
    BOOST_CHECK_EQUAL(tui::shortNodeId("0x3a1f00112233"), "3a1f0011");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
