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
 * @brief the send page renders: no key → the button says load a key first, with a key the
 *        add-sealer form shows its parameter names and types, the confirmation layer prints
 *        to/method/params/mode, the features entry lists flags on/off, EL mode keeps only raw
 * @file SendViewTest.cpp
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
tui::SendForm sendViewAddSealer()
{
    auto form = tui::makeSendForm(false);
    form.expanded = {"consensus"};
    form.rebuild();
    for (size_t i = 0; i < form.entries.size(); ++i)
    {
        if (form.entries[i].preset && form.entries[i].preset->name == "add-sealer")
        {
            form.tree.selected = i;
        }
    }
    return form;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(SendViewTest)

BOOST_AUTO_TEST_CASE(withoutAKeyTheButtonIsDisabled)
{
    tui::Model model;
    auto form = sendViewAddSealer();
    tui::Frame frame{.width = 120, .height = 40, .nowMs = 0};
    auto text = renderText(tui::renderSend(model, form, frame), 120, 40);
    BOOST_CHECK(text.find("load a key first") != std::string::npos);
    BOOST_CHECK(text.find("key:      none") != std::string::npos);
    BOOST_CHECK(text.find("nodeId (string)") != std::string::npos);
    BOOST_CHECK(text.find("weight (uint256)") != std::string::npos);
    BOOST_CHECK(text.find("consensus: add-sealer") != std::string::npos);
    BOOST_CHECK(text.find("no transaction sent yet") != std::string::npos);
    // the tree shows the open group and its siblings
    BOOST_CHECK(text.find("- consensus") != std::string::npos);
    BOOST_CHECK(text.find("+ sysconfig") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(withAKeyTheFormSendsAndTheOutcomeLineShows)
{
    tui::Model model;
    model.keyAddress = "0x3f12000000000000000000000000000000009a00";
    model.send.last = tui::SendOutcome{.seq = 1,
        .ok = true,
        .line = "tx 0x69de8f…52 -> block 129 ok",
        .txHash = "0x69de",
        .proposalId = "",
        .proposalStatus = ""};
    auto form = sendViewAddSealer();
    form.focus = form.buttonFocus();
    tui::Frame frame{.width = 120, .height = 40, .nowMs = 0};
    auto text = renderText(tui::renderSend(model, form, frame), 120, 40);
    BOOST_CHECK(text.find("load a key first") == std::string::npos);
    BOOST_CHECK(text.find("[ send ]") != std::string::npos);
    BOOST_CHECK(text.find("key:      0x3f12") != std::string::npos);
    BOOST_CHECK(text.find(" ok tx 0x69de8f") != std::string::npos);
    model.send.busy = true;
    text = renderText(tui::renderSend(model, form, frame), 120, 40);
    BOOST_CHECK(text.find("sending…") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(confirmationLayerListsWhatIsSent)
{
    tui::Model model;
    model.keyAddress = "0x3f12";
    auto form = sendViewAddSealer();
    form.fields[0] = "3a1f00";
    form.fields[1] = "1";
    tui::SendContext context;
    context.hasKey = true;
    form.pending = tui::sendFormRequest(form, context);
    form.confirmRows = tui::sendConfirmRows(form.pending, context);
    form.confirm = true;
    tui::Nav nav;
    nav.page = tui::Page::send;
    tui::Frame frame{.width = 120, .height = 40, .nowMs = 0};
    auto text = renderText(tui::renderFrame(model, nav, frame, "", &form), 120, 40);
    BOOST_CHECK(text.find(" confirm ") != std::string::npos);
    BOOST_CHECK(text.find("mode   direct") != std::string::npos);
    BOOST_CHECK(
        text.find("to     0x0000000000000000000000000000000000001003") != std::string::npos);
    BOOST_CHECK(text.find("method addSealer(string,uint256)") != std::string::npos);
    BOOST_CHECK(text.find("params nodeId=3a1f00, weight=1") != std::string::npos);
    BOOST_CHECK(text.find("Enter send   Esc cancel") != std::string::npos);
    // proposal mode on an auth chain
    context.proposals = true;
    form.confirmRows = tui::sendConfirmRows(form.pending, context);
    text = renderText(tui::renderFrame(model, nav, frame, "", &form), 120, 40);
    BOOST_CHECK(text.find("mode   proposal") != std::string::npos);
    BOOST_CHECK(text.find("createSetConsensusWeightProposal") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(featuresEntryListsFlagsAndTinySizesDoNotThrow)
{
    tui::Model model;
    model.send.featuresLoaded = true;
    model.send.features = {FeatureState{"feature_balance", false, false},
        FeatureState{"bugfix_revert", true, false},
        FeatureState{"feature_raw_address", false, true}};
    auto form = tui::makeSendForm(false);
    for (size_t i = 0; i < form.entries.size(); ++i)
    {
        if (form.entries[i].kind == tui::SendEntryKind::features)
        {
            form.tree.selected = i;
        }
    }
    form.featureChecked.insert("feature_balance");
    form.focus = 2;
    tui::Frame frame{.width = 120, .height = 40, .nowMs = 0};
    auto text = renderText(tui::renderSend(model, form, frame), 120, 40);
    BOOST_CHECK(text.find("[x]  feature_balance off") != std::string::npos);
    BOOST_CHECK(text.find("[on] bugfix_revert on") != std::string::npos);
    BOOST_CHECK(text.find("feature_raw_address off deprecated") != std::string::npos);
    BOOST_CHECK(text.find("enable selected") != std::string::npos);
    tui::Nav nav;
    nav.page = tui::Page::send;
    for (auto [w, h] : {std::pair{1, 1}, std::pair{10, 5}, std::pair{40, 12}, std::pair{60, 20}})
    {
        tui::Frame tiny{.width = w, .height = h, .nowMs = 0};
        BOOST_CHECK_NO_THROW(renderText(tui::renderFrame(model, nav, tiny, "", &form), w, h));
    }
    // EL mode: the tree holds raw only
    auto el = tui::makeSendForm(true);
    text = renderText(tui::renderSend(model, el, frame), 120, 40);
    BOOST_CHECK(text.find("raw transaction") != std::string::npos);
    BOOST_CHECK(text.find("consensus") == std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
