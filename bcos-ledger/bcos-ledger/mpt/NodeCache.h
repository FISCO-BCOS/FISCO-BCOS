/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file NodeCache.h
 * @brief LRU+CONCURRENT cache mapping node hash -> RLP-encoded MPT node (spec §5.1)
 */
#pragma once

#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <optional>

namespace bcos::ledger::mpt
{
/// Bounded cache for decoded/encoded MPT nodes. Keys are 32-byte node hashes,
/// values are the RLP encoding of the node. Wraps bcos-framework MemoryStorage
/// with CONCURRENT (thread-safe sharded buckets) and LRU (capacity-bounded)
/// attributes so a hot working set stays resident while cold nodes are evicted.
class NodeCache
{
public:
    /// @param capacity LRU byte budget (sum of key+value sizes) before eviction.
    explicit NodeCache(size_t capacity = 64 * 1024);

    /// Returns the cached RLP encoding for @p key, or nullopt on a miss.
    bcos::task::Task<std::optional<bcos::bytes>> get(bcos::h256 const& key);

    /// Inserts or overwrites the RLP encoding for @p key.
    bcos::task::Task<void> put(bcos::h256 const& key, bcos::bytes value);

private:
    using Inner = bcos::storage2::memory_storage::MemoryStorage<bcos::h256, bcos::bytes,
        bcos::storage2::memory_storage::Attribute::CONCURRENT |
            bcos::storage2::memory_storage::Attribute::LRU>;
    Inner m_inner;
};
}  // namespace bcos::ledger::mpt
