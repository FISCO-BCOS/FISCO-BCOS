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

#include "StateStorageFactory.h"
#include "KeyPageStorage.h"
#include "StateStorage.h"
#include "StateStorageInterface.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-tool/BfsFileFactory.h"


using namespace bcos::storage;
using namespace bcos::ledger;
using namespace bcos::tool;
using namespace bcos::protocol;

bcos::storage::StateStorageInterface::Ptr StateStorageFactory::createStateStorage(
    bcos::storage::StorageInterface::Ptr storage, uint32_t compatibilityVersion)
{
    STORAGE_LOG(TRACE) << LOG_KV("compatibilityVersion, ", compatibilityVersion)
                       << LOG_KV("protocol::BlockVersion::V3_1_VERSION", (uint32_t)protocol::BlockVersion::V3_1_VERSION)
                       << LOG_KV("keyPageSize", m_keyPageSize);

    auto keyPageIgnoreTables = std::make_shared<std::set<std::string, std::less<>>>(
        std::initializer_list<std::set<std::string, std::less<>>::value_type>{
            std::string(bcos::ledger::SYS_CONFIG),
            std::string(bcos::ledger::SYS_CONSENSUS),
            bcos::storage::FS_ROOT,
            bcos::storage::FS_APPS,
            bcos::storage::FS_USER,
            bcos::storage::FS_SYS_BIN,
            bcos::storage::FS_USER_TABLE,
            bcos::storage::StorageInterface::SYS_TABLES,
        });
    STORAGE_LOG(TRACE) << LOG_KV("keyPageIgnoreTables Size", keyPageIgnoreTables->size());

    if( m_keyPageSize > 0){
        if(compatibilityVersion >= (uint32_t)protocol::BlockVersion::V3_1_VERSION)
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
                           << LOG_KV("keyPageIgnoreTables size",keyPageIgnoreTables->size());
        return std::make_shared<bcos::storage::KeyPageStorage>
            (storage, m_keyPageSize, compatibilityVersion, keyPageIgnoreTables, true);
    }
    return std::make_shared<bcos::storage::StateStorage>(storage);
}