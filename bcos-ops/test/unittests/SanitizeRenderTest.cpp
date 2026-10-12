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
 * @brief node-provided text reaches the terminal without control bytes: an ESC inside a peer
 *        id, an address, a sealer or a proposal field renders as visible text, never as an
 *        escape sequence (the list cells, detail fields, gov fields and node tables all pass
 *        through sanitizeForTerminal)
 * @file SanitizeRenderTest.cpp
 */
#include "bcos-ops/tui/Model.h"
#include "bcos-ops/tui/Nav.h"
#include "bcos-ops/tui/views/Pages.h"
#include "fixtures/Render.h"
#include <boost/test/unit_test.hpp>

namespace bcos::ops::test
{
namespace
{
std::string const c_clearScreen = "\x1b[2J";

/// the injected sequence must not survive; its printable tail stays visible
void checkSanitized(std::string const& _raw, std::string const& _what)
{
    BOOST_TEST_INFO(_what);
    BOOST_CHECK(_raw.find(c_clearScreen) == std::string::npos);
    BOOST_CHECK(stripAnsi(_raw).find("[2J") != std::string::npos);
}
}  // namespace

BOOST_AUTO_TEST_SUITE(SanitizeRenderTest)

BOOST_AUTO_TEST_CASE(homeListCellsDropEscapes)
{
    tui::Model model;
    tui::BlockRow block;
    block.number = 5;
    block.hash = "0x05";
    block.txCount = 1;
    model.explorer.blocks.push_back(block);
    tui::TxRow tx;
    tx.hash = "0xab" + c_clearScreen;
    tx.from = c_clearScreen + "0x3f12";
    tx.to = "0x43c1";
    tx.blockNumber = 5;
    model.explorer.txs.push_back(tx);
    tui::Nav nav;
    tui::Frame frame{.width = 120, .height = 30, .nowMs = 0};
    checkSanitized(renderRaw(tui::renderHome(model, nav, frame), 120, 30), "home tx row");
}

BOOST_AUTO_TEST_CASE(blockDetailFieldsDropEscapes)
{
    tui::Model model;
    Json::Value block;
    block["number"] = 7;
    block["hash"] = "0x07";
    block["sealer"] = c_clearScreen;
    block["transactions"] = Json::Value(Json::arrayValue);
    model.explorer.blockDetails[7] = block;
    tui::Nav nav;
    nav.stack.push_back({.kind = tui::DetailKind::block, .number = 7, .resolved = true});
    tui::Frame frame{.width = 80, .height = 24, .nowMs = 0};
    checkSanitized(renderRaw(tui::renderDetail(model, nav, frame), 80, 24), "block detail");
}

BOOST_AUTO_TEST_CASE(govListAndProposalDetailDropEscapes)
{
    tui::Model model;
    model.authChain = true;
    model.governance.loaded = true;
    model.governance.governors = {{c_clearScreen + "0x3f12", "1"}};
    tui::ProposalRow row;
    row.id = 3;
    row.status = 1;
    row.statusName = "voting";
    row.typeName = "sysConfig";
    row.proposer = c_clearScreen + "0x3f12";
    row.resource = "0x1000" + c_clearScreen;
    model.governance.proposals = {row};
    model.governance.proposalCount = 1;
    model.governance.details[3] = row;
    tui::Nav nav;
    nav.page = tui::Page::gov;
    nav.authChain = true;
    tui::Frame frame{.width = 120, .height = 30, .nowMs = 0};
    checkSanitized(renderRaw(tui::renderGov(model, nav, frame), 120, 30), "gov list");
    tui::DetailRef ref{.kind = tui::DetailKind::proposal, .number = 3, .resolved = true};
    checkSanitized(
        renderRaw(tui::renderProposalDetail(model, frame, ref, nullptr), 120, 30), "proposal");
}

BOOST_AUTO_TEST_CASE(nodeTablesDropEscapes)
{
    tui::Model model;
    model.status.version = "3.18.0" + c_clearScreen;
    model.sync["blockNumber"] = 9;
    Json::Value peer;
    peer["nodeID"] = c_clearScreen + "3a1f01";
    peer["blockNumber"] = 9;
    peer["latestHash"] = "0x09";
    model.sync["peers"].append(peer);
    checkSanitized(renderRaw(tui::renderNode(model), 120, 30), "node page");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
