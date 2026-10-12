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
 * @file Home.cpp
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
constexpr int c_sideBySideColumns = 100;
constexpr int c_bandRows = 4;  // window border 2 + two lines

std::string homeCheckSummary(Model const& _model)
{
    if (_model.checks.empty())
    {
        return "checks -";
    }
    size_t failed = 0;
    for (auto const& check : _model.checks)
    {
        failed += check.failed() ? 1 : 0;
    }
    return failed > 0 ? "checks " + std::to_string(failed) + " FAIL" :
                        "checks " + std::to_string(_model.checks.size()) + " ok";
}

template <typename T>
std::string homeShow(std::optional<T> const& _value)
{
    return _value ? std::to_string(*_value) : "-";
}

Element homeBand(Model const& _model, Frame const& _frame)
{
    auto const& s = _model.status;
    auto summary = homeCheckSummary(_model);
    auto summaryColor = summary.find("FAIL") != std::string::npos ? Color::Red : Color::Green;
    auto line1 = hbox({text(" height ") | dim, text(homeShow(s.blockNumber)), text("  view ") | dim,
        text(homeShow(s.view)), text("  leader ") | dim, text(homeShow(s.leaderIndex)),
        text("  nodes ") | dim,
        text(homeShow(s.connectedGroupNodes) + "/" + homeShow(s.consensusNodesNum)),
        text("  quorum ") | dim, text(homeShow(s.minRequiredQuorum)), text("  "),
        text(summary) | color(summaryColor)});
    auto age = lastBlockAgeMs(_model.explorer, _frame.nowMs);
    auto interval = avgIntervalMs(_model.explorer, 20);
    auto window20 = std::min<size_t>(
        20, _model.explorer.blocks.size() > 0 ? _model.explorer.blocks.size() - 1 : 0);
    std::string intervalText = interval ? std::to_string(*interval / 1000) + "." +
                                              std::to_string((*interval % 1000) / 100) +
                                              "s (last " + std::to_string(window20) + ")" :
                                          "-";
    auto line2 = hbox(
        {text(" pending ") | dim, text(homeShow(s.pendingTxSize) + "/" + homeShow(s.txpoolLimit)),
            text("   last block ") | dim, text(age ? ageText(*age) + " ago" : "-"),
            text("   avg interval ") | dim, text(intervalText)});
    if (s.inTimeout.value_or(false))
    {
        line2 = hbox({line2, text("   view change in progress") | color(Color::Yellow)});
    }
    return window(text(" chain "), vbox({line1, line2}), LIGHT);
}
}  // namespace

Element renderHome(Model const& _model, Nav& _nav, Frame const& _frame)
{
    bool sideBySide = _frame.width >= c_sideBySideColumns;
    int listsHeight = std::max(4, _frame.height - c_bandRows);
    int listHeight = sideBySide ? listsHeight : listsHeight / 2;
    int listWidth = (sideBySide ? _frame.width / 2 : _frame.width) - 2;  // borders

    std::vector<ListColumn> blockColumns = {
        {"height", 7, 0}, {"hash", 11, 1}, {"txs", 4, 2}, {"time", 5, 4}, {"age", 5, 3}};
    std::vector<ListColumn> txColumns = {
        {"hash", 11, 0}, {"from", 9, 3}, {"to", 9, 4}, {"status", 6, 1}, {"height", 7, 2}};
    auto blocksFit = fitListColumns(blockColumns, listWidth);
    auto txsFit = fitListColumns(txColumns, listWidth);

    bool focusBlocks = !_nav.inDetail() && _nav.page == Page::home && _nav.focus == 0;
    bool focusTxs = !_nav.inDetail() && _nav.page == Page::home && _nav.focus == 1;
    auto blocks = renderListView<BlockRow>(
        " blocks ", blocksFit, _model.explorer.blocks, _nav.blocks, focusBlocks, listHeight,
        [&](BlockRow const& row, ListColumn const& column) -> std::string {
            if (column.name == "height")
            {
                return std::to_string(row.number);
            }
            if (column.name == "hash")
            {
                return abridged(row.hash, 4);
            }
            if (column.name == "txs")
            {
                return std::to_string(row.txCount);
            }
            if (column.name == "time")
            {
                return clockText(row.timestampMs);
            }
            return row.timestampMs > 0 ?
                       ageText(std::max<int64_t>(0, _frame.nowMs - row.timestampMs)) :
                       "-";
        },
        [](BlockRow const& row) { return row.gapBelow; });
    auto txs = renderListView<TxRow>(
        " txs ", txsFit, _model.explorer.txs, _nav.txs, focusTxs, listHeight,
        [&](TxRow const& row, ListColumn const& column) -> std::string {
            if (column.name == "hash")
            {
                return abridged(row.hash, 4);
            }
            if (column.name == "from")
            {
                return row.from.empty() ? "-" : abridged(row.from, 4);
            }
            if (column.name == "to")
            {
                return row.to.empty() ? "create" : abridged(row.to, 4);
            }
            if (column.name == "status")
            {
                return txStatusWord(row.status);
            }
            return std::to_string(row.blockNumber);
        },
        [](TxRow const&) { return false; });
    auto band = homeBand(_model, _frame);
    if (sideBySide)
    {
        return vbox({band, hbox({blocks | flex, txs | flex}) | flex});
    }
    return vbox({band, blocks | flex, txs | flex});
}
}  // namespace bcos::ops::tui
