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
 * @file NavTest.cpp
 * @brief the gov page and the proposal detail render the committee, the proposal rows, the
 *        vote hints and the notice; non-auth chains get a note; refs resolve to proposal ids
 * @file GovRenderTest.cpp
 */
#include "bcos-ops/tui/Model.h"
#include "bcos-ops/tui/Nav.h"
#include "bcos-ops/tui/SendForm.h"
#include "bcos-ops/tui/views/Pages.h"
#include "fixtures/Render.h"
#include <boost/test/unit_test.hpp>

namespace bcos::ops::test
{
namespace
{
tui::ProposalRow govRow(int64_t _id, int _status, int _type)
{
    tui::ProposalRow row;
    row.id = _id;
    row.type = _type;
    row.typeName = tui::proposalTypeName(_type);
    row.status = _status;
    row.statusName = _status == 1 ? "voting" : _status == 2 ? "passed" : "denied";
    row.proposer = "0x3f12000000000000000000000000000000009a00";
    row.resource = "0x0000000000000000000000000000000000001000";
    row.expiresAtBlock = "1128";
    row.agree = 1;
    row.against = _status == 3 ? 2 : 0;
    row.executed = _status == 2;
    if (_status == 2)
    {
        row.execCode = 0;
        row.execCodeName = "SUCCESS";
    }
    return row;
}

tui::Model& govModel(tui::Model& _model)
{
    _model.authChain = true;
    _model.governance.loaded = true;
    _model.governance.committee = "0xc0ffee0000000000000000000000000000000001";
    _model.governance.participatesRate = "100";
    _model.governance.winRate = "50";
    _model.governance.governors = {{"0x3f12000000000000000000000000000000009a00", "1"},
        {"0x0cc5000000000000000000000000000000007890", "2"}};
    _model.governance.proposalCount = 3;
    _model.governance.proposals = {govRow(3, 1, 41), govRow(2, 2, 51), govRow(1, 3, 11)};
    return _model;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(GovRenderTest)

BOOST_AUTO_TEST_CASE(pageShowsCommitteeAndProposalRows)
{
    tui::Model model;
    govModel(model);
    tui::Nav nav;
    nav.page = tui::Page::gov;
    nav.authChain = true;
    tui::Frame frame{.width = 80, .height = 24, .nowMs = 0};
    auto text = renderText(tui::renderGov(model, nav, frame), 80, 24);
    BOOST_CHECK(text.find("participatesRate") != std::string::npos);
    BOOST_CHECK(text.find("100%") != std::string::npos);
    BOOST_CHECK(text.find("winRate") != std::string::npos);
    BOOST_CHECK(text.find("50%") != std::string::npos);
    BOOST_CHECK(
        text.find("0x3f12000000000000000000000000000000009a00  weight 1") != std::string::npos);
    BOOST_CHECK(text.find("proposals 3") != std::string::npos);
    auto row = lineWith(text, "sysConfig");
    BOOST_CHECK(row.find("3") != std::string::npos);
    BOOST_CHECK(row.find("voting") != std::string::npos);
    BOOST_CHECK(lineWith(text, "consensusWeight").find("passed") != std::string::npos);
    BOOST_CHECK(lineWith(text, "denied").find("governor") != std::string::npos);
    BOOST_CHECK_EQUAL(nav.proposals.count, 3U);
}

BOOST_AUTO_TEST_CASE(nonAuthChainsGetANote)
{
    tui::Model model;
    tui::Nav nav;
    nav.page = tui::Page::gov;
    tui::Frame frame{.width = 80, .height = 24, .nowMs = 0};
    auto text = renderText(tui::renderGov(model, nav, frame), 80, 24);
    BOOST_CHECK(text.find("not an auth chain") != std::string::npos);
    BOOST_CHECK_EQUAL(nav.proposals.count, 0U);
    // the whole frame with a proposal ref on a non-auth chain renders the same note
    nav.stack.push_back({.kind = tui::DetailKind::proposal, .number = 1, .resolved = true});
    auto detail = renderText(tui::renderFrame(model, nav, frame, ""), 80, 24);
    BOOST_CHECK(detail.find("not an auth chain") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(detailShowsVotesHintsAndNotice)
{
    tui::Model model;
    govModel(model);
    model.governance.details[1] = govRow(1, 3, 11);
    tui::DetailRef ref{.kind = tui::DetailKind::proposal, .number = 1, .resolved = true};
    auto form = tui::makeSendForm(false);
    form.notice = "load a key first";
    model.send.last.seq = 1;
    model.send.last.ok = false;
    model.send.last.line = "error: boom \x1b[2J";
    tui::Frame frame{.width = 80, .height = 24, .nowMs = 0};
    auto raw = renderRaw(tui::renderProposalDetail(model, frame, ref, &form), 80, 24);
    auto text = stripAnsi(raw);
    BOOST_CHECK(text.find("proposal 1") != std::string::npos);
    BOOST_CHECK(text.find("governor (11)") != std::string::npos);
    BOOST_CHECK(text.find("denied") != std::string::npos);
    BOOST_CHECK(lineWith(text, "agree").find("1") != std::string::npos);
    BOOST_CHECK(lineWith(text, "against").find("2") != std::string::npos);
    BOOST_CHECK(text.find("expiresAtBlock") != std::string::npos);
    BOOST_CHECK(text.find("1128") != std::string::npos);
    BOOST_CHECK(text.find("v vote for") != std::string::npos);
    BOOST_CHECK(text.find("d revoke") != std::string::npos);
    BOOST_CHECK(text.find("load a key first") != std::string::npos);
    BOOST_CHECK(text.find("boom") != std::string::npos);
    BOOST_CHECK(raw.find("\x1b[2J") == std::string::npos);  // control bytes never reach the
                                                            // terminal
    // an id not loaded yet
    tui::DetailRef pending{.kind = tui::DetailKind::proposal, .number = 9, .resolved = true};
    auto loading = renderText(tui::renderProposalDetail(model, frame, pending, nullptr), 80, 24);
    BOOST_CHECK(loading.find("loading proposal") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(refsResolveToProposalIdsAndWantsListThem)
{
    tui::Model model;
    govModel(model);
    tui::Nav nav;
    nav.authChain = true;
    nav.page = tui::Page::gov;
    nav.proposals.count = 3;
    nav.proposals.pageRows = 7;
    nav.proposals.selected = 1;  // the second row: proposal 2
    BOOST_CHECK(tui::navHandle(nav, ftxui::Event::Return) == tui::NavAction::redraw);
    BOOST_REQUIRE(nav.inDetail());
    BOOST_CHECK(!nav.stack.back().resolved);
    tui::resolveProposalRefs(nav, model.governance);
    BOOST_CHECK(nav.stack.back().resolved);
    BOOST_CHECK_EQUAL(nav.stack.back().number, 2);
    auto wants = tui::governanceWants(nav);
    BOOST_CHECK_EQUAL(wants.rows, 7U);
    BOOST_REQUIRE_EQUAL(wants.ids.size(), 1U);
    BOOST_CHECK_EQUAL(wants.ids[0], 2);
    // a newer proposal arriving above does not move the open one
    model.governance.proposals.insert(model.governance.proposals.begin(), govRow(4, 1, 41));
    tui::resolveProposalRefs(nav, model.governance);
    BOOST_CHECK_EQUAL(nav.stack.back().number, 2);
    // the frame renders the detail and keeps the proposal page's bottom hint
    tui::Frame frame{.width = 120, .height = 40, .nowMs = 0};
    auto text = renderText(tui::renderFrame(model, nav, frame, ""), 120, 40);
    BOOST_CHECK(text.find("proposal 2") != std::string::npos);
    BOOST_CHECK(text.find("v for") != std::string::npos);
    BOOST_CHECK(tui::navHandle(nav, ftxui::Event::Escape) == tui::NavAction::redraw);
    BOOST_CHECK(!nav.inDetail());
    BOOST_CHECK_EQUAL(nav.proposals.selected, 1U);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
