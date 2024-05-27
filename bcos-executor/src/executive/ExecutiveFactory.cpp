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
#include "../vm/Precompiled.h"
#include "BillingTransactionExecutive.h"
#include "CoroutineTransactionExecutive.h"
#include "PromiseTransactionExecutive.h"
#include "ShardingTransactionExecutive.h"
#include "TransactionExecutive.h"
#include "bcos-executor/src/precompiled/CastPrecompiled.h"
#include "bcos-executor/src/precompiled/extension/AccountManagerPrecompiled.h"
#include "bcos-executor/src/precompiled/extension/AccountPrecompiled.h"
#include "bcos-framework/executor/PrecompiledTypeDef.h"
#include "bcos-framework/protocol/Protocol.h"

using namespace bcos::executor;
using namespace bcos::precompiled;


std::shared_ptr<TransactionExecutive> ExecutiveFactory::build(
    const std::string& _contractAddress, int64_t contextID, int64_t seq, ExecutiveType execType)
{
    std::shared_ptr<TransactionExecutive> executive;
    switch (execType)
    {
    case ExecutiveType::coroutine:
        /*
        if (m_isTiKVStorage)
        {
            // this logic is just for version lesser than 3.3.0, bug fix
            executive = std::make_shared<PromiseTransactionExecutive>(m_poolForPromiseWait,
                m_blockContext, _contractAddress, contextID, seq, m_gasInjector);
        }
        else
        {
         */
        executive = std::make_shared<CoroutineTransactionExecutive>(
            m_blockContext, _contractAddress, contextID, seq, m_gasInjector);
        //}
        break;
    case ExecutiveType::billing:
        executive = std::make_shared<BillingTransactionExecutive>(
            m_blockContext, _contractAddress, contextID, seq, m_gasInjector);
        break;
    default:
        executive = std::make_shared<TransactionExecutive>(
            m_blockContext, _contractAddress, contextID, seq, m_gasInjector);
    }

    setParams(executive);
    return executive;
}

void ExecutiveFactory::registerExtPrecompiled(std::shared_ptr<TransactionExecutive>& executive)
{
    // TODO: register User developed Precompiled contract if needed
    // registerUserPrecompiled(context);
}

void ExecutiveFactory::setParams(std::shared_ptr<TransactionExecutive> executive)
{
    executive->setPrecompiled(m_precompiled);
    executive->setEVMPrecompiled(m_evmPrecompiled);
    executive->setStaticPrecompiled(m_staticPrecompiled);

    registerExtPrecompiled(executive);
}
std::shared_ptr<bcos::precompiled::Precompiled> ExecutiveFactory::getPrecompiled(
    const std::string& address) const
{
    return m_precompiled->at(address, m_blockContext.blockVersion(), m_blockContext.isAuthCheck(),
        m_blockContext.features());
}

std::shared_ptr<TransactionExecutive> ShardingExecutiveFactory::build(
    const std::string& _contractAddress, int64_t contextID, int64_t seq, ExecutiveType execType)
{
    std::shared_ptr<TransactionExecutive> executive;
    bool needUsePromise = false;
    switch (execType)
    {
    case ExecutiveType::coroutine:
        needUsePromise = m_isTiKVStorage;  // tikv storage need to use promise executive
        executive = std::make_shared<ShardingTransactionExecutive>(m_blockContext, _contractAddress,
            contextID, seq, m_gasInjector, m_poolForPromiseWait, needUsePromise);
        break;
    default:
        assert(execType != ExecutiveType::billing);
        executive = std::make_shared<TransactionExecutive>(
            m_blockContext, _contractAddress, contextID, seq, m_gasInjector);
    }

    setParams(executive);
    return executive;
}

std::shared_ptr<TransactionExecutive> ShardingExecutiveFactory::buildChild(
    ShardingTransactionExecutive* parent, const std::string& _contractAddress, int64_t contextID,
    int64_t seq)
{
    TransactionExecutive::Ptr childExecutive = std::make_shared<ShardingChildTransactionExecutive>(
        parent, m_blockContext, _contractAddress, contextID, seq, m_gasInjector,
        m_poolForPromiseWait, parent->isUsePromise());
    setParams(childExecutive);
    return childExecutive;
}
