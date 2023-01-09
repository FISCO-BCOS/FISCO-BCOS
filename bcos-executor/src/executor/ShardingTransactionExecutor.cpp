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
 * @file ShardingExecutor.cpp
 * @author: JimmyShi22
 * @date: 2023-01-07
 */

#include "../executive/ExecutiveDagFlow.h"
#include "ShardingTransactionExecutor.h"

using namespace bcos::executor;

void ShardingTransactionExecutor::executeTransactions(std::string contractAddress,
    gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
    std::function<void(
        bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
        callback)
{
    if (m_blockVersion >= uint32_t(bcos::protocol::BlockVersion::V3_3_VERSION))
    {
        TransactionExecutor::executeTransactions(
            contractAddress, std::move(inputs), std::move(callback));
    }
    else
    {
        TransactionExecutor::executeTransactions(
            contractAddress, std::move(inputs), std::move(callback));
    }
}

std::shared_ptr<ExecutiveFlowInterface> ShardingTransactionExecutor::getExecutiveFlow(
    std::shared_ptr<BlockContext> blockContext, std::string codeAddress, bool useCoroutine)
{
    if (m_blockVersion >= uint32_t(bcos::protocol::BlockVersion::V3_3_VERSION))
    {
        EXECUTOR_NAME_LOG(DEBUG) << "getExecutiveFlow" << LOG_KV("codeAddress", codeAddress);
        bcos::RecursiveGuard lock(x_executiveFlowLock);
        ExecutiveFlowInterface::Ptr executiveFlow = blockContext->getExecutiveFlow(codeAddress);
        if (executiveFlow == nullptr)
        {
            auto executiveFactory = std::make_shared<ExecutiveFactory>(blockContext,
                m_precompiledContract, m_constantPrecompiled, m_builtInPrecompiled, m_gasInjector);

            executiveFlow = std::make_shared<ExecutiveDagFlow>(executiveFactory);
            executiveFlow->setThreadPool(m_threadPool);
            blockContext->setExecutiveFlow(codeAddress, executiveFlow);
        }
        return executiveFlow;
    }
    else
    {
        return TransactionExecutor::getExecutiveFlow(blockContext, codeAddress, useCoroutine);
    }
}
