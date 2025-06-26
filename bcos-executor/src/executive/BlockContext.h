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
 * @brief block level context
 * @file BlockContext.h
 * @author: xingqiangbai
 * @date: 2021-05-26
 */

#pragma once
#include "../Common.h"
#include "ExecutiveFactory.h"
#include "ExecutiveFlowInterface.h"
#include "LedgerCache.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/storage/EntryCache.h"
#include "bcos-framework/storage/Table.h"
#include "bcos-table/src/StateStorage.h"
#include "bcos-utilities/BucketMap.h"
#include <tbb/concurrent_unordered_map.h>
#include <functional>
#include <memory>
#include <string_view>

namespace bcos::executor
{
class TransactionExecutive;
class PrecompiledContract;

class BlockContext : public std::enable_shared_from_this<BlockContext>
{
public:
    using Ptr = std::shared_ptr<BlockContext>;

    BlockContext(std::shared_ptr<storage::StateStorageInterface> storage,
        LedgerCache::Ptr ledgerCache, crypto::Hash::Ptr _hashImpl,
        bcos::protocol::BlockNumber blockNumber, h256 blockHash, uint64_t timestamp,
        uint32_t blockVersion, bool _isWasm, bool _isAuthCheck,
        storage::StorageInterface::Ptr backendStorage = nullptr);

    BlockContext(std::shared_ptr<storage::StateStorageInterface> storage,
        LedgerCache::Ptr ledgerCache, crypto::Hash::Ptr _hashImpl,
        protocol::BlockHeader const& current, bool _isWasm, bool _isAuthCheck,
        storage::StorageInterface::Ptr backendStorage = nullptr,
        std::shared_ptr<std::set<std::string, std::less<>>> = nullptr);

    using getTxCriticalsHandler = std::function<std::shared_ptr<std::vector<std::string>>(
        const protocol::Transaction::ConstPtr& _tx)>;
    virtual ~BlockContext() = default;

    std::shared_ptr<storage::StateStorageInterface> storage() const { return m_storage; }
    using transientStorageMap = BucketMap<int64_t, std::shared_ptr<storage::StateStorageInterface>>;
    std::shared_ptr<transientStorageMap> getTransientStorageMap() const
    {
        return m_transientStorageMap;
    }

    uint64_t txGasLimit() const { return m_ledgerCache->fetchTxGasLimit(); }

    auto getTxCriticals(const protocol::Transaction::ConstPtr& _tx)
        -> std::shared_ptr<std::vector<std::string>>;

    crypto::Hash::Ptr hashHandler() const { return m_hashImpl; }
    bool isWasm() const { return m_isWasm; }
    bool isAuthCheck() const { return m_isAuthCheck; }
    int64_t number() const { return m_blockNumber; }
    h256 hash() const { return m_blockHash; }
    h256 parentHash() const { return m_parentHash; }
    h256 blockHash(int64_t _number) const { return m_ledgerCache->fetchBlockHash(_number); }
    uint64_t timestamp() const { return m_timeStamp; }
    uint32_t blockVersion() const { return m_blockVersion; }
    void suicide(std::string_view address);
    void killSuicides();

    VMSchedule const& vmSchedule() const { return m_schedule; }

    LedgerCache::Ptr const& ledgerCache() const { return m_ledgerCache; }

    ExecutiveFlowInterface::Ptr getExecutiveFlow(std::string codeAddress);
    void setExecutiveFlow(std::string codeAddress, ExecutiveFlowInterface::Ptr executiveFlow);

    std::shared_ptr<VMFactory> getVMFactory() const { return m_vmFactory; }
    void setVMSchedule();
    void setVMFactory(std::shared_ptr<VMFactory> factory) { m_vmFactory = factory; }

    void stop()
    {
        std::vector<ExecutiveFlowInterface::Ptr> executiveFlow2Stop;
        {
            bcos::ReadGuard l(x_executiveFlows);
            for (auto it : m_executiveFlows)
            {
                EXECUTOR_LOG(INFO) << "Try to stop flow: " << it.first;
                executiveFlow2Stop.push_back(it.second);
            }
        }

        if (executiveFlow2Stop.empty())
        {
            return;
        }

        for (auto executiveFlow : executiveFlow2Stop)
        {
            executiveFlow->stop();
        }
    }
    void clear()
    {
        bcos::WriteGuard l(x_executiveFlows);
        m_executiveFlows.clear();
    }

    void registerNeedSwitchEvent(std::function<void()> event) { f_onNeedSwitchEvent = event; }

    void triggerSwitch()
    {
        if (f_onNeedSwitchEvent)
        {
            f_onNeedSwitchEvent();
        }
    }

    auto keyPageIgnoreTables() const { return m_keyPageIgnoreTables; }

    const ledger::Features& features() const;
    const ledger::SystemConfigs& configs() const;
    void setFeatures(ledger::Features features) { m_features = std::move(features); }
    void setConfigs(ledger::SystemConfigs configs) { m_configs = std::move(configs); }
    storage::EntryCachePtr getCodeCache() const { return m_codeCache; }
    storage::EntryCachePtr getCodeHashCache() const { return m_codeHashCache; }
    auto backendStorage() const { return m_backendStorage; }

private:
    mutable bcos::SharedMutex x_executiveFlows;
    tbb::concurrent_unordered_map<std::string, ExecutiveFlowInterface::Ptr> m_executiveFlows;

    bcos::protocol::BlockNumber m_blockNumber;
    h256 m_blockHash;
    h256 m_parentHash;
    uint64_t m_timeStamp;
    uint32_t m_blockVersion;

    VMSchedule m_schedule;
    bool m_isWasm = false;
    bool m_isAuthCheck = false;
    std::shared_ptr<storage::StateStorageInterface> m_storage;
    transientStorageMap::Ptr m_transientStorageMap;
    crypto::Hash::Ptr m_hashImpl;
    std::function<void()> f_onNeedSwitchEvent;
    std::shared_ptr<std::set<std::string, std::less<>>> m_keyPageIgnoreTables;
    LedgerCache::Ptr m_ledgerCache;
    std::set<std::string> m_suicides;  // contract address need to selfdestruct
    mutable bcos::SharedMutex x_suicides;
    std::shared_ptr<VMFactory> m_vmFactory;

    storage::EntryCachePtr m_codeCache = std::make_shared<storage::EntryCache>();
    storage::EntryCachePtr m_codeHashCache = std::make_shared<storage::EntryCache>();
    bcos::storage::StorageInterface::Ptr m_backendStorage;
    ledger::Features m_features;
    ledger::SystemConfigs m_configs;
};

}  // namespace bcos::executor
