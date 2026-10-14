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
 * @brief the send page's key map: Tab order tree / key / fields / button, Enter on the button
 *        opens the confirmation layer only with a key, Esc closes layers before anything else,
 *        the confirmation rows match the CLI's, proposals show mode proposal
 * @file SendFormTest.cpp
 */
#include "bcos-ops/tui/SendForm.h"
#include "bcos-ops/OpsError.h"
#include "bcos-ops/tx/Presets.h"
#include <boost/test/unit_test.hpp>

namespace bcos::ops::test
{
namespace
{
using tui::SendAction;
using tui::SendForm;

/// a form with consensus add-sealer under the cursor
SendForm sendFormOnAddSealer()
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
    // the first key event binds the fields to the entry under the cursor (and clears them)
    tui::SendContext context;
    tui::sendFormHandle(form, context, ftxui::Event::Custom);
    return form;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(SendFormTest)

BOOST_AUTO_TEST_CASE(treeListsTheGroupsAndExpandsOnEnter)
{
    auto form = tui::makeSendForm(false);
    tui::SendContext context;
    // hello is open by default: deploy + set + get under it
    BOOST_REQUIRE(form.entries.size() > 4);
    BOOST_CHECK_EQUAL(form.entries[0].label, "- hello");
    BOOST_CHECK_EQUAL(form.entries[1].label, "  deploy HelloWorld");
    BOOST_CHECK_EQUAL(form.entries[2].label, "  set");
    BOOST_CHECK_EQUAL(form.entries[3].label, "  get");
    BOOST_CHECK_EQUAL(form.entries[4].label, "+ consensus");
    // Enter on a closed group opens it; the write presets appear, the reads do not
    form.tree.selected = 4;
    BOOST_CHECK(tui::sendFormHandle(form, context, ftxui::Event::Return) == SendAction::redraw);
    BOOST_CHECK_EQUAL(form.entries[4].label, "- consensus");
    BOOST_CHECK_EQUAL(form.entries[5].label, "  add-sealer");
    bool hasRead = false;
    for (auto const& entry : form.entries)
    {
        hasRead = hasRead || entry.label == "  sealers";
    }
    BOOST_CHECK(!hasRead);
    // the tail entries
    auto const& last = form.entries.back();
    BOOST_CHECK(last.kind == tui::SendEntryKind::custom);
    // EL mode: raw only
    auto el = tui::makeSendForm(true);
    BOOST_REQUIRE_EQUAL(el.entries.size(), 1U);
    BOOST_CHECK(el.entries[0].kind == tui::SendEntryKind::raw);
}

BOOST_AUTO_TEST_CASE(tabCyclesTreeKeyFieldsButtonAndEscGoesBackToTheTree)
{
    auto form = sendFormOnAddSealer();
    tui::SendContext context;
    BOOST_REQUIRE_EQUAL(form.fieldCount(), 2);  // nodeId, weight
    BOOST_CHECK_EQUAL(form.buttonFocus(), 4);
    BOOST_CHECK(tui::sendFormHandle(form, context, ftxui::Event::Tab) == SendAction::redraw);
    BOOST_CHECK_EQUAL(form.focus, 1);
    BOOST_CHECK_EQUAL(form.inputId(), "key");
    tui::sendFormHandle(form, context, ftxui::Event::Tab);
    BOOST_CHECK_EQUAL(form.inputId(), "p0");
    tui::sendFormHandle(form, context, ftxui::Event::Tab);
    BOOST_CHECK_EQUAL(form.inputId(), "p1");
    tui::sendFormHandle(form, context, ftxui::Event::Tab);
    BOOST_CHECK_EQUAL(form.focus, form.buttonFocus());
    BOOST_CHECK(!form.editing());
    tui::sendFormHandle(form, context, ftxui::Event::Tab);
    BOOST_CHECK_EQUAL(form.focus, 0);
    // plain characters on a field go to the Input; Esc leaves the field
    form.focus = 2;
    BOOST_CHECK(tui::sendFormHandle(form, context, ftxui::Event::Character('x')) ==
                SendAction::passToInput);
    BOOST_CHECK(tui::sendFormHandle(form, context, ftxui::Event::Escape) == SendAction::redraw);
    BOOST_CHECK_EQUAL(form.focus, 0);
    // on the tree Esc and digits belong to the page key map
    BOOST_CHECK(tui::sendFormHandle(form, context, ftxui::Event::Escape) == SendAction::toNav);
    BOOST_CHECK(
        tui::sendFormHandle(form, context, ftxui::Event::Character('1')) == SendAction::toNav);
    // Enter on the key field loads the key
    form.focus = 1;
    form.keyFile = "/tmp/k.pem";
    BOOST_CHECK(tui::sendFormHandle(form, context, ftxui::Event::Return) == SendAction::loadKey);
}

BOOST_AUTO_TEST_CASE(sendButtonNeedsAKeyThenOpensTheConfirmationLayer)
{
    auto form = sendFormOnAddSealer();
    tui::SendContext context;
    form.fields[0] = "3a1f00";
    form.fields[1] = "1";
    form.focus = form.buttonFocus();
    BOOST_CHECK(tui::sendFormHandle(form, context, ftxui::Event::Return) == SendAction::redraw);
    BOOST_CHECK(!form.confirm);
    BOOST_CHECK_EQUAL(form.notice, "load a key first");
    context.hasKey = true;
    context.routingKnown = true;  // the key load answered direct | proposal
    BOOST_CHECK(tui::sendFormHandle(form, context, ftxui::Event::Return) == SendAction::redraw);
    BOOST_REQUIRE(form.confirm);
    BOOST_REQUIRE_EQUAL(form.confirmRows.size(), 4U);
    BOOST_CHECK_EQUAL(form.confirmRows[0].second, "direct");
    BOOST_CHECK_EQUAL(form.confirmRows[1].second, "0x0000000000000000000000000000000000001003");
    BOOST_CHECK_EQUAL(form.confirmRows[2].second, "addSealer(string,uint256)");
    BOOST_CHECK_EQUAL(form.confirmRows[3].second, "nodeId=3a1f00, weight=1");
    // Esc closes the layer first, Enter submits the pending request
    BOOST_CHECK(tui::sendFormHandle(form, context, ftxui::Event::Escape) == SendAction::redraw);
    BOOST_CHECK(!form.confirm);
    tui::sendFormHandle(form, context, ftxui::Event::Return);
    BOOST_REQUIRE(form.confirm);
    BOOST_CHECK(tui::sendFormHandle(form, context, ftxui::Event::Return) == SendAction::submit);
    BOOST_CHECK(!form.confirm);
    BOOST_CHECK_EQUAL(form.focus, 0);  // back on the tree once the detail closes
    BOOST_CHECK(form.pending.kind == tui::SendRequest::Kind::preset);
    BOOST_REQUIRE_EQUAL(form.pending.params.size(), 2U);
    BOOST_CHECK_EQUAL(form.pending.params[1], "1");
    // a missing value is a notice, not a request
    form.fields[1].clear();
    form.focus = form.buttonFocus();
    tui::sendFormHandle(form, context, ftxui::Event::Return);
    BOOST_CHECK(!form.confirm);
    BOOST_CHECK_EQUAL(form.notice, "missing weight");
}

BOOST_AUTO_TEST_CASE(authChainConfirmationShowsTheProposal)
{
    auto form = sendFormOnAddSealer();
    tui::SendContext context;
    context.hasKey = true;
    context.routingKnown = true;
    context.proposals = true;
    form.fields[0] = "3a1f00";
    form.fields[1] = "2";
    form.focus = form.buttonFocus();
    tui::sendFormHandle(form, context, ftxui::Event::Return);
    BOOST_REQUIRE(form.confirm);
    BOOST_REQUIRE_EQUAL(form.confirmRows.size(), 5U);
    BOOST_CHECK_EQUAL(form.confirmRows[0].second, "proposal");
    BOOST_CHECK_EQUAL(form.confirmRows[1].second, "0x0000000000000000000000000000000000010001");
    BOOST_CHECK_EQUAL(
        form.confirmRows[2].second, "createSetConsensusWeightProposal(string,uint32,bool,uint256)");
    BOOST_CHECK(form.confirmRows[3].second.find("3a1f00") != std::string::npos);
    BOOST_CHECK(form.confirmRows[3].second.find("1000") != std::string::npos);  // interval
    BOOST_CHECK_EQUAL(form.confirmRows[4].second, "stands in for addSealer(string,uint256)");
}

BOOST_AUTO_TEST_CASE(featuresRawAndCustomBuildTheirRequests)
{
    auto form = tui::makeSendForm(false);
    tui::SendContext context;
    context.hasKey = true;
    context.featuresLoaded = true;
    context.features = {FeatureState{"feature_balance", false, false},
        FeatureState{"feature_balance_precompiled", false, false},
        FeatureState{"bugfix_revert", true, false}};
    // features: the cursor on the entry asks for the flags once, space toggles, on flags refuse
    for (size_t i = 0; i < form.entries.size(); ++i)
    {
        if (form.entries[i].kind == tui::SendEntryKind::features)
        {
            form.tree.selected = i;
        }
    }
    form.entryKey.clear();
    BOOST_CHECK(tui::sendFormHandle(form, context, ftxui::Event::Return) == SendAction::redraw);
    BOOST_CHECK_EQUAL(form.focus, 2);  // the list
    tui::sendFormHandle(form, context, ftxui::Event::Character(' '));
    BOOST_CHECK(form.featureChecked.contains("feature_balance"));
    tui::sendFormHandle(form, context, ftxui::Event::Character('j'));
    tui::sendFormHandle(form, context, ftxui::Event::Character('j'));
    tui::sendFormHandle(form, context, ftxui::Event::Character(' '));
    BOOST_CHECK(!form.featureChecked.contains("bugfix_revert"));
    BOOST_CHECK(form.notice.find("already on") != std::string::npos);
    auto request = tui::sendFormRequest(form, context);
    BOOST_CHECK(request.kind == tui::SendRequest::Kind::enableFeatures);
    BOOST_REQUIRE_EQUAL(request.features.size(), 1U);
    BOOST_CHECK_EQUAL(request.features[0], "feature_balance");
    auto rows = tui::sendConfirmRows(request, context);
    BOOST_CHECK_EQUAL(rows[2].second, "setValueByKey(string,string)");
    // raw
    form.tree.selected = form.entries.size() - 2;
    form.entryKey.clear();
    tui::sendFormHandle(form, context, ftxui::Event::Tab);
    form.rawText = "@/tmp/signed.hex";
    request = tui::sendFormRequest(form, context);
    BOOST_CHECK(request.kind == tui::SendRequest::Kind::raw);
    rows = tui::sendConfirmRows(request, context);
    BOOST_CHECK_EQUAL(rows[0].second, "raw");
    BOOST_CHECK_EQUAL(rows[2].second, "file /tmp/signed.hex");
    // custom: the fields follow the signature
    form.tree.selected = form.entries.size() - 1;
    form.entryKey.clear();
    tui::sendFormHandle(form, context, ftxui::Event::Tab);
    form.customTo = "0xa1";
    form.customSig = "set(string,uint256)";
    auto specs = form.fieldSpecs();
    BOOST_REQUIRE_EQUAL(specs.size(), 5U);  // to, sig, arg0, arg1, value
    BOOST_CHECK_EQUAL(specs[2].type, "string");
    BOOST_CHECK_EQUAL(specs[3].type, "uint256");
    form.fields[0] = "hi";
    form.fields[1] = "7";
    form.customValue = "5";
    request = tui::sendFormRequest(form, context);
    BOOST_CHECK(request.kind == tui::SendRequest::Kind::custom);
    BOOST_CHECK_EQUAL(request.to, "0xa1");
    BOOST_CHECK_EQUAL(request.value, "5");
    rows = tui::sendConfirmRows(request, context);
    BOOST_CHECK_EQUAL(rows[2].second, "set(string,uint256)");
    BOOST_CHECK_EQUAL(rows[3].second, "hi, 7");
    BOOST_CHECK_EQUAL(rows[4].second, "5");
    // hello set needs the deployed address
    form.tree.selected = 2;
    form.entryKey.clear();
    tui::sendFormHandle(form, context, ftxui::Event::Tab);
    BOOST_CHECK_THROW(tui::sendFormRequest(form, context), OpsError);
    context.helloAddress = "0xc02d";
    form.entryKey.clear();
    tui::sendFormHandle(form, context, ftxui::Event::Tab);
    form.fields[0] = "hi";
    request = tui::sendFormRequest(form, context);
    BOOST_CHECK_EQUAL(request.to, "0xc02d");
    BOOST_CHECK(request.preset != nullptr && request.preset->name == "set");
}

BOOST_AUTO_TEST_CASE(theLayerNeedsTheRoutingAndFreezesItIntoTheRequest)
{
    auto form = sendFormOnAddSealer();
    tui::SendContext context;
    context.hasKey = true;
    context.routingKnown = false;
    form.fields[0] = "3a1f00";
    form.fields[1] = "1";
    form.focus = form.buttonFocus();
    BOOST_CHECK(tui::sendFormHandle(form, context, ftxui::Event::Return) == SendAction::redraw);
    BOOST_CHECK(!form.confirm);
    BOOST_CHECK(form.notice.find("routing unknown") != std::string::npos);
    // known and proposals: the layer says proposal and the request carries exactly that
    context.routingKnown = true;
    context.proposals = true;
    BOOST_CHECK(tui::sendFormHandle(form, context, ftxui::Event::Return) == SendAction::redraw);
    BOOST_REQUIRE(form.confirm);
    BOOST_CHECK(form.pending.routing.proposals);
    BOOST_CHECK_EQUAL(form.confirmRows[0].second, "proposal");
    tui::sendFormHandle(form, context, ftxui::Event::Escape);
    // known and direct
    context.proposals = false;
    tui::sendFormHandle(form, context, ftxui::Event::Return);
    BOOST_REQUIRE(form.confirm);
    BOOST_CHECK(!form.pending.routing.proposals);
    BOOST_CHECK_EQUAL(form.confirmRows[0].second, "direct");
    // a read preset never needs the routing
    tui::sendFormHandle(form, context, ftxui::Event::Escape);
    tui::SendRequest read;
    read.kind = tui::SendRequest::Kind::preset;
    read.preset = findPreset("consensus", "sealers");
    BOOST_REQUIRE(read.preset != nullptr);
    BOOST_CHECK(!tui::SendRequest::mayRouteThroughProposal(read.kind, read.preset));
    // a write without a ProposalSpec (3-arg addSealer) never routes through a proposal
    auto const* direct = findPreset("consensus", "add-sealer-term");
    BOOST_REQUIRE(direct != nullptr);
    BOOST_CHECK(!tui::SendRequest::mayRouteThroughProposal(read.kind, direct));
    BOOST_CHECK(tui::SendRequest::mayRouteThroughProposal(
        read.kind, findPreset("consensus", "add-observer")));
    BOOST_CHECK(
        tui::SendRequest::mayRouteThroughProposal(tui::SendRequest::Kind::enableFeatures, nullptr));
}

BOOST_AUTO_TEST_CASE(featuresConfirmationShowsTheDependencyOrder)
{
    tui::SendRequest request;
    request.kind = tui::SendRequest::Kind::enableFeatures;
    request.features = {"feature_balance_precompiled"};
    tui::SendContext context;
    auto rows = tui::sendConfirmRows(request, context);
    BOOST_CHECK_EQUAL(rows[0].second, "direct");
    BOOST_CHECK_EQUAL(rows[1].second, "0x0000000000000000000000000000000000001000");
    BOOST_CHECK_EQUAL(rows[2].second, "setValueByKey(string,string)");
    BOOST_CHECK(
        rows[3].second.starts_with("2 step(s): feature_balance=1, feature_balance_precompiled=1"));
    request.routing.proposals = true;
    rows = tui::sendConfirmRows(request, context);
    BOOST_CHECK_EQUAL(rows[0].second, "proposal");
    // what is actually sent in proposal mode: the committee's proposal, not setValueByKey
    BOOST_CHECK_EQUAL(rows[1].second, "0x0000000000000000000000000000000000010001");
    BOOST_CHECK(rows[2].second.starts_with("createSetSysConfigProposal("));
    BOOST_CHECK(rows.back().second.find("createSetSysConfigProposal") != std::string::npos);
    // a flag the page read as on is not a step
    context.features = {{.name = "feature_balance", .enabled = true}};
    rows = tui::sendConfirmRows(request, context);
    BOOST_CHECK(rows[3].second.starts_with("1 step(s): feature_balance_precompiled=1"));
    BOOST_CHECK_EQUAL(rows[4].first, "already on");
    BOOST_CHECK_EQUAL(rows[4].second, "feature_balance");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
