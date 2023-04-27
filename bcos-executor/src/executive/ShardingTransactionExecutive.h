/*
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
 * @brief Execute transaction in a shard
 * @file ShardingTransactionExecutive.h
 * @author: JimmyShi22
 * @date: 2023-01-07
 */

#pragma once
#include "CoroutineTransactionExecutive.h"
#include "PromiseTransactionExecutive.h"

namespace bcos::executor
{
class ShardingTransactionExecutive : public PromiseTransactionExecutive
{
public:
    ShardingTransactionExecutive(const BlockContext& blockContext, std::string contractAddress,
        int64_t contextID, int64_t seq, const wasm::GasInjector& gasInjector,
        ThreadPool::Ptr pool = nullptr, bool usePromise = false);

    ~ShardingTransactionExecutive() override = default;

    CallParameters::UniquePtr start(CallParameters::UniquePtr input) override;

    CallParameters::UniquePtr externalCall(CallParameters::UniquePtr input) override;

    CallParameters::UniquePtr resume() override;

    TransactionExecutive::Ptr buildChildExecutive(const std::string& _contractAddress,
        int64_t contextID, int64_t seq, bool useCoroutine = true) override
    {
        ShardingExecutiveFactory executiveFactory = ShardingExecutiveFactory(
            m_blockContext, m_evmPrecompiled, m_precompiled, m_staticPrecompiled, m_gasInjector);

        return executiveFactory.build(_contractAddress, contextID, seq, useCoroutine);
    }

    std::string getContractShard(const std::string_view& contractAddress);

private:
    std::optional<std::string> m_shardName;
    bool m_usePromise;
};
}  // namespace bcos::executor
