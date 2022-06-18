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
 * @brief initializer for the dispatcher and executor
 * @file DispatcherInitializer.h
 * @author: yujiechen
 * @date 2021-06-21
 */
#pragma once
#include "ProtocolInitializer.h"
#include "bcos-framework//protocol/BlockFactory.h"
#include "bcos-protocol/TransactionSubmitResultFactoryImpl.h"
#include <bcos-framework/dispatcher/SchedulerInterface.h>
#include <bcos-framework/executor/ExecutionMessage.h>
#include <bcos-framework/executor/NativeExecutionMessage.h>
#include <bcos-framework/ledger/LedgerInterface.h>
#include <bcos-framework/storage/StorageInterface.h>
#include <bcos-scheduler/src/SchedulerFactory.h>
#include <bcos-scheduler/src/SchedulerImpl.h>
#include <bcos-tool/NodeConfig.h>

namespace bcos::initializer
{
class SchedulerInitializer
{
public:
    static bcos::scheduler::SchedulerInterface::Ptr build(
        bcos::scheduler::ExecutorManager::Ptr executorManager,
        bcos::ledger::LedgerInterface::Ptr _ledger,
        bcos::storage::TransactionalStorageInterface::Ptr storage,
        bcos::protocol::ExecutionMessageFactory::Ptr executionMessageFactory,
        bcos::protocol::BlockFactory::Ptr blockFactory, bcos::txpool::TxPoolInterface::Ptr txPool,
        bcos::protocol::TransactionSubmitResultFactory::Ptr transactionSubmitResultFactory,
        crypto::Hash::Ptr hashImpl, bool isAuthCheck, bool isWasm, int64_t schedulerSeq)
    {
        bcos::scheduler::SchedulerFactory factory(std::move(executorManager), std::move(_ledger),
            std::move(storage), executionMessageFactory, std::move(blockFactory), std::move(txPool),
            std::move(transactionSubmitResultFactory), std::move(hashImpl), isAuthCheck, isWasm);

        return factory.build(schedulerSeq);
    }

    static bcos::scheduler::SchedulerFactory::Ptr buildFactory(
        bcos::scheduler::ExecutorManager::Ptr executorManager,
        bcos::ledger::LedgerInterface::Ptr _ledger,
        bcos::storage::TransactionalStorageInterface::Ptr storage,
        bcos::protocol::ExecutionMessageFactory::Ptr executionMessageFactory,
        bcos::protocol::BlockFactory::Ptr blockFactory, bcos::txpool::TxPoolInterface::Ptr txPool,
        bcos::protocol::TransactionSubmitResultFactory::Ptr transactionSubmitResultFactory,
        crypto::Hash::Ptr hashImpl, bool isAuthCheck, bool isWasm)
    {
        return std::make_shared<bcos::scheduler::SchedulerFactory>(std::move(executorManager),
            std::move(_ledger), std::move(storage), executionMessageFactory,
            std::move(blockFactory), txPool, std::move(transactionSubmitResultFactory),
            std::move(hashImpl), isAuthCheck, isWasm);
    }
};
}  // namespace bcos::initializer