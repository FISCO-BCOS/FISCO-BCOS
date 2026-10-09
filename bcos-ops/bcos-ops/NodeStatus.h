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
 * @brief the status snapshot. Same schema for the rpc / attach / log sources; a field the source
 *        cannot fill stays nullopt and `reasons[field]` says why.
 * @file NodeStatus.h
 */
#pragma once

#include <json/json.h>
#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace bcos::ops
{
struct NodeStatus
{
    // identity
    std::optional<std::string> nodeId;
    std::optional<std::string> version;
    std::optional<std::string> chainId;
    std::optional<std::string> groupId;
    std::optional<bool> isConsensusNode;
    std::optional<bool> smCrypto;
    std::optional<bool> authCheck;
    // chain
    std::optional<int64_t> blockNumber;
    std::optional<std::string> latestHash;
    std::optional<int64_t> latestTimestamp;  // ms
    // consensus
    std::optional<int64_t> view;
    std::optional<int64_t> leaderIndex;
    std::optional<int64_t> changeCycle;
    std::optional<bool> inTimeout;
    std::optional<int64_t> consensusNodesNum;
    std::optional<int64_t> connectedConsensusNodes;  // excludes the node itself
    std::optional<int64_t> minRequiredQuorum;
    std::optional<int64_t> consensusTimeoutMs;
    // sync
    std::optional<bool> isSyncing;
    std::optional<int64_t> knownHighestNumber;
    std::optional<int64_t> lag;
    std::optional<int64_t> peerCount;
    // txpool
    std::optional<int64_t> pendingTxSize;
    std::optional<int64_t> txpoolLimit;
    // meta
    std::string source;
    int64_t collectedAt = 0;  // ms since epoch
    std::map<std::string, std::string> reasons;

    void missing(std::string _field, std::string _reason)
    {
        reasons[std::move(_field)] = std::move(_reason);
    }

    Json::Value toJson() const;
};
}  // namespace bcos::ops
