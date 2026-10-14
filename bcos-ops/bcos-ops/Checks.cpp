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
 * @file Checks.cpp
 */
#include "Checks.h"

namespace bcos::ops
{
namespace
{
Check skipped(std::string _name, std::string _why)
{
    return Check{std::move(_name), "skipped", std::move(_why)};
}
Check verdict(std::string _name, bool _fail, std::string _detail)
{
    return Check{std::move(_name), _fail ? "fail" : "ok", std::move(_detail)};
}
}  // namespace

std::vector<Check> evaluate(
    NodeStatus const& _status, Thresholds const& _thresholds, int64_t _nowMs)
{
    std::vector<Check> checks;

    // 1. height_advancing
    if (!_status.latestTimestamp || !_status.consensusTimeoutMs || !_status.pendingTxSize)
    {
        checks.push_back(skipped(
            "height_advancing", "needs latestTimestamp, consensusTimeoutMs and pendingTxSize"));
    }
    else
    {
        auto ageMs = _nowMs - *_status.latestTimestamp;
        auto limitMs = static_cast<int64_t>(_thresholds.stallFactor * *_status.consensusTimeoutMs);
        bool fail = ageMs > limitMs && *_status.pendingTxSize > 0;
        auto check = verdict("height_advancing", fail,
            fail ? "pending txs but no block for " + std::to_string(ageMs) + "ms" : "");
        check.values["blockAgeMs"] = static_cast<Json::Int64>(ageMs);
        check.values["limitMs"] = static_cast<Json::Int64>(limitMs);
        check.values["pendingTxSize"] = static_cast<Json::Int64>(*_status.pendingTxSize);
        checks.push_back(std::move(check));
    }

    // 2. consensus_stable
    if (!_status.inTimeout)
    {
        checks.push_back(skipped("consensus_stable", "inTimeout unknown"));
    }
    else
    {
        auto check = verdict("consensus_stable", *_status.inTimeout,
            *_status.inTimeout ? "view change in progress" : "");
        check.values["inTimeout"] = *_status.inTimeout;
        if (_status.view)
        {
            check.values["view"] = static_cast<Json::Int64>(*_status.view);
        }
        if (_status.changeCycle)
        {
            check.values["changeCycle"] = static_cast<Json::Int64>(*_status.changeCycle);
        }
        checks.push_back(std::move(check));
    }

    // 3. sync_caught_up
    if (!_status.lag || !_status.isSyncing)
    {
        checks.push_back(skipped("sync_caught_up", "lag or isSyncing unknown"));
    }
    else
    {
        bool fail = *_status.lag >= _thresholds.maxLag && *_status.isSyncing;
        auto check = verdict("sync_caught_up", fail,
            fail ? "syncing and " + std::to_string(*_status.lag) + " blocks behind" : "");
        check.values["lag"] = static_cast<Json::Int64>(*_status.lag);
        check.values["maxLag"] = static_cast<Json::Int64>(_thresholds.maxLag);
        check.values["isSyncing"] = *_status.isSyncing;
        checks.push_back(std::move(check));
    }

    // 4. quorum_connected
    if (_status.isConsensusNode && !*_status.isConsensusNode)
    {
        checks.push_back(skipped("quorum_connected", "not a consensus node"));
    }
    else if (!_status.connectedGroupNodes || !_status.minRequiredQuorum)
    {
        checks.push_back(skipped("quorum_connected", "connected nodes or quorum unknown"));
    }
    else
    {
        // connectedNodeList counts this node and any observer, so this leans towards ok
        auto reachable = *_status.connectedGroupNodes;
        bool fail = reachable < *_status.minRequiredQuorum;
        auto check = verdict("quorum_connected", fail,
            fail ?
                "only " + std::to_string(reachable) + " group nodes connected (self included) of " +
                    std::to_string(*_status.minRequiredQuorum) + " required" :
                "");
        check.values["reachable"] = static_cast<Json::Int64>(reachable);
        check.values["minRequiredQuorum"] = static_cast<Json::Int64>(*_status.minRequiredQuorum);
        checks.push_back(std::move(check));
    }

    // 5. txpool_not_full
    if (!_status.pendingTxSize || !_status.txpoolLimit)
    {
        checks.push_back(skipped("txpool_not_full", "pendingTxSize or txpool limit unknown"));
    }
    else
    {
        bool fail = *_status.pendingTxSize >= *_status.txpoolLimit;
        auto check = verdict("txpool_not_full", fail, fail ? "txpool at limit" : "");
        check.values["pendingTxSize"] = static_cast<Json::Int64>(*_status.pendingTxSize);
        check.values["limit"] = static_cast<Json::Int64>(*_status.txpoolLimit);
        checks.push_back(std::move(check));
    }
    return checks;
}

bool anyFailed(std::vector<Check> const& _checks)
{
    for (auto const& check : _checks)
    {
        if (check.failed())
        {
            return true;
        }
    }
    return false;
}

Json::Value checksToJson(std::vector<Check> const& _checks)
{
    Json::Value array(Json::arrayValue);
    for (auto const& check : _checks)
    {
        Json::Value item;
        item["name"] = check.name;
        item["state"] = check.state;
        item["ok"] = check.ok();
        if (!check.detail.empty())
        {
            item["detail"] = check.detail;
        }
        for (auto const& key : check.values.getMemberNames())
        {
            item[key] = check.values[key];
        }
        array.append(item);
    }
    return array;
}
}  // namespace bcos::ops
