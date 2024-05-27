/*
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @brief The executor of a contract
 * @file DmcExecutor.h
 * @author: jimmyshi
 * @date: 2022-03-23
 */


#pragma once

#include "DmcExecutor.h"

namespace bcos::scheduler
{
class ShardingDmcExecutor : public DmcExecutor
{
public:
    ShardingDmcExecutor(std::string name, std::string contractAddress,
        bcos::protocol::Block::Ptr block,
        bcos::executor::ParallelTransactionExecutorInterface::Ptr executor,
        GraphKeyLocks::Ptr keyLocks, bcos::crypto::Hash::Ptr hashImpl,
        DmcStepRecorder::Ptr dmcRecorder, int64_t schedulerTermId, bool isCall)
      : DmcExecutor(std::move(name), std::move(contractAddress), block, executor, keyLocks,
            hashImpl, dmcRecorder, isCall),
        m_schedulerTermId(schedulerTermId)
    {}

    ~ShardingDmcExecutor() override = default;

    void submit(protocol::ExecutionMessage::UniquePtr message, bool withDAG) override;

    void shardGo(std::function<void(bcos::Error::UniquePtr, Status)> callback);

    void executorCall(bcos::protocol::ExecutionMessage::UniquePtr input,
        std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
            callback) override;

    void executorExecuteTransactions(std::string contractAddress,
        gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
        // called every time at all tx stop( pause or finish)
        std::function<void(
            bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
            callback) override;

    void preExecute() override;

private:
    void handleShardGoOutput(std::vector<bcos::protocol::ExecutionMessage::UniquePtr> outputs);
    void handleExecutiveOutputs(
        std::vector<bcos::protocol::ExecutionMessage::UniquePtr> outputs) override;

    std::shared_ptr<std::vector<protocol::ExecutionMessage::UniquePtr>> m_preparedMessages =
        std::make_shared<std::vector<protocol::ExecutionMessage::UniquePtr>>();
    int64_t m_schedulerTermId;
    mutable bcos::SharedMutex x_preExecute;
};
}  // namespace bcos::scheduler
