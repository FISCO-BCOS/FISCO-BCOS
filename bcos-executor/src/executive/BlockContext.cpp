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
 * @date: 2021-05-27
 */

#include "BlockContext.h"
#include "../vm/Precompiled.h"
#include "TransactionExecutive.h"
#include "bcos-framework/storage/StorageInterface.h"
#include "bcos-framework/storage/Table.h"
#include "bcos-task/Wait.h"
#include <bcos-utilities/Error.h>
#include <boost/core/ignore_unused.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/throw_exception.hpp>
#include <string>
#include <utility>

using namespace bcos::executor;
using namespace bcos::protocol;
using namespace bcos::precompiled;
using namespace std;

BlockContext::BlockContext(std::shared_ptr<storage::StateStorageInterface> storage,
    LedgerCache::Ptr ledgerCache, crypto::Hash::Ptr _hashImpl,
    bcos::protocol::BlockNumber blockNumber, h256 blockHash, uint64_t timestamp,
    uint32_t blockVersion, bool _isWasm, bool _isAuthCheck,
    storage::StorageInterface::Ptr backendStorage)
  : m_blockNumber(blockNumber),
    m_blockHash(blockHash),
    m_timeStamp(timestamp),
    m_blockVersion(blockVersion),
    m_isWasm(_isWasm),
    m_isAuthCheck(_isAuthCheck),
    m_storage(std::move(storage)),
    m_transientStorageMap(std::make_shared<transientStorageMap>(10)),
    m_hashImpl(std::move(_hashImpl)),
    m_ledgerCache(std::move(ledgerCache)),
    m_backendStorage(std::move(backendStorage))
{
    if (!m_storage)
    {
        EXECUTOR_LOG(WARNING) << "No available storage, make sure it's testing";
        return;
    }

    task::syncWait(readFromStorage(m_features, *m_storage, m_blockNumber));
    setVMSchedule();
}

BlockContext::BlockContext(std::shared_ptr<storage::StateStorageInterface> storage,
    LedgerCache::Ptr ledgerCache, crypto::Hash::Ptr _hashImpl, protocol::BlockHeader const& current,
    bool _isWasm, bool _isAuthCheck, storage::StorageInterface::Ptr backendStorage,
    std::shared_ptr<std::set<std::string, std::less<>>> _keyPageIgnoreTables)
  : BlockContext(std::move(storage), std::move(ledgerCache), std::move(_hashImpl), current.number(),
        current.hash(), current.timestamp(), current.version(), _isWasm, _isAuthCheck,
        std::move(backendStorage))
{
    if (current.number() > 0 && !current.parentInfo().empty())
    {
        auto view = current.parentInfo();
        auto it = view.begin();
        m_parentHash = (*it).blockHash;
    }

    m_keyPageIgnoreTables = std::move(_keyPageIgnoreTables);

    auto table = m_storage->openTable(ledger::SYS_CONFIG);
    if (table)
    {
        for (auto key : bcos::ledger::Features::featureKeys())
        {
            auto entry = table->getRow(key);
            if (entry)
            {
                auto [value, enableNumber] = entry->getObject<ledger::SystemConfigEntry>();
                if (current.number() >= enableNumber)
                {
                    m_features.set(key);
                }
            }
        }
    }
    setVMSchedule();
}


ExecutiveFlowInterface::Ptr BlockContext::getExecutiveFlow(std::string codeAddress)
{
    bcos::ReadGuard l(x_executiveFlows);
    auto it = m_executiveFlows.find(codeAddress);
    if (it == m_executiveFlows.end())
    {
        /*
        bool success;
        std::tie(it, success) =
            m_executiveFlows.emplace(codeAddress, std::make_shared<ExecutiveStackFlow>());

            */
        return nullptr;
    }
    return it->second;
}

void BlockContext::setExecutiveFlow(
    std::string codeAddress, ExecutiveFlowInterface::Ptr executiveFlow)
{
    bcos::ReadGuard l(x_executiveFlows);
    m_executiveFlows.emplace(codeAddress, executiveFlow);
}
void BlockContext::setVMSchedule()
{
    if (m_features.get(ledger::Features::Flag::feature_evm_cancun))
    {
        m_schedule = FiscoBcosScheduleCancun;
    }
    else if (m_blockVersion >= (uint32_t)bcos::protocol::BlockVersion::V3_2_VERSION)
    {
        m_schedule = FiscoBcosScheduleV320;
    }
    else
    {
        m_schedule = FiscoBcosSchedule;
    }
}

void BlockContext::suicide(std::string_view contract2Suicide)
{
    {
        bcos::WriteGuard l(x_suicides);
        m_suicides.emplace(contract2Suicide);
    }

    EXECUTOR_LOG(TRACE) << LOG_BADGE("SUICIDE")
                        << "Add suicide: " << LOG_KV("table2Suicide", contract2Suicide)
                        << LOG_KV("suicides.size", m_suicides.size())
                        << LOG_KV("blockNumber", m_blockNumber);
}

void BlockContext::killSuicides()
{
    bcos::ReadGuard l(x_suicides);
    if (m_suicides.empty())
    {
        return;
    }

    auto emptyCodeHash = m_hashImpl->hash(""sv);
    for (std::string_view table2Suicide : m_suicides)
    {
        auto contractTable = storage()->openTable(table2Suicide);

        if (contractTable)
        {
            // set codeHash
            bcos::storage::Entry emptyCodeHashEntry;
            emptyCodeHashEntry.importFields({emptyCodeHash.asBytes()});
            contractTable->setRow(ACCOUNT_CODE_HASH, std::move(emptyCodeHashEntry));

            // delete binary
            bcos::storage::Entry emptyCodeEntry;
            emptyCodeEntry.importFields({""});
            contractTable->setRow(ACCOUNT_CODE, std::move(emptyCodeEntry));
        }

        EXECUTOR_LOG(TRACE) << LOG_BADGE("SUICIDE")
                            << "Kill contract: " << LOG_KV("contract2Suicide", table2Suicide)
                            << LOG_KV("blockNumber", m_blockNumber);
    }
}

const bcos::ledger::Features& bcos::executor::BlockContext::features() const
{
    return m_features;
}
