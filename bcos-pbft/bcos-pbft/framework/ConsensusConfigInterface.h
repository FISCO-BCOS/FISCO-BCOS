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
 * @brief interface for Consensus Config
 * @file ConsensusConfigInterface.h
 * @author: yujiechen
 * @date 2021-04-09
 */
#pragma once
#include "ProposalInterface.h"
#include <bcos-framework/interfaces/consensus/ConsensusNodeInterface.h>
#include <bcos-framework/interfaces/consensus/ConsensusTypeDef.h>
namespace bcos
{
namespace consensus
{
class ConsensusConfigInterface
{
public:
    using Ptr = std::shared_ptr<ConsensusConfigInterface>;
    ConsensusConfigInterface() = default;
    virtual ~ConsensusConfigInterface() {}

    // the NodeID of the consensus node
    virtual bcos::crypto::PublicPtr nodeID() const = 0;
    // the nodeIndex of this node
    virtual IndexType nodeIndex() const = 0;

    // the sealer list
    virtual ConsensusNodeList consensusNodeList() const = 0;
    virtual bcos::crypto::NodeIDs consensusNodeIDList(bool _excludeSelf = true) const = 0;
    virtual bool isConsensusNode() const = 0;

    // the consensus timeout
    virtual uint64_t consensusTimeout() const = 0;

    // the min valid quorum before agree on a round of consensus
    virtual uint64_t minRequiredQuorum() const = 0;

    virtual void setConsensusNodeList(ConsensusNodeList& _sealerList) = 0;
    virtual void setConsensusTimeout(uint64_t _consensusTimeout) = 0;

    virtual void setCommittedProposal(ProposalInterface::Ptr _committedProposal) = 0;
    virtual ProposalInterface::ConstPtr committedProposal() = 0;
};
}  // namespace consensus
}  // namespace bcos