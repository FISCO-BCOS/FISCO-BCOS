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
 * @brief the information of the consensus node with weight
 * @file ConsensusNode.h
 * @author: yujiechen
 * @date 2021-04-12
 */
#pragma once
#include "ConsensusNodeInterface.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include <utility>

namespace bcos::consensus
{
class ConsensusNode : public ConsensusNodeInterface
{
public:
    using Ptr = std::shared_ptr<ConsensusNode>;
    ConsensusNode(const ConsensusNode&) = default;
    ConsensusNode(ConsensusNode&&) = default;
    ConsensusNode& operator=(const ConsensusNode&) = default;
    ConsensusNode& operator=(ConsensusNode&&) = default;
    explicit ConsensusNode(bcos::crypto::PublicPtr _nodeID) : m_nodeID(std::move(_nodeID)) {}

    ConsensusNode(
        bcos::crypto::PublicPtr nodeID, Type type, uint64_t voteWeight, uint64_t termWeight)
      : m_nodeID(std::move(nodeID)),
        m_type(type),
        m_voteWeight(voteWeight),
        m_termWeight(termWeight)
    {}

    ~ConsensusNode() override = default;
    bcos::crypto::PublicPtr nodeID() const override { return m_nodeID; }
    Type type() const override { return m_type; }
    uint64_t voteWeight() const override { return m_voteWeight; }
    uint64_t termWeight() const override { return m_termWeight; }
    protocol::BlockNumber enableNumber() const override { return m_blockNumber; }

    bcos::crypto::PublicPtr m_nodeID;
    Type m_type{};
    uint64_t m_voteWeight = defaultVoteWeight;
    uint64_t m_termWeight = defaultTermWeight;
    protocol::BlockNumber m_blockNumber = 0;
};
}  // namespace bcos::consensus
