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
 * @brief TransactionExecutorFactory
 * @file TransactionExecutorFactory.h
 * @author: jimmyshi
 * @date: 2022-01-19
 */
#pragma once

#include "EvmTransactionExecutor.h"
#include "WasmTransactionExecutor.h"

namespace bcos
{
namespace executor
{
class TransactionExecutorFactory
{
public:
    static TransactionExecutor::Ptr build(txpool::TxPoolInterface::Ptr txpool,
        storage::MergeableStorageInterface::Ptr cachedStorage,
        storage::TransactionalStorageInterface::Ptr backendStorage,
        protocol::ExecutionMessageFactory::Ptr executionMessageFactory,
        bcos::crypto::Hash::Ptr hashImpl, bool isWasm, bool isAuthCheck)
    {
        if (isWasm)
        {
            return std::make_shared<WasmTransactionExecutor>(txpool, cachedStorage, backendStorage,
                executionMessageFactory, hashImpl, isAuthCheck);
        }
        else
        {
            return std::make_shared<EvmTransactionExecutor>(txpool, cachedStorage, backendStorage,
                executionMessageFactory, hashImpl, isAuthCheck);
        }
    }
};

}  // namespace executor
}  // namespace bcos
