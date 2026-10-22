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
 * @file Consensus.cpp
 */
#include "Pages.h"
#include "bcos-ops/Output.h"
#include <ftxui/dom/table.hpp>

namespace bcos::ops::tui
{
using namespace ftxui;

Element renderConsensus(Model const& _model)
{
    auto const& c = _model.consensus;
    bool timeout = c.get("timeout", false).asBool();
    auto title = text(timeout ? " consensus: TIMEOUT (view change in progress) " : " consensus ");
    if (timeout)
    {
        title = title | color(Color::Red) | bold;
    }
    std::vector<std::vector<std::string>> rows = {
        {"index", "nodeID", "weight", "termWeight", "leader"}};
    auto leader = c.get("leaderIndex", -1).asInt64();
    for (auto const& node : c["consensusNodeList"])
    {
        auto index = node.get("index", -1).asInt64();
        // the node ids and a string weight come from the node, like Node.cpp's peer ids
        rows.push_back({std::to_string(index),
            sanitizeForTerminal(shortNodeId(node.get("nodeID", "").asString())),
            node.get("weight", 0).isString() ?
                sanitizeForTerminal(node.get("weight", 0).asString()) :
                std::to_string(node.get("weight", 0).asInt64()),
            std::to_string(node.get("termWeight", 0).asInt64()), index == leader ? "*" : ""});
    }
    Table table(rows);
    table.SelectRow(0).Decorate(dim);
    table.SelectAll().SeparatorVertical(LIGHT);
    if (leader >= 0 && leader + 1 < static_cast<int64_t>(rows.size()))
    {
        table.SelectRow(static_cast<int>(leader) + 1).Decorate(bold);
    }
    auto state = hbox({text(" view ") | dim, text(std::to_string(c.get("view", 0).asInt64())),
        text("  changeCycle ") | dim, text(std::to_string(c.get("changeCycle", 0).asInt64())),
        text("  connected ") | dim, text(std::to_string(c.get("connectedNodeList", 0).asInt64())),
        text("  quorum ") | dim, text(std::to_string(c.get("minRequiredQuorum", 0).asInt64()))});
    return window(title,
        vbox({state, separatorLight(),
            c.isNull() ? text("no consensus status yet") | dim : table.Render()}),
        LIGHT);
}
}  // namespace bcos::ops::tui
