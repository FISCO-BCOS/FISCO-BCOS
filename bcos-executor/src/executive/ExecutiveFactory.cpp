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
    executive->setConstantPrecompiled(m_constantPrecompiled);
    executive->setEVMPrecompiled(m_precompiledContract);
    executive->setBuiltInPrecompiled(m_builtInPrecompiled);

    registerExtPrecompiled(executive);
    return executive;
}
void ExecutiveFactory::registerExtPrecompiled(std::shared_ptr<TransactionExecutive>& executive)
{
    auto blockContext = m_blockContext.lock();
    if (blockContext->blockVersion() >= (uint32_t)protocol::BlockVersion::V3_1_VERSION)
    {
        if (!executive->isPrecompiled(ACCOUNT_MGR_ADDRESS) &&
            !executive->isPrecompiled(ACCOUNT_MANAGER_NAME))
        {
            executive->setConstantPrecompiled(
                blockContext->isWasm() ? ACCOUNT_MANAGER_NAME : ACCOUNT_MGR_ADDRESS,
                std::make_shared<AccountManagerPrecompiled>());
        }

        if (!executive->isPrecompiled(ACCOUNT_ADDRESS))
        {
            executive->setConstantPrecompiled(
                ACCOUNT_ADDRESS, std::make_shared<AccountPrecompiled>());
        }
    }
    // TODO: register User developed Precompiled contract
    // registerUserPrecompiled(context);
}