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
 * @file Views.cpp
 */
#include "Views.h"
#include "bcos-ops/Output.h"
#include <chrono>
#include <ftxui/dom/table.hpp>

namespace bcos::ops::tui
{
using namespace ftxui;

namespace
{
template <typename T>
std::string show(std::optional<T> const& _value)
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

std::string shortId(std::string const& _hex)
{
    auto body = _hex.starts_with("0x") ? _hex.substr(2) : _hex;
    return body.size() > 8 ? body.substr(0, 8) : body;
}

Element checkLine(Check const& _check)
{
    auto label = text(" " + _check.name + " ");
    if (_check.failed())
    {
        return hbox({text("FAIL") | color(Color::Red) | bold, label,
            text(_check.detail) | color(Color::Red)});
    }
    if (_check.ok())
    {
        return hbox({text(" ok ") | color(Color::Green), label});
    }
    return hbox({text("skip") | color(Color::GrayDark), label,
        text(_check.detail) | color(Color::GrayDark)});
}
}  // namespace

Element renderTopBar(Model const& _model)
{
    auto node = _model.status.nodeId ? shortId(*_model.status.nodeId) : "--------";
    auto source = _model.source.empty() ? "connecting" : _model.source;
    Elements cells = {text(" fisco-bcos tui ") | bold, text(" node " + node),
        text("  source " + source),
        text("  refresh in " + std::to_string(_model.refreshInSeconds) + "s ")};
    auto bar = hbox(std::move(cells));
    if (!_model.error.empty())
    {
        return hbox({bar, text("  " + _model.error) | color(Color::Red)}) |
               bgcolor(Color::RedLight) | color(Color::Black);
    }
    return bar | inverted;
}

Element renderOverview(Model const& _model)
{
    auto const& s = _model.status;
    std::vector<std::vector<std::string>> rows = {
        {"node",
            (s.nodeId ? shortId(*s.nodeId) : "-") +
                (s.isConsensusNode ? (*s.isConsensusNode ? " (sealer)" : " (observer)") : ""),
            "version", show(s.version)},
        {"height", show(s.blockNumber), "hash", s.latestHash ? abridged(*s.latestHash) : "-"},
        {"view", show(s.view), "leader", show(s.leaderIndex)},
        {"nodes", show(s.consensusNodesNum), "connected", show(s.connectedGroupNodes)},
        {"quorum", show(s.minRequiredQuorum), "inTimeout", show(s.inTimeout)},
        {"syncing", show(s.isSyncing), "highest", show(s.knownHighestNumber)},
        {"lag", show(s.lag), "peers", show(s.peerCount)},
        {"pending", show(s.pendingTxSize), "limit", show(s.txpoolLimit)},
    };
    Table table(rows);
    table.SelectColumn(0).Decorate(dim);
    table.SelectColumn(2).Decorate(dim);
    Elements checks;
    size_t failed = 0;
    for (auto const& check : _model.checks)
    {
        checks.push_back(checkLine(check));
        failed += check.failed() ? 1 : 0;
    }
    auto summary =
        _model.checks.empty() ? text("checks: waiting for the first refresh") | dim :
        failed > 0 ? text("checks " + std::to_string(failed) + " fail") | color(Color::Red) | bold :
                     text("checks " + std::to_string(_model.checks.size()) + " ok") |
                         color(Color::Green);
    return vbox({table.Render(), separator(), summary, vbox(std::move(checks))}) | border;
}

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
        rows.push_back({std::to_string(index), shortId(node.get("nodeID", "").asString()),
            node.get("weight", 0).asString().empty() ?
                std::to_string(node.get("weight", 0).asInt64()) :
                node.get("weight", 0).asString(),
            std::to_string(node.get("termWeight", 0).asInt64()), index == leader ? "*" : ""});
    }
    Table table(rows);
    table.SelectRow(0).Decorate(bold);
    table.SelectAll().SeparatorVertical(LIGHT);
    if (leader >= 0 && leader + 1 < static_cast<int64_t>(rows.size()))
    {
        table.SelectRow(static_cast<int>(leader) + 1).Decorate(bold);
    }
    auto state = hbox({text("view " + std::to_string(c.get("view", 0).asInt64())),
        text("  changeCycle " + std::to_string(c.get("changeCycle", 0).asInt64())),
        text("  connected " + std::to_string(c.get("connectedNodeList", 0).asInt64())),
        text("  quorum " + std::to_string(c.get("minRequiredQuorum", 0).asInt64()))});
    return vbox({title, state, separator(),
               c.isNull() ? text("no consensus status yet") | dim : table.Render()}) |
           border;
}

Element renderSync(Model const& _model)
{
    auto const& s = _model.sync;
    auto local = s.get("blockNumber", 0).asInt64();
    std::vector<std::vector<std::string>> rows = {{"peer", "height", "diff", "latestHash"}};
    for (auto const& peer : s["peers"])
    {
        auto height = peer.get("blockNumber", 0).asInt64();
        rows.push_back({shortId(peer.get("nodeID", "").asString()), std::to_string(height),
            std::to_string(height - local), abridged(peer.get("latestHash", "").asString())});
    }
    Table table(rows);
    table.SelectRow(0).Decorate(bold);
    auto head = hbox({text(s.get("isSyncing", false).asBool() ? "syncing" : "idle") | bold,
        text("  local " + std::to_string(local)),
        text("  highest " + std::to_string(s.get("knownHighestNumber", 0).asInt64())),
        text("  p2p connections " + std::to_string(_model.peers["peers"].size()))});
    return vbox({text(" sync / p2p "), head, separator(),
               s.isNull() ? text("no sync status yet") | dim : table.Render()}) |
           border;
}

Element renderTx(Model const& _model)
{
    Elements lines;
    if (_model.smokeRunning)
    {
        lines.push_back(text("running smoke ...") | color(Color::Yellow));
    }
    if (!_model.smokeError.empty())
    {
        lines.push_back(text("error: " + _model.smokeError) | color(Color::Red));
    }
    if (_model.smoke)
    {
        for (auto const& step : _model.smoke->steps)
        {
            lines.push_back(hbox({text(step.ok ? " ok  " : "FAIL ") |
                                      color(step.ok ? Color::Green : Color::Red),
                text(step.name) | size(WIDTH, EQUAL, 8),
                text(step.txHash.empty() ? "-" : abridged(step.txHash, 6)) | size(WIDTH, EQUAL, 16),
                text(" " + step.detail)}));
        }
        if (!_model.smoke->ok)
        {
            lines.push_back(text("reason=" + _model.smoke->reason) | color(Color::Red));
        }
    }
    if (lines.empty())
    {
        lines.push_back(text("press Enter on the button to deploy HelloWorld, set and get") | dim);
    }
    return vbox(std::move(lines));
}

Element renderLog(Model const& _model, std::string const& _filter)
{
    Elements lines;
    for (auto const& event : _model.logTail)
    {
        if (!LogTail::matches(event, _filter))
        {
            continue;
        }
        std::string badges;
        for (auto const& badge : event.badges)
        {
            badges += "[" + badge + "]";
        }
        // the key/value tail: whatever follows the event name in the raw message (decorators
        // and [blk-N] badges make the prefix length unknowable from the parsed parts)
        auto namePos = event.name.empty() ? std::string::npos : event.raw.find(event.name);
        auto rest = namePos == std::string::npos ? std::string() :
                                                   event.raw.substr(namePos + event.name.size());
        auto line = hbox(
            {text(event.timestamp.size() > 11 ? event.timestamp.substr(11, 12) : event.timestamp) |
                    dim,
                text(" " + sanitizeForTerminal(badges)) | dim,
                text(sanitizeForTerminal(event.name)) | bold, text(sanitizeForTerminal(rest))});
        if (!_filter.empty())
        {
            line = line | color(Color::Yellow);
        }
        else if (event.level == "warning" || event.level == "error")
        {
            line = line | color(Color::Red);
        }
        lines.push_back(line);
    }
    if (lines.empty())
    {
        lines.push_back(
            text(_filter.empty() ? "waiting for log lines" : "no line matches '" + _filter + "'") |
            dim);
    }
    return vbox(std::move(lines)) | vscroll_indicator | yframe | flex;
}

Component makeTxView(Model& _model, std::function<void()> _runSmoke)
{
    auto button = Button(" run smoke ", std::move(_runSmoke), ButtonOption::Border());
    return Renderer(button, [button, &_model]() {
        std::lock_guard<std::mutex> lock(_model.mutex);
        return vbox({text(" tx "), button->Render(), separator(), renderTx(_model)}) | border;
    });
}

Component makeLogView(Model& _model, std::string& _filter)
{
    auto input = Input(&_filter, "filter: event name or channel (e.g. ViewChange, TXPOOL)");
    return Renderer(input, [input, &_model, &_filter]() {
        std::lock_guard<std::mutex> lock(_model.mutex);
        return vbox({hbox({text(" log  filter> "), input->Render() | flex}), separator(),
                   renderLog(_model, _filter)}) |
               border;
    });
}
}  // namespace bcos::ops::tui
