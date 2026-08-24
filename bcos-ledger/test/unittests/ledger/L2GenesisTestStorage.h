/**
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file L2GenesisTestStorage.h
 * @brief In-memory StateStorage for L2 genesis tests. Mirrors the MockStorage in
 *        LedgerTest.cpp: base StateStorage leaves setRows() unimplemented, which
 *        prewriteBlockToStorage() needs, so we override it here. Distinct class
 *        name avoids ODR clashes under UNITY_BUILD.
 */
#pragma once

#include "bcos-framework/storage/StorageInterface.h"
#include <bcos-table/src/StateStorage.h>

namespace bcos::test
{
class L2GenesisTestStorage : public virtual storage::StateStorage
{
public:
    L2GenesisTestStorage(std::shared_ptr<storage::StorageInterface> prev)
      : storage::StateStorageInterface(prev), storage::StateStorage(prev, false)
    {}

    bcos::Error::Ptr setRows(std::string_view tableName,
        ::ranges::any_view<std::string_view,
            ::ranges::category::random_access | ::ranges::category::sized>
            keys,
        ::ranges::any_view<std::string_view,
            ::ranges::category::random_access | ::ranges::category::sized>
            values) override
    {
        for (size_t i = 0; i < keys.size(); ++i)
        {
            storage::Entry e;
            e.set(std::string(values[i]));
            asyncSetRow(tableName, keys[i], e, [](Error::UniquePtr) {});
        }
        return nullptr;
    }
};

inline std::shared_ptr<L2GenesisTestStorage> makeL2GenesisTestStorage()
{
    auto memoryStorage = std::make_shared<storage::StateStorage>(nullptr, false);
    memoryStorage->setEnableTraverse(true);
    auto storage = std::make_shared<L2GenesisTestStorage>(memoryStorage);
    storage->setEnableTraverse(true);
    return storage;
}
}  // namespace bcos::test
