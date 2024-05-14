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
 * @brief interface of Table
 * @file CacheStorageFactory.h
 * @author: wenlinli
 * @date: 2022-12-14
 */

#pragma once

#include "StateStorageInterface.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-table/src/KeyPageStorage.h"
#include "bcos-table/src/StateStorage.h"
#include "bcos-tool/BfsFileFactory.h"
#include <bcos-framework/consensus/ConsensusNodeInterface.h>
#include <bcos-framework/protocol/Block.h>
#include <tbb/concurrent_unordered_map.h>

namespace bcos::storage
{

// FileSystem paths
constexpr static const char* const FS_ROOT = "/";
constexpr static const char* const FS_APPS = "/apps";
constexpr static const char* const FS_USER = "/usr";
constexpr static const char* const FS_SYS_BIN = "/sys";
constexpr static const char* const FS_USER_TABLE = "/tables";
constexpr static const std::array<std::string_view, 8> IGNORED_ARRAY{
    bcos::ledger::SYS_CONFIG,
    bcos::ledger::SYS_CONSENSUS,
    bcos::storage::FS_ROOT,
    bcos::storage::FS_APPS,
    bcos::storage::FS_USER,
    bcos::storage::FS_SYS_BIN,
    bcos::storage::FS_USER_TABLE,
    bcos::storage::StorageInterface::SYS_TABLES,
};

constexpr static const std::array<std::string_view, 5> IGNORED_ARRAY_310{bcos::ledger::SYS_CONFIG,
    bcos::ledger::SYS_CONSENSUS, bcos::storage::StorageInterface::SYS_TABLES,
    bcos::ledger::SYS_CODE_BINARY, bcos::ledger::SYS_CONTRACT_ABI};

class StateStorageFactory
{
public:
    using Ptr = std::shared_ptr<StateStorageFactory>;
    StateStorageFactory(size_t keyPageSize) : m_keyPageSize(keyPageSize) {}

    virtual ~StateStorageFactory() = default;

    virtual storage::StateStorageInterface::Ptr createStateStorage(
        bcos::storage::StorageInterface::Ptr storage, uint32_t compatibilityVersion,
        bool setRowWithDirtyFlag, bool ignoreNotExist = false,
        std::shared_ptr<std::set<std::string, std::less<>>> const& keyPageIgnoreTables = nullptr)
    {
        STORAGE_LOG(TRACE) << LOG_KV("compatibilityVersion", compatibilityVersion)
                           << LOG_KV("protocol::BlockVersion::V3_1_VERSION",
                                  (uint32_t)protocol::BlockVersion::V3_1_VERSION)
                           << LOG_KV("keyPageSize", m_keyPageSize)
                           << LOG_KV("setRowWithDirtyFlag", setRowWithDirtyFlag);

        if (m_keyPageSize > 0)
        {
            if (compatibilityVersion >= (uint32_t)protocol::BlockVersion::V3_1_VERSION &&
                keyPageIgnoreTables != nullptr)
            {
                if (keyPageIgnoreTables->contains(tool::FS_ROOT))
                {
                    for (const auto& _sub : tool::FS_ROOT_SUBS)
                    {
                        std::string sub(_sub);
                        keyPageIgnoreTables->erase(sub);
                    }
                }
                keyPageIgnoreTables->insert(
                    {std::string(ledger::SYS_CODE_BINARY), std::string(ledger::SYS_CONTRACT_ABI)});
            }
            STORAGE_LOG(TRACE) << LOG_KV("keyPageSize", m_keyPageSize)
                               << LOG_KV("compatibilityVersion", compatibilityVersion)
                               << LOG_KV("keyPageIgnoreTables size",
                                      keyPageIgnoreTables == nullptr ? 0 :
                                                                       keyPageIgnoreTables->size());
            return std::make_shared<bcos::storage::KeyPageStorage>(storage, setRowWithDirtyFlag,
                m_keyPageSize, compatibilityVersion, keyPageIgnoreTables, ignoreNotExist);
        }

        // Pass useHashV310 flag to hash() insted of compatibilityVersion
        return std::make_shared<bcos::storage::StateStorage>(storage, setRowWithDirtyFlag);
    }

private:
    size_t m_keyPageSize;
};

}  // namespace bcos::storage
