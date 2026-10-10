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
 * @file RpcCollector.cpp
 */
#include "RpcCollector.h"
#include "GroupFacts.h"
#include "bcos-ops/OpsError.h"
#include <chrono>

namespace bcos::ops
{
namespace
{
Json::Value params(std::string const& _group)
{
    Json::Value value(Json::arrayValue);
    value.append(_group);
    return value;
}

Json::Value params(std::string const& _group, std::string const& _node)
{
    auto value = params(_group);
    value.append(_node);
    return value;
}

/// runs one RPC; on failure records the reason under every field it would have filled
template <typename Fill>
void tryCall(NodeStatus& _status, RpcCall const& _call, std::string_view _method,
    Json::Value const& _params, std::initializer_list<char const*> _fields, Fill&& _fill)
{
    try
    {
        _fill(_call(_method, _params));
    }
    catch (std::exception const& e)
    {
        for (auto const* field : _fields)
        {
            _status.missing(field, std::string(_method) + ": " + e.what());
        }
    }
}

int64_t asInt64(Json::Value const& _value)
{
    if (_value.isString())
    {
        return std::stoll(_value.asString());
    }
    return _value.asInt64();
}
}  // namespace

std::string discoverGroup(RpcCall const& _call)
{
    auto result = _call("getGroupList", Json::Value(Json::arrayValue));
    auto const& list = result["groupList"];
    if (!list.isArray() || list.empty())
    {
        throw OpsError(c_exitUsage, "getGroupList returned no group");
    }
    return list[0].asString();
}

NodeStatus collectFromRpc(RpcCall const& _call, std::string const& _group,
    std::string const& _source, LocalFallbacks const& _fallbacks)
{
    NodeStatus status;
    status.source = _source;
    status.collectedAt = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch())
                             .count();
    status.groupId = _group;
    std::string const node;  // empty: the node that serves the connection

    tryCall(status, _call, "getConsensusStatus", params(_group, node),
        {"nodeID", "isConsensusNode", "view", "leaderIndex", "changeCycle", "inTimeout",
            "consensusNodesNum", "connectedGroupNodes", "minRequiredQuorum"},
        [&](Json::Value const& v) {
            status.nodeId = v["nodeID"].asString();
            status.isConsensusNode = v["isConsensusNode"].asBool();
            status.view = asInt64(v["view"]);
            status.leaderIndex = asInt64(v["leaderIndex"]);
            status.changeCycle = asInt64(v["changeCycle"]);
            status.inTimeout = v["timeout"].asBool();
            status.consensusNodesNum = asInt64(v["consensusNodesNum"]);
            status.connectedGroupNodes = asInt64(v["connectedNodeList"]);
            status.minRequiredQuorum = asInt64(v["minRequiredQuorum"]);
            if (v.isMember("consensusTimeout"))  // 3.18+: the node reports its own period
            {
                status.consensusTimeoutMs = asInt64(v["consensusTimeout"]);
            }
        });

    try
    {
        auto facts = groupFacts(_call, _group, status.nodeId);
        status.chainId = facts.chainId;
        status.version = facts.version;
        status.smCrypto = facts.smCrypto;
        status.authCheck = facts.authCheck;
    }
    catch (std::exception const& e)
    {
        for (auto const* field : {"version", "chainId", "smCrypto", "authCheck"})
        {
            status.missing(field, std::string("getGroupInfo: ") + e.what());
        }
    }

    tryCall(status, _call, "getBlockNumber", params(_group, node), {"blockNumber"},
        [&](Json::Value const& v) { status.blockNumber = asInt64(v); });

    if (status.blockNumber)
    {
        auto blockParams = params(_group, node);
        blockParams.append(static_cast<Json::Int64>(*status.blockNumber));
        blockParams.append(true);
        blockParams.append(true);
        tryCall(status, _call, "getBlockByNumber", blockParams, {"latestHash", "latestTimestamp"},
            [&](Json::Value const& v) {
                status.latestHash = v["hash"].asString();
                status.latestTimestamp = asInt64(v["timestamp"]);
            });
    }
    else
    {
        status.missing("latestHash", "getBlockNumber failed");
        status.missing("latestTimestamp", "getBlockNumber failed");
    }

    tryCall(status, _call, "getSyncStatus", params(_group, node),
        {"isSyncing", "knownHighestNumber", "lag", "peerCount"}, [&](Json::Value const& v) {
            status.isSyncing = v["isSyncing"].asBool();
            status.knownHighestNumber = asInt64(v["knownHighestNumber"]);
            auto local = status.blockNumber ? *status.blockNumber : asInt64(v["blockNumber"]);
            status.lag = *status.knownHighestNumber - local;
            status.peerCount = static_cast<int64_t>(v["peers"].size());
        });

    tryCall(status, _call, "getPendingTxSize", params(_group, node), {"pendingTxSize"},
        [&](Json::Value const& v) { status.pendingTxSize = asInt64(v); });

    // consensus_timeout is a genesis value, not a system-config key, so older nodes cannot
    // answer it over RPC; the node dir supplies it locally
    if (!status.consensusTimeoutMs)
    {
        if (_fallbacks.consensusTimeoutMs)
        {
            status.consensusTimeoutMs = _fallbacks.consensusTimeoutMs;
        }
        else
        {
            status.missing("consensusTimeoutMs",
                "not in getConsensusStatus (node < 3.18) and no node dir to read config.genesis");
        }
    }

    if (_fallbacks.txpoolLimit)
    {
        status.txpoolLimit = _fallbacks.txpoolLimit;
    }
    else
    {
        status.missing("txpoolLimit", "not exposed by RPC; only known from the node dir");
    }
    return status;
}
}  // namespace bcos::ops
