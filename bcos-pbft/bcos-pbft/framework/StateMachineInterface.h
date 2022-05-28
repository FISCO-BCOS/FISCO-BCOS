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
 * @brief interface for the state machine to execute the transactions
 * @file StateMachineInterface.h
 * @author: yujiechen
 * @date 2021-05-18
 */
#pragma once
#include "ProposalInterface.h"
#include <bcos-framework//consensus/ConsensusNode.h>
namespace bcos
{
namespace consensus
{
class StateMachineInterface
{
public:
    using Ptr = std::shared_ptr<StateMachineInterface>;
    StateMachineInterface() = default;
    virtual ~StateMachineInterface() {}

    virtual void asyncApply(ssize_t _execTimeout, ProposalInterface::ConstPtr _lastAppliedProposal,
        ProposalInterface::Ptr _proposal, ProposalInterface::Ptr _executedProposal,
        std::function<void(bool)> _onExecuteFinished) = 0;
};
}  // namespace consensus
}  // namespace bcos
