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
 * @brief The block executive(context) for serial transaction execution
 * @file SerialDmcBlockExecutive.h
 * @author: jimmyshi
 * @date: 2022-07-12
 */


#pragma once
#include "BlockExecutive.h"

#define SERIAL_EXECUTE_LOG(LEVEL) SCHEDULER_LOG(LEVEL) << LOG_BADGE("serialExecute")

namespace bcos::scheduler
{
static const std::string SERIAL_EXECUTOR_NAME = std::string("serial_executor");

class SerialDmcBlockExecutive : public BlockExecutive
{
public:
    SerialDmcBlockExecutive(bcos::protocol::Block::Ptr block, SchedulerImpl* scheduler,
        size_t startContextID,
        bcos::protocol::TransactionSubmitResultFactory::Ptr transactionSubmitResultFactory,
        bool staticCall, bcos::protocol::BlockFactory::Ptr _blockFactory,
        bcos::txpool::TxPoolInterface::Ptr _txPool)
      : BlockExecutive(block, scheduler, startContextID, transactionSubmitResultFactory, staticCall,
            _blockFactory, _txPool){};

    SerialDmcBlockExecutive(bcos::protocol::Block::Ptr block, SchedulerImpl* scheduler,
        size_t startContextID,
        bcos::protocol::TransactionSubmitResultFactory::Ptr transactionSubmitResultFactory,
        bool staticCall, bcos::protocol::BlockFactory::Ptr _blockFactory,
        bcos::txpool::TxPoolInterface::Ptr _txPool, uint64_t _gasLimit, bool _syncBlock)
      : BlockExecutive(block, scheduler, startContextID, transactionSubmitResultFactory, staticCall,
            _blockFactory, _txPool, _gasLimit, _syncBlock)
    {}
    virtual ~SerialDmcBlockExecutive(){};


    void prepare() override;
    void asyncExecute(
        std::function<void(Error::UniquePtr, protocol::BlockHeader::Ptr, bool)> callback) override;

private:
    void serialExecute(
        std::function<void(Error::UniquePtr, protocol::BlockHeader::Ptr, bool)> callback);

    void recursiveExecuteTx(
        ContextID contextID, std::function<void(bcos::Error::UniquePtr)> callback);

    void onExecuteFinish(
        std::function<void(Error::UniquePtr, protocol::BlockHeader::Ptr, bool)> callback) override;

    std::vector<ExecutiveState::Ptr> m_serialExecutiveStates;
    bcos::executor::ParallelTransactionExecutorInterface::Ptr m_executor;
    bcos::scheduler::ExecutorManager::ExecutorInfo::Ptr m_executorInfo;
};
}  // namespace bcos::scheduler
