/*
 *  Copyright (C) 2023 FISCO BCOS.
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
 * @brief ShardingExecutor
 * @file ShardingExecutor.h
 * @author: JimmyShi22
 * @date: 2023-01-07
 */
#pragma once
#include "../dag/TxDAGFlow.h"
#include "TransactionExecutor.h"
namespace bcos::executor
{

class ShardingTransactionExecutor : public TransactionExecutor
{
public:
    using Ptr = std::shared_ptr<ShardingTransactionExecutor>;
    ShardingTransactionExecutor(bcos::ledger::LedgerInterface::Ptr ledger,
        txpool::TxPoolInterface::Ptr txpool, storage::MergeableStorageInterface::Ptr cachedStorage,
        storage::TransactionalStorageInterface::Ptr backendStorage,
        protocol::ExecutionMessageFactory::Ptr executionMessageFactory,
        storage::StateStorageFactory::Ptr stateStorageFactory, bcos::crypto::Hash::Ptr hashImpl,
        bool isWasm, bool isAuthCheck, std::shared_ptr<VMFactory> vmFactory,
        std::shared_ptr<std::set<std::string, std::less<>>> keyPageIgnoreTables, std::string name)
      : TransactionExecutor(ledger, txpool, cachedStorage, backendStorage, executionMessageFactory,
            stateStorageFactory, hashImpl, isWasm, isAuthCheck, vmFactory, keyPageIgnoreTables,
            name){};

    ~ShardingTransactionExecutor() override = default;

    void executeTransactions(std::string contractAddress,
        gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
        std::function<void(
            bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
            callback) override;

    void preExecuteTransactions(int64_t schedulerTermId,
        const bcos::protocol::BlockHeader::ConstPtr& blockHeader, std::string contractAddress,
        gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
        std::function<void(bcos::Error::UniquePtr)> callback) override;


    std::shared_ptr<ExecutiveFlowInterface> getExecutiveFlow(
        std::shared_ptr<BlockContext> blockContext, std::string codeAddress, bool useCoroutine,
        bool isStaticCall = false) override;

private:
    BlockContext::Ptr createTmpBlockContext(const protocol::BlockHeader::ConstPtr& currentHeader);

private:
    class PreExeCache
    {
    public:
        using Ptr = std::shared_ptr<PreExeCache>;

        std::shared_ptr<std::vector<CallParameters::UniquePtr>> inputs;
        TxDAGFlow::Ptr dagFlow;
        mutable bcos::SharedMutex x_cache;
    };

    // <blockNumber, timestamp> -> TxDAGFlow
    std::map<std::tuple<bcos::protocol::BlockNumber, int64_t, const std::string>, PreExeCache::Ptr>
        m_preparedCache;
    mutable bcos::SharedMutex x_preparedCache;
};


}  // namespace bcos::executor
