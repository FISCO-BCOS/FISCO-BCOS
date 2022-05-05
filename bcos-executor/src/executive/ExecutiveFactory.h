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
 * @file ExecutiveFactory.h
 * @author: jimmyshi
 * @date: 2022-03-22
 */

#pragma once

#include "../executor/TransactionExecutor.h"
#include <tbb/concurrent_unordered_map.h>
#include <atomic>
#include <stack>

namespace bcos
{
namespace executor
{

class BlockContext;
class TransactionExecutive;

class ExecutiveFactory
{
public:
    using Ptr = std::shared_ptr<ExecutiveFactory>;


    ExecutiveFactory(std::shared_ptr<BlockContext> blockContext,
        std::shared_ptr<std::map<std::string, std::shared_ptr<PrecompiledContract>>>
            precompiledContract,
        std::shared_ptr<std::map<std::string, std::shared_ptr<precompiled::Precompiled>>>
            constantPrecompiled,
        std::shared_ptr<const std::set<std::string>> builtInPrecompiled,
        std::shared_ptr<wasm::GasInjector> gasInjector)
      : m_precompiledContract(precompiledContract),
        m_constantPrecompiled(constantPrecompiled),
        m_builtInPrecompiled(builtInPrecompiled),
        m_blockContext(blockContext),
        m_gasInjector(gasInjector)
    {}

    std::shared_ptr<TransactionExecutive> build(
        const std::string& _contractAddress, int64_t contextID, int64_t seq);


private:
    std::shared_ptr<std::map<std::string, std::shared_ptr<PrecompiledContract>>>
        m_precompiledContract;
    std::shared_ptr<std::map<std::string, std::shared_ptr<precompiled::Precompiled>>>
        m_constantPrecompiled;
    std::shared_ptr<const std::set<std::string>> m_builtInPrecompiled;
    std::shared_ptr<BlockContext> m_blockContext;
    std::shared_ptr<wasm::GasInjector> m_gasInjector;
};

}  // namespace executor
}  // namespace bcos
