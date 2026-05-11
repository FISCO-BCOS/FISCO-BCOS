/*
 *  Copyright (C) 2022 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "StateStorageFactory.h"

using namespace bcos::storage;

StateStorageFactory::StateStorageFactory(size_t keyPageSize) : m_keyPageSize(keyPageSize) {}

StateStorageInterface::Ptr StateStorageFactory::createStateStorage(
    bcos::storage::StorageInterface::Ptr storage, uint32_t compatibilityVersion,
    bool setRowWithDirtyFlag, bool ignoreNotExist,
    std::shared_ptr<std::set<std::string, std::less<>>> const& keyPageIgnoreTables)
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
                for (const auto& subPath : tool::FS_ROOT_SUBS)
                {
                    std::string sub(subPath);
                    keyPageIgnoreTables->erase(sub);
                }
            }
            keyPageIgnoreTables->insert(
                {std::string(ledger::SYS_CODE_BINARY), std::string(ledger::SYS_CONTRACT_ABI)});
        }
        STORAGE_LOG(TRACE) << LOG_KV("keyPageSize", m_keyPageSize)
                           << LOG_KV("compatibilityVersion", compatibilityVersion)
                           << LOG_KV("keyPageIgnoreTables size",
                                  keyPageIgnoreTables == nullptr ? 0 : keyPageIgnoreTables->size());
        return std::make_shared<bcos::storage::KeyPageStorage>(storage, setRowWithDirtyFlag,
            m_keyPageSize, compatibilityVersion, keyPageIgnoreTables, ignoreNotExist);
    }

    return std::make_shared<bcos::storage::StateStorage>(storage, setRowWithDirtyFlag);
}