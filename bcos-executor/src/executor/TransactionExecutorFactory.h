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

#include "TransactionExecutor.h"
#include "bcos-framework/storage/StorageInterface.h"
#include "bcos-ledger/src/libledger/utilities/Common.h"
#include <bcos-table/src/CacheStorageFactory.h>


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
        bcos::crypto::Hash::Ptr hashImpl, bool isWasm, bool isAuthCheck, size_t keyPageSize,
        std::string name = "executor-" + std::to_string(utcTime()))
    {  // only for test
        auto keyPageIgnoreTables = std::make_shared<std::set<std::string, std::less<>>>(
            std::initializer_list<std::set<std::string, std::less<>>::value_type>{
                std::string(ledger::SYS_CONFIG),
                std::string(ledger::SYS_CONSENSUS),
                ledger::FS_ROOT,
                ledger::FS_APPS,
                ledger::FS_USER,
                ledger::FS_SYS_BIN,
                ledger::FS_USER_TABLE,
                storage::StorageInterface::SYS_TABLES,
            });
        return std::make_shared<TransactionExecutor>(ledger, txpool, cachedStorage, backendStorage,
            executionMessageFactory, hashImpl, isWasm, isAuthCheck, keyPageSize,
            keyPageIgnoreTables, name);
    }

    TransactionExecutorFactory(bcos::ledger::LedgerInterface::Ptr ledger,
        txpool::TxPoolInterface::Ptr txpool, storage::CacheStorageFactory::Ptr cacheFactory,
        storage::TransactionalStorageInterface::Ptr storage,
        protocol::ExecutionMessageFactory::Ptr executionMessageFactory,
        bcos::crypto::Hash::Ptr hashImpl, bool isWasm, bool isAuthCheck, size_t keyPageSize,
        std::string name)
      : m_name(name),
        m_keyPageSize(keyPageSize),
        m_ledger(ledger),
        m_txpool(txpool),
        m_cacheFactory(cacheFactory),
        m_storage(storage),
        m_executionMessageFactory(executionMessageFactory),
        m_hashImpl(hashImpl),
        m_isWasm(isWasm),
        m_isAuthCheck(isAuthCheck)
    {
        m_keyPageIgnoreTables = std::make_shared<std::set<std::string, std::less<>>>(
            std::initializer_list<std::set<std::string, std::less<>>::value_type>{
                std::string(ledger::SYS_CONFIG),
                std::string(ledger::SYS_CONSENSUS),
                ledger::FS_ROOT,
                ledger::FS_APPS,
                ledger::FS_USER,
                ledger::FS_SYS_BIN,
                ledger::FS_USER_TABLE,
                storage::StorageInterface::SYS_TABLES,
            });
    }

    TransactionExecutor::Ptr build()
    {
        auto executor = std::make_shared<TransactionExecutor>(m_ledger, m_txpool,
            m_cacheFactory ? m_cacheFactory->build() : nullptr, m_storage,
            m_executionMessageFactory, m_hashImpl, m_isWasm, m_isAuthCheck, m_keyPageSize,
            m_keyPageIgnoreTables, m_name + "-" + std::to_string(utcTime()));
        if (f_onNeedSwitchEvent)
        {
            executor->registerNeedSwitchEvent(f_onNeedSwitchEvent);
        }
        return executor;
    }

    void registerNeedSwitchEvent(std::function<void()> event) { f_onNeedSwitchEvent = event; }

private:
    std::string m_name;
    size_t m_keyPageSize;
    std::shared_ptr<std::set<std::string, std::less<>>> m_keyPageIgnoreTables;
    bcos::ledger::LedgerInterface::Ptr m_ledger;
    txpool::TxPoolInterface::Ptr m_txpool;
    storage::CacheStorageFactory::Ptr m_cacheFactory;
    storage::TransactionalStorageInterface::Ptr m_storage;
    protocol::ExecutionMessageFactory::Ptr m_executionMessageFactory;
    bcos::crypto::Hash::Ptr m_hashImpl;
    bool m_isWasm;
    bool m_isAuthCheck;
    std::function<void()> f_onNeedSwitchEvent;
};

}  // namespace executor
}  // namespace bcos
