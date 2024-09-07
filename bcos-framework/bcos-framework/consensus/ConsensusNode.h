/**
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief the information of the consensus node
 * @file ConsensusNode.h
 * @author: yujiechen
 * @date 2021-04-09
 */
#pragma once
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-utilities/ThreeWay4Apple.h"
#include <bcos-crypto/interfaces/crypto/KeyInterface.h>
#include <bcos-framework/Common.h>
#include <compare>

namespace bcos::consensus
{

constexpr static uint64_t defaultVoteWeight = 100;
constexpr static uint64_t defaultTermWeight = 1;

enum class Type
{
    consensus_sealer,
    consensus_observer,
    consensus_candidate_sealer
};

struct ConsensusNode
{
    bcos::crypto::PublicPtr nodeID;
    Type type;
    uint64_t voteWeight;
    uint64_t termWeight;
    protocol::BlockNumber enableNumber;
};

inline std::strong_ordering operator<=>(ConsensusNode const& lhs, ConsensusNode const& rhs)
{
    if (auto cmp = lhs.nodeID->data() <=> rhs.nodeID->data(); !std::is_eq(cmp))
    {
        return cmp;
    }
    if (auto cmp = lhs.type <=> rhs.type; !std::is_eq(cmp))
    {
        return cmp;
    }
    if (auto cmp = lhs.voteWeight <=> rhs.voteWeight; !std::is_eq(cmp))
    {
        return cmp;
    }
    if (auto cmp = lhs.termWeight <=> rhs.termWeight; !std::is_eq(cmp))
    {
        return cmp;
    }
    return lhs.enableNumber <=> rhs.enableNumber;
}

using ConsensusNodeList = std::vector<ConsensusNode>;
using ConsensusNodeSet = std::set<ConsensusNode>;
inline std::string decsConsensusNodeList(ConsensusNodeList const& _nodeList)
{
    std::ostringstream stringstream;
    for (const auto& node : _nodeList)
    {
        stringstream << LOG_KV(node.nodeID->shortHex(), std::to_string(node.voteWeight));
    }
    return stringstream.str();
}
}  // namespace bcos::consensus
