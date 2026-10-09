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
 * @file NodeStatus.cpp
 */
#include "NodeStatus.h"

namespace bcos::ops
{
namespace
{
template <typename T>
Json::Value opt(std::optional<T> const& _value)
{
    if (!_value)
    {
        return Json::Value(Json::nullValue);
    }
    if constexpr (std::is_same_v<T, int64_t>)
    {
        return Json::Value(static_cast<Json::Int64>(*_value));
    }
    else
    {
        return Json::Value(*_value);
    }
}
}  // namespace

Json::Value NodeStatus::toJson() const
{
    Json::Value root;
    Json::Value& identity = root["identity"];
    identity["nodeID"] = opt(nodeId);
    identity["version"] = opt(version);
    identity["chainId"] = opt(chainId);
    identity["groupId"] = opt(groupId);
    identity["isConsensusNode"] = opt(isConsensusNode);
    identity["smCrypto"] = opt(smCrypto);
    identity["authCheck"] = opt(authCheck);

    Json::Value& chain = root["chain"];
    chain["blockNumber"] = opt(blockNumber);
    chain["latestHash"] = opt(latestHash);
    chain["latestTimestamp"] = opt(latestTimestamp);

    Json::Value& consensus = root["consensus"];
    consensus["view"] = opt(view);
    consensus["leaderIndex"] = opt(leaderIndex);
    consensus["changeCycle"] = opt(changeCycle);
    consensus["inTimeout"] = opt(inTimeout);
    consensus["consensusNodesNum"] = opt(consensusNodesNum);
    consensus["connectedGroupNodes"] = opt(connectedGroupNodes);
    consensus["minRequiredQuorum"] = opt(minRequiredQuorum);
    consensus["consensusTimeoutMs"] = opt(consensusTimeoutMs);

    Json::Value& sync = root["sync"];
    sync["isSyncing"] = opt(isSyncing);
    sync["knownHighestNumber"] = opt(knownHighestNumber);
    sync["lag"] = opt(lag);
    sync["peerCount"] = opt(peerCount);

    Json::Value& txpool = root["txpool"];
    txpool["pendingTxSize"] = opt(pendingTxSize);
    txpool["limit"] = opt(txpoolLimit);

    root["source"] = source;
    root["collectedAt"] = static_cast<Json::Int64>(collectedAt);
    Json::Value& reasonsJson = root["reasons"];
    reasonsJson = Json::Value(Json::objectValue);
    for (auto const& [field, reason] : reasons)
    {
        reasonsJson[field] = reason;
    }
    return root;
}
}  // namespace bcos::ops
