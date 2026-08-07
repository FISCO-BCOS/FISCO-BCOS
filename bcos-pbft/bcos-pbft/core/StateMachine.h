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
 * @brief state machine to execute the transactions
 * @file StateMachine.h
 * @author: yujiechen
 * @date 2021-05-18
 */
#pragma once
#include "bcos-framework/consensus/StateMachineInterface.h"
#include <bcos-framework/dispatcher/SchedulerInterface.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-utilities/IOServicePool.h>

#include <utility>
namespace bcos
{
namespace consensus
{
/// OP 模式短路回调错误码：区别于 invalid proposal 的 -1。
constexpr int64_t c_opModeExecutionDisabled = -2;

class StateMachine : public StateMachineInterface, public std::enable_shared_from_this<StateMachine>
{
public:
    StateMachine(bcos::scheduler::SchedulerInterface::Ptr _scheduler,
        bcos::protocol::BlockFactory::Ptr _blockFactory,
        bcos::IOServicePool::Ptr _ioServicePool, bool opStackMode = false)
      : m_scheduler(std::move(_scheduler)), m_blockFactory(std::move(_blockFactory)),
        m_strand(std::move(_ioServicePool)), m_opStackMode(opStackMode)
    {}

    void asyncApply(ssize_t _execTimeout, ProposalInterface::ConstPtr _lastAppliedProposal,
        ProposalInterface::Ptr _proposal, ProposalInterface::Ptr _executedProposal,
        std::function<void(int64_t)> _onExecuteFinished) override;

    void asyncPreApply(
        ProposalInterface::Ptr _proposal, std::function<void(bool)> _onPreApplyFinished) override;

private:
    void apply(ssize_t _execTimeout, ProposalInterface::ConstPtr _lastAppliedProposal,
        ProposalInterface::Ptr _proposal, ProposalInterface::Ptr _executedProposal,
        std::function<void(int64_t)> _onExecuteFinished);

    void preApply(ProposalInterface::Ptr _proposal, std::function<void(bool)> _onPreApplyFinished);

protected:
    bcos::scheduler::SchedulerInterface::Ptr m_scheduler;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    bcos::Strand m_strand;
    /// OP 模式（executor_version>=3）：PBFT 不得执行区块（Engine API executeOpBlock 是唯一驱动）。
    bool m_opStackMode = false;
};
}  // namespace consensus
}  // namespace bcos