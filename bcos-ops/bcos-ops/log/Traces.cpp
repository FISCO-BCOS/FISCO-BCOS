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
 * @file Traces.cpp
 */
#include "Traces.h"
#include "EventNames.h"
#include <algorithm>
#include <ctime>
#include <map>

namespace bcos::ops
{
namespace
{
std::string stripHex(std::string _value)
{
    if (_value.starts_with("0x") || _value.starts_with("0X"))
    {
        _value = _value.substr(2);
    }
    auto dots = _value.find("...");
    if (dots != std::string::npos)
    {
        _value = _value.substr(0, dots);
    }
    std::transform(_value.begin(), _value.end(), _value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return _value;
}

std::string kvSummary(Event const& _event, std::initializer_list<char const*> _keys)
{
    std::string out;
    for (auto const* key : _keys)
    {
        auto it = _event.kv.find(key);
        if (it != _event.kv.end())
        {
            out += (out.empty() ? "" : " ") + std::string(key) + "=" + it->second;
        }
    }
    return out;
}

std::string delta(int64_t _now, int64_t _prev)
{
    return _prev == 0 ? "" : "+" + std::to_string(_now - _prev) + "ms";
}
}  // namespace

Json::Value TraceResult::toJson() const
{
    Json::Value root;
    root["header"] = Json::Value(Json::arrayValue);
    for (auto const& column : header)
    {
        root["header"].append(column);
    }
    root["rows"] = Json::Value(Json::arrayValue);
    for (auto const& row : rows)
    {
        Json::Value item;
        for (size_t i = 0; i < row.size() && i < header.size(); ++i)
        {
            item[header[i]] = row[i];
        }
        root["rows"].append(item);
    }
    root["missing"] = missing;
    return root;
}

bool hashMatches(std::string const& _logValue, std::string const& _query)
{
    auto logHex = stripHex(_logValue);
    auto query = stripHex(_query);
    if (logHex.empty() || query.empty())
    {
        return false;
    }
    return query.starts_with(logHex) || logHex.starts_with(query);
}

std::string formatTime(int64_t _ms)
{
    std::time_t seconds = static_cast<std::time_t>(_ms / 1000);
    std::tm tm{};
    localtime_r(&seconds, &tm);
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec,
        static_cast<int>(_ms % 1000));
    return buffer;
}

TraceResult traceViewChange(std::vector<Event> const& _events, size_t _last)
{
    TraceResult result;
    result.header = {"time", "fromView", "toView", "reason", "waitingIndex", "waitedMs", "quorum",
        "newLeader", "reachedAt"};
    struct Round
    {
        Event const* triggered = nullptr;
        Event const* quorum = nullptr;
        Event const* reached = nullptr;
        int64_t firstMs = 0;
    };
    std::map<int64_t, Round> rounds;
    for (auto const& event : _events)
    {
        std::optional<int64_t> toView;
        if (event.name == events::ViewChangeTriggered || event.name == events::ViewChangeSent ||
            event.name == events::ViewChangeReceived || event.name == events::ViewChangeQuorum)
        {
            toView = event.getInt("toView");
        }
        else if (event.name == events::NewViewReached)
        {
            toView = event.getInt("view");
        }
        if (!toView)
        {
            continue;
        }
        auto& round = rounds[*toView];
        if (round.firstMs == 0)
        {
            round.firstMs = event.timeMs;
        }
        if (event.name == events::ViewChangeTriggered && !round.triggered)
        {
            round.triggered = &event;
        }
        else if (event.name == events::ViewChangeQuorum)
        {
            round.quorum = &event;
        }
        else if (event.name == events::NewViewReached)
        {
            round.reached = &event;
        }
    }
    std::vector<std::vector<std::string>> rows;
    for (auto const& [toView, round] : rounds)
    {
        auto const* anchor =
            round.triggered ? round.triggered : (round.reached ? round.reached : round.quorum);
        rows.push_back({formatTime(round.firstMs),
            round.triggered ?
                round.triggered->get("view") :
                (round.reached ? std::to_string(*round.reached->getInt("view") - 1) : "-"),
            std::to_string(toView), round.triggered ? round.triggered->get("reason") : "(remote)",
            round.triggered ? round.triggered->get("waitingIndex", "-") : "-",
            round.triggered ? round.triggered->get("waitedMs", "-") : "-",
            round.quorum ?
                round.quorum->get("weight") + "/" + round.quorum->get("minRequiredQuorum") :
                "-",
            round.reached ? round.reached->get("leaderIdx", "-") : "-",
            round.reached ? formatTime(round.reached->timeMs) : "-"});
        (void)anchor;
    }
    if (rows.size() > _last)
    {
        rows.erase(rows.begin(), rows.begin() + static_cast<std::ptrdiff_t>(rows.size() - _last));
    }
    result.rows = std::move(rows);
    if (result.rows.empty())
    {
        result.missing = "no viewchange events in the window";
    }
    return result;
}

TraceResult tracePbft(std::vector<Event> const& _events, int64_t _number)
{
    TraceResult result;
    result.header = {"time", "event", "keys", "delta"};
    int64_t prev = 0;
    auto index = std::to_string(_number);
    // the handbook's PBFT round, in its real order; other [CONSENSUS] lines with an index= key
    // (notifications, cache bookkeeping) are not part of the thread
    static std::vector<std::string_view> const c_round = {events::PrePrepareSent,
        events::PrePrepareReceived, events::PrePrepareRejected, events::PrepareQuorum,
        events::CommitQuorum, events::ProposalExecuted, events::ProposalExecuteFailed,
        events::CheckpointSent, events::CheckpointQuorum, events::CheckpointResend,
        events::BlockCommitted, events::Report};
    for (auto const& event : _events)
    {
        if (!event.hasBadge(events::BadgeConsensus) ||
            std::find(c_round.begin(), c_round.end(), event.name) == c_round.end())
        {
            continue;
        }
        bool matches = (event.name == events::Report && event.get("committedIndex") == index) ||
                       (event.name != events::Report && event.get("index") == index);
        if (!matches)
        {
            continue;
        }
        result.rows.push_back({formatTime(event.timeMs), event.name,
            kvSummary(event, {"fromIdx", "view", "weight", "execMs", "txs", "commitMs", "roundMs",
                                 "reason", "hash"}),
            delta(event.timeMs, prev)});
        prev = event.timeMs;
    }
    if (result.rows.empty())
    {
        result.missing = "no PBFT events for index " + index + " in the window";
    }
    return result;
}

TraceResult traceTx(std::vector<Event> const& _events, std::string const& _hash)
{
    TraceResult result;
    result.header = {"time", "event", "keys"};
    static std::vector<std::string_view> const c_stages = {events::TxAdmitted, events::TxRejected,
        events::TxSealed, events::TxSealSkipped, events::TxExecuted, events::TxRemoved};
    for (auto const& event : _events)
    {
        if (std::find(c_stages.begin(), c_stages.end(), event.name) == c_stages.end())
        {
            continue;
        }
        if (!hashMatches(event.get("tx"), _hash))
        {
            continue;
        }
        result.rows.push_back({formatTime(event.timeMs), event.name,
            kvSummary(event, {"reason", "from", "nonce", "blockLimit", "batchId", "number",
                                 "status", "gasUsed"})});
    }
    if (result.rows.empty())
    {
        result.missing =
            "not found in log (per-tx events are DEBUG; run: fisco-bcos log-level set --module "
            "TXPOOL debug, "
            "then resend)";
    }
    return result;
}

TraceResult traceSync(std::vector<Event> const& _events)
{
    TraceResult result;
    result.header = {"startedAt", "from", "to", "blocks", "costMs", "reason"};
    Event const* started = nullptr;
    for (auto const& event : _events)
    {
        if (event.name == events::SyncStarted)
        {
            started = &event;
        }
        else if (event.name == events::SyncFinished)
        {
            result.rows.push_back({started ? formatTime(started->timeMs) : "-",
                started ? started->get("number", "-") : "-", event.get("number", "-"),
                event.get("blocks", "-"), event.get("costMs", "-"), event.get("reason", "-")});
            started = nullptr;
        }
    }
    if (started)
    {
        result.rows.push_back({formatTime(started->timeMs), started->get("number", "-"),
            "(still syncing, highest " + started->get("highest", "-") + ")", "-", "-", "-"});
    }
    if (result.rows.empty())
    {
        result.missing = "no sync activity in the window";
    }
    return result;
}

TraceResult traceSealStall(std::vector<Event> const& _events)
{
    TraceResult result;
    result.header = {"from", "to", "durationMs", "reason", "index", "until"};
    Event const* skipped = nullptr;
    for (auto const& event : _events)
    {
        if (event.name == events::SealSkipped)
        {
            if (skipped)
            {
                result.rows.push_back({formatTime(skipped->timeMs), formatTime(event.timeMs),
                    std::to_string(event.timeMs - skipped->timeMs), skipped->get("reason"),
                    skipped->get("index", "-"), skipped->get("until", "-")});
            }
            skipped = &event;
        }
        else if (event.name == events::SealResumed && skipped)
        {
            result.rows.push_back({formatTime(skipped->timeMs), formatTime(event.timeMs),
                std::to_string(event.timeMs - skipped->timeMs), skipped->get("reason"),
                skipped->get("index", "-"), skipped->get("until", "-")});
            skipped = nullptr;
        }
    }
    if (skipped)
    {
        result.rows.push_back({formatTime(skipped->timeMs), "(ongoing)", "-",
            skipped->get("reason"), skipped->get("index", "-"), skipped->get("until", "-")});
    }
    if (result.rows.empty())
    {
        result.missing = "no SealSkipped events in the window";
    }
    return result;
}

TraceResult traceP2p(std::vector<Event> const& _events)
{
    TraceResult result;
    result.header = {"peer", "endpoint", "state", "lastChange", "reason"};
    struct PeerState
    {
        std::string endpoint, state, reason;
        int64_t ms = 0;
    };
    std::map<std::string, PeerState> peers;
    for (auto const& event : _events)
    {
        if (event.name == events::PeerConnected || event.name == events::PeerDisconnected)
        {
            auto& state = peers[event.get("peer", event.get("endpoint"))];
            state.endpoint = event.get("endpoint");
            state.state = event.name == events::PeerConnected ? "connected" : "disconnected";
            state.reason =
                event.name == events::PeerConnected ? event.get("direction") : event.get("reason");
            state.ms = event.timeMs;
        }
        else if (event.name == events::HandshakeFailed || event.name == events::PeerConnectFailed)
        {
            auto& state = peers[event.get("endpoint")];
            state.endpoint = event.get("endpoint");
            state.state =
                event.name == events::HandshakeFailed ? "handshake_failed" : "connect_failed";
            state.reason = event.get("reason");
            state.ms = event.timeMs;
        }
    }
    for (auto const& [peer, state] : peers)
    {
        result.rows.push_back(
            {peer, state.endpoint, state.state, formatTime(state.ms), state.reason});
    }
    if (result.rows.empty())
    {
        result.missing = "no P2P connection events in the window";
    }
    return result;
}

TraceResult traceTxSync(std::vector<Event> const& _events)
{
    TraceResult result;
    result.header = {"time", "number", "missed", "total", "peer", "requested", "outcome", "costMs"};
    struct Proposal
    {
        int64_t ms = 0;
        std::string missed, total, peer, requested, outcome, costMs;
    };
    std::map<std::string, Proposal> proposals;
    for (auto const& event : _events)
    {
        auto number = event.get("number");
        if (number.empty())
        {
            continue;
        }
        if (event.name == events::ProposalTxsMissing)
        {
            auto& p = proposals[number];
            p.ms = event.timeMs;
            p.missed = event.get("missed");
            p.total = event.get("total");
        }
        else if (event.name == events::TxsRequested)
        {
            auto& p = proposals[number];
            if (p.ms == 0)
            {
                p.ms = event.timeMs;
            }
            p.peer = event.get("peer");
            p.requested = event.get("count");
        }
        else if (event.name == events::TxsReceived)
        {
            auto& p = proposals[number];
            p.outcome = "received " + event.get("count");
            p.costMs = event.get("costMs");
        }
        else if (event.name == events::TxsRequestFailed)
        {
            auto& p = proposals[number];
            p.outcome = "failed: " + event.get("msg", event.get("code"));
        }
    }
    for (auto const& [number, p] : proposals)
    {
        result.rows.push_back({formatTime(p.ms), number, p.missed.empty() ? "-" : p.missed,
            p.total.empty() ? "-" : p.total, p.peer.empty() ? "-" : p.peer,
            p.requested.empty() ? "-" : p.requested, p.outcome.empty() ? "pending" : p.outcome,
            p.costMs.empty() ? "-" : p.costMs});
    }
    if (result.rows.empty())
    {
        result.missing = "no transaction sync events in the window";
    }
    return result;
}
}  // namespace bcos::ops
