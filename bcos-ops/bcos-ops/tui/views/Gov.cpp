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
 * @brief the browser rows of the home page: recent blocks and the transactions flattened out of
 * @brief the gov page (auth chains): committee rates and governors, the newest proposals, and
 *        the proposal detail with the vote / revoke hints. Everything shown comes from
 *        Model.governance; the keys live in govFormHandle (SendForm.cpp).
 * @file Gov.cpp
 */
#include "ListView.h"
#include "Pages.h"
#include "bcos-ops/Output.h"
#include <algorithm>

namespace bcos::ops::tui
{
using namespace ftxui;

namespace
{
Element govField(std::string const& _label, std::string const& _value, int _labelWidth = 17)
{
    return hbox(
        {text(_label) | dim | size(WIDTH, EQUAL, _labelWidth), text(sanitizeForTerminal(_value))});
}

Element govNotAuth()
{
    return window(text(" gov "),
        text("gov: not an auth chain (build_chain -a; proposals need a committee)") | dim, LIGHT);
}

Element govStatus(ProposalRow const& _row)
{
    auto word = text(_row.statusName);
    switch (_row.status)
    {
    case 1:
        return word | color(Color::Yellow);
    case 2:
        return word | color(Color::Green);
    case 3:
    case 4:
    case 5:
        return word | color(Color::Red);
    default:
        return word | dim;
    }
}

std::string govCell(ProposalRow const& _row, ListColumn const& _column)
{
    if (_column.name == "id")
    {
        return std::to_string(_row.id);
    }
    if (_column.name == "type")
    {
        return _row.typeName;
    }
    if (_column.name == "status")
    {
        return _row.statusName;
    }
    if (_column.name == "proposer")
    {
        return _row.proposer.empty() ? "-" : abridged(_row.proposer, 4);
    }
    if (_column.name == "agree")
    {
        return std::to_string(_row.agree);
    }
    return std::to_string(_row.against);
}
}  // namespace

Element renderGov(Model const& _model, Nav& _nav, Frame const& _frame)
{
    if (!_model.authChain)
    {
        _nav.proposals.count = 0;
        return govNotAuth();
    }
    auto const& gov = _model.governance;
    Elements committee;
    if (!gov.loaded)
    {
        committee.push_back(
            text(gov.error.empty() ? "reading the committee…" : sanitizeForTerminal(gov.error)) |
            dim);
    }
    else
    {
        committee.push_back(hbox({govField("participatesRate", gov.participatesRate + "%"),
            text("  "), govField("winRate", gov.winRate + "%", 8)}));
        committee.push_back(govField("committee", gov.committee));
        for (auto const& governor : gov.governors)
        {
            committee.push_back(
                govField("governor", governor.address + "  weight " + governor.weight));
        }
        if (gov.governors.empty())
        {
            committee.push_back(text("no governors") | dim);
        }
        if (!gov.error.empty())
        {
            committee.push_back(text(sanitizeForTerminal(gov.error)) | color(Color::Yellow));
        }
    }
    auto upper = window(text(" committee "), vbox(committee), LIGHT);
    int upperHeight = static_cast<int>(committee.size()) + 2;
    int listHeight = std::max(4, _frame.height - upperHeight);
    std::vector<ListColumn> columns = {{.name = "id", .width = 5, .priority = 0},
        {.name = "type", .width = 15, .priority = 1}, {.name = "status", .width = 8, .priority = 2},
        {.name = "proposer", .width = 11, .priority = 5},
        {.name = "agree", .width = 5, .priority = 3},
        {.name = "against", .width = 7, .priority = 4}};
    auto fit = fitListColumns(columns, _frame.width - 2);
    std::deque<ProposalRow> rows(gov.proposals.begin(), gov.proposals.end());
    std::string title = " proposals " + std::to_string(gov.proposalCount) + " ";
    auto list = renderListView<ProposalRow>(
        title, fit, rows, _nav.proposals, !_nav.inDetail() && _nav.page == Page::gov, listHeight,
        [&](ProposalRow const& row, ListColumn const& column) { return govCell(row, column); },
        [](ProposalRow const&) { return false; });
    return vbox({upper, list | flex});
}

Element renderProposalDetail(
    Model const& _model, Frame const& _frame, DetailRef const& _ref, SendForm const* _form)
{
    if (!_model.authChain)
    {
        return govNotAuth();
    }
    auto const& gov = _model.governance;
    auto detail = gov.details.find(_ref.number);
    ProposalRow const* row = detail == gov.details.end() ? nullptr : &detail->second;
    if (row == nullptr)
    {
        auto listed = std::find_if(gov.proposals.begin(), gov.proposals.end(),
            [&](ProposalRow const& r) { return r.id == _ref.number; });
        row = listed == gov.proposals.end() ? nullptr : &*listed;
    }
    Elements lines;
    if (row == nullptr)
    {
        lines.push_back(text("loading proposal…") | dim);
    }
    else
    {
        Elements left = {govField("id", std::to_string(row->id)),
            govField("type", row->typeName + " (" + std::to_string(row->type) + ")"),
            govField("resource", row->resource), govField("proposer", row->proposer),
            hbox({text("status") | dim | size(WIDTH, EQUAL, 17), govStatus(*row)})};
        Elements right = {govField("agree", std::to_string(row->agree)),
            govField("against", std::to_string(row->against)),
            govField("expiresAtBlock", row->expiresAtBlock),
            govField("executed", row->executed ? "yes" : "no"),
            govField("execCode",
                row->execCode ? std::to_string(*row->execCode) +
                                    (row->execCodeName.empty() ? "" : " " + row->execCodeName) :
                                "-")};
        lines.push_back(_frame.width >= 100 ? hbox({vbox(left) | flex, vbox(right) | flex}) :
                                              vbox({vbox(left), vbox(right)}));
    }
    lines.push_back(separatorLight());
    lines.push_back(text("v vote for   x vote against   d revoke   (governor key required)") | dim);
    if (_form != nullptr && !_form->notice.empty())
    {
        lines.push_back(text(sanitizeForTerminal(_form->notice)) | color(Color::Yellow));
    }
    auto const& last = _model.send.last;
    if (last.seq != 0 && !last.line.empty())
    {
        lines.push_back(
            hbox({text(last.ok ? "ok " : "FAIL ") | color(last.ok ? Color::Green : Color::Red),
                text(sanitizeForTerminal(last.line))}));
    }
    if (!gov.error.empty())
    {
        lines.push_back(text(sanitizeForTerminal(gov.error)) | color(Color::Yellow));
    }
    return window(
        text(" proposal " + std::to_string(_ref.number) + " "), vbox(std::move(lines)), LIGHT);
}

void resolveProposalRefs(Nav& _nav, GovernanceState const& _governance)
{
    for (auto& ref : _nav.stack)
    {
        if (ref.kind != DetailKind::proposal || ref.resolved)
        {
            continue;
        }
        auto index = static_cast<size_t>(std::max<int64_t>(0, ref.number));
        if (index < _governance.proposals.size())
        {
            ref.number = _governance.proposals[index].id;
            ref.resolved = true;
        }
    }
}

GovernanceWants governanceWants(Nav const& _nav)
{
    GovernanceWants wants;
    wants.rows = std::max<size_t>(_nav.proposals.pageRows, 1);
    for (auto const& ref : _nav.stack)
    {
        if (ref.kind == DetailKind::proposal && ref.resolved)
        {
            wants.ids.push_back(ref.number);
        }
    }
    return wants;
}
}  // namespace bcos::ops::tui
