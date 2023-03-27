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
 * @brief The block executive(context) for sharding transaction execution
 * @file ShardingBlockExecutive.h
 * @author: jimmyshi
 * @date: 2022-01-07
 */

#pragma once
#include "BlockExecutive.h"
#include <bcos-table/src/ContractShardUtils.h>
#include <tbb/concurrent_unordered_map.h>

namespace bcos::scheduler
{
class ShardingBlockExecutive : public BlockExecutive
{
public:
    using Ptr = std::shared_ptr<ShardingBlockExecutive>;

    ShardingBlockExecutive(bcos::protocol::Block::Ptr block, SchedulerImpl* scheduler,
        size_t startContextID,
        bcos::protocol::TransactionSubmitResultFactory::Ptr transactionSubmitResultFactory,
        bool staticCall, bcos::protocol::BlockFactory::Ptr _blockFactory,
        bcos::txpool::TxPoolInterface::Ptr _txPool,
        std::shared_ptr<tbb::concurrent_unordered_map<std::string, std::string>>
            _contract2ShardCache)
      : BlockExecutive(block, scheduler, startContextID, transactionSubmitResultFactory, staticCall,
            _blockFactory, _txPool),
        m_contract2ShardCache(_contract2ShardCache){};

    ShardingBlockExecutive(bcos::protocol::Block::Ptr block, SchedulerImpl* scheduler,
        size_t startContextID,
        bcos::protocol::TransactionSubmitResultFactory::Ptr transactionSubmitResultFactory,
        bool staticCall, bcos::protocol::BlockFactory::Ptr _blockFactory,
        bcos::txpool::TxPoolInterface::Ptr _txPool,
        std::shared_ptr<tbb::concurrent_unordered_map<std::string, std::string>>
            _contract2ShardCache,
        uint64_t _gasLimit, bool _syncBlock)
      : BlockExecutive(block, scheduler, startContextID, transactionSubmitResultFactory, staticCall,
            _blockFactory, _txPool, _gasLimit, _syncBlock),
        m_contract2ShardCache(_contract2ShardCache)
    {}

    void shardingExecute(
        std::function<void(Error::UniquePtr, protocol::BlockHeader::Ptr, bool)> callback);

    void asyncExecute(
        std::function<void(Error::UniquePtr, protocol::BlockHeader::Ptr, bool)> callback) override;

    void prepare() override;

    std::shared_ptr<DmcExecutor> registerAndGetDmcExecutor(std::string contractAddress) override;
    std::shared_ptr<DmcExecutor> buildDmcExecutor(const std::string& name,
        const std::string& contractAddress,
        bcos::executor::ParallelTransactionExecutorInterface::Ptr executor) override;

private:
    bool needPrepareExecutor() override { return false; }

    std::string getContractShard(const std::string& contractAddress);

    std::optional<bcos::storage::StorageWrapper> m_storageWrapper;
    // tbb::concurrent_unordered_map<std::string, std::string> m_contract2Shard;
    std::shared_ptr<tbb::concurrent_unordered_map<std::string, std::string>> m_contract2ShardCache;
};
}  // namespace bcos::scheduler
