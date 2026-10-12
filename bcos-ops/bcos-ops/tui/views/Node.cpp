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
 * @file Node.cpp
 */
#include "Pages.h"
#include "bcos-ops/Output.h"
#include <ftxui/dom/table.hpp>

namespace bcos::ops::tui
{
using namespace ftxui;

namespace
{
template <typename T>
std::string nodeShow(std::optional<T> const& _value)
{
    if (!_value)
    {
        return "-";
    }
    if constexpr (std::is_same_v<T, bool>)
    {
        return *_value ? "true" : "false";
    }
    else if constexpr (std::is_same_v<T, std::string>)
    {
        return *_value;
    }
    else
    {
        return std::to_string(*_value);
    }
}

Element nodeCheckLine(Check const& _check)
{
    auto name = text(" " + _check.name + " ") | size(WIDTH, EQUAL, 20);
    auto detail = text(sanitizeForTerminal(_check.detail));
    if (_check.failed())
    {
        return hbox({text("FAIL") | color(Color::Red) | bold, name, detail | color(Color::Red)});
    }
    if (_check.ok())
    {
        return hbox({text(" ok ") | color(Color::Green), name, detail | dim});
    }
    return hbox({text("skip") | color(Color::Yellow), name, detail | dim});
}
}  // namespace

Element renderNode(Model const& _model)
{
    auto const& s = _model.status;
    std::vector<std::vector<std::string>> identity = {
        {"node", s.nodeId ? shortNodeId(*s.nodeId) : "-", "role",
            s.isConsensusNode ? (*s.isConsensusNode ? "sealer" : "observer") : "-"},
        {"version", nodeShow(s.version), "chain", nodeShow(s.chainId)},
        {"group", nodeShow(s.groupId), "sm", nodeShow(s.smCrypto)},
        {"auth", nodeShow(s.authCheck), "source", _model.source.empty() ? "-" : _model.source},
    };
    for (auto& row : identity)  // Table draws cells edge to edge; keep a gap between them
    {
        for (auto& cell : row)
        {
            cell = sanitizeForTerminal(cell) + "  ";  // version / chainId come from the node
        }
    }
    Table identityTable(identity);
    identityTable.SelectColumn(0).Decorate(dim);
    identityTable.SelectColumn(2).Decorate(dim);

    auto const& sync = _model.sync;
    auto local = sync.get("blockNumber", 0).asInt64();
    std::vector<std::vector<std::string>> peers = {{"peer", "height", "diff", "latestHash"}};
    for (auto const& peer : sync["peers"])
    {
        auto height = peer.get("blockNumber", 0).asInt64();
        peers.push_back({shortNodeId(peer.get("nodeID", "").asString()), std::to_string(height),
            std::to_string(height - local), abridged(peer.get("latestHash", "").asString())});
    }
    for (auto& row : peers)
    {
        for (auto& cell : row)
        {
            cell = sanitizeForTerminal(cell) + "  ";  // peer ids and hashes come from the node
        }
    }
    Table peerTable(peers);
    peerTable.SelectRow(0).Decorate(dim);
    auto syncHead = hbox({text(sync.get("isSyncing", false).asBool() ? " syncing" : " idle") | bold,
        text("  local ") | dim, text(std::to_string(local)), text("  highest ") | dim,
        text(std::to_string(sync.get("knownHighestNumber", 0).asInt64())), text("  lag ") | dim,
        text(nodeShow(s.lag)), text("  p2p connections ") | dim,
        text(std::to_string(_model.peers["peers"].size()))});

    Elements checks;
    for (auto const& check : _model.checks)
    {
        checks.push_back(nodeCheckLine(check));
    }
    if (checks.empty())
    {
        checks.push_back(text("waiting for the first refresh") | dim);
    }
    return vbox({window(text(" node "), identityTable.Render(), LIGHT),
        window(text(" sync / p2p "),
            vbox({syncHead, separatorLight(),
                sync.isNull() ? text("no sync status yet") | dim : peerTable.Render()}),
            LIGHT),
        window(text(" checks "), vbox(std::move(checks)), LIGHT)});
}
}  // namespace bcos::ops::tui
