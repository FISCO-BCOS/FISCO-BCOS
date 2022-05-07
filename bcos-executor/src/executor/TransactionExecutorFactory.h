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
    using Ptr = std::shared_ptr<TransactionExecutorFactory>;

    static TransactionExecutor::Ptr build(bcos::ledger::LedgerInterface::Ptr ledger,
        txpool::TxPoolInterface::Ptr txpool, storage::MergeableStorageInterface::Ptr cachedStorage,
        storage::TransactionalStorageInterface::Ptr backendStorage,
        protocol::ExecutionMessageFactory::Ptr executionMessageFactory,
        bcos::crypto::Hash::Ptr hashImpl, bool isWasm, bool isAuthCheck,
        std::string name = "executor-" + std::to_string(utcTime()))
    {
        if (isWasm)
        {
            return std::make_shared<WasmTransactionExecutor>(ledger, txpool, cachedStorage,
                backendStorage, executionMessageFactory, hashImpl, isAuthCheck, name);
        }
        else
        {
            return std::make_shared<EvmTransactionExecutor>(ledger, txpool, cachedStorage,
                backendStorage, executionMessageFactory, hashImpl, isAuthCheck, name);
        }
    }

    TransactionExecutorFactory(bcos::ledger::LedgerInterface::Ptr ledger,
        txpool::TxPoolInterface::Ptr txpool, storage::MergeableStorageInterface::Ptr cache,
        storage::TransactionalStorageInterface::Ptr storage,
        protocol::ExecutionMessageFactory::Ptr executionMessageFactory,
        bcos::crypto::Hash::Ptr hashImpl, bool isWasm, bool isAuthCheck,
        std::string name = "executor-" + std::to_string(utcTime()))
      : m_name(name),
        m_ledger(ledger),
        m_txpool(txpool),
        m_cache(cache),
        m_storage(storage),
        m_executionMessageFactory(executionMessageFactory),
        m_hashImpl(hashImpl),
        m_isWasm(isWasm),
        m_isAuthCheck(isAuthCheck)
    {}

    TransactionExecutor::Ptr build()
    {
        return build(m_ledger, m_txpool, m_cache, m_storage, m_executionMessageFactory, m_hashImpl,
            m_isWasm, m_isAuthCheck, m_name);
    }

private:
    std::string m_name;
    bcos::ledger::LedgerInterface::Ptr m_ledger;
    txpool::TxPoolInterface::Ptr m_txpool;
    storage::MergeableStorageInterface::Ptr m_cache;
    storage::TransactionalStorageInterface::Ptr m_storage;
    protocol::ExecutionMessageFactory::Ptr m_executionMessageFactory;
    bcos::crypto::Hash::Ptr m_hashImpl;
    bool m_isWasm;
    bool m_isAuthCheck;
};

}  // namespace executor
}  // namespace bcos
