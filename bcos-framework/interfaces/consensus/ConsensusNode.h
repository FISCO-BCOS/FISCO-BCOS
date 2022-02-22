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
#include "../../interfaces/consensus/ConsensusNodeInterface.h"
namespace bcos
{
namespace consensus
{
class ConsensusNode : public ConsensusNodeInterface
{
public:
    using Ptr = std::shared_ptr<ConsensusNode>;
    explicit ConsensusNode(bcos::crypto::PublicPtr _nodeID) : m_nodeID(_nodeID) {}

    ConsensusNode(bcos::crypto::PublicPtr _nodeID, uint64_t _weight)
      : m_nodeID(_nodeID), m_weight(_weight)
    {}

    ~ConsensusNode() override {}

    bcos::crypto::PublicPtr nodeID() const override { return m_nodeID; }
    uint64_t weight() const override { return m_weight; }

private:
    bcos::crypto::PublicPtr m_nodeID;
    uint64_t m_weight = 100;
};
}  // namespace consensus
}  // namespace bcos