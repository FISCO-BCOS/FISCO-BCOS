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
 * @brief factory of executive
 * @file ExecutiveFactory.cpp
 * @author: jimmyshi
 * @date: 2022-03-22
 */

#include "ExecutiveFactory.h"
#include "CoroutineTransactionExecutive.h"
#include "ShardingTransactionExecutive.h"
#include "TransactionExecutive.h"
#include "bcos-executor/src/precompiled/extension/AccountManagerPrecompiled.h"
#include "bcos-executor/src/precompiled/extension/AccountPrecompiled.h"
#include "bcos-framework/executor/PrecompiledTypeDef.h"
#include "bcos-framework/protocol/Protocol.h"

using namespace bcos::executor;
using namespace bcos::precompiled;


std::shared_ptr<TransactionExecutive> ExecutiveFactory::build(
    const std::string& _contractAddress, int64_t contextID, int64_t seq, bool useCoroutine)
{
    std::shared_ptr<TransactionExecutive> executive;
    if (useCoroutine)
    {
        executive = std::make_shared<CoroutineTransactionExecutive>(
            m_blockContext, _contractAddress, contextID, seq, m_gasInjector);
    }
    else
    {
        executive = std::make_shared<TransactionExecutive>(
            m_blockContext, _contractAddress, contextID, seq, m_gasInjector);
    }

    setParams(executive);
    return executive;
}

void ExecutiveFactory::registerExtPrecompiled(std::shared_ptr<TransactionExecutive>& executive)
{
    // Code below has moved to initEvmEnvironment & initWasmEnvironment in TransactionExecutor.cpp:
    //     m_constantPrecompiled->insert(
    //        {ACCOUNT_MGR_ADDRESS, std::make_shared<AccountManagerPrecompiled>()});
    //    m_constantPrecompiled->insert({ACCOUNT_MANAGER_NAME,
    //    std::make_shared<AccountManagerPrecompiled>()});
    //    m_constantPrecompiled->insert({ACCOUNT_ADDRESS, std::make_shared<AccountPrecompiled>()});

    // TODO: register User developed Precompiled contract
    // registerUserPrecompiled(context);
}

void ExecutiveFactory::setParams(std::shared_ptr<TransactionExecutive> executive)
{
    executive->setConstantPrecompiled(m_constantPrecompiled);
    executive->setEVMPrecompiled(m_precompiledContract);
    executive->setBuiltInPrecompiled(m_builtInPrecompiled);

    registerExtPrecompiled(executive);
}

std::shared_ptr<TransactionExecutive> ShardingExecutiveFactory::build(
    const std::string& _contractAddress, int64_t contextID, int64_t seq, bool useCoroutine)
{
    if (useCoroutine)
    {
        auto executive = std::make_shared<ShardingTransactionExecutive>(
            m_blockContext, _contractAddress, contextID, seq, m_gasInjector);
        setParams(executive);
        return executive;
    }
    else
    {
        auto executive = std::make_shared<TransactionExecutive>(
            m_blockContext, _contractAddress, contextID, seq, m_gasInjector);
        setParams(executive);
        return executive;
    }
};
