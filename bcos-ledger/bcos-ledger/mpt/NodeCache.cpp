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
 * @file NodeCache.cpp
 * @brief LRU+CONCURRENT cache mapping node hash -> RLP-encoded MPT node (spec §5.1)
 */
#include "NodeCache.h"

namespace bcos::ledger::mpt
{
NodeCache::NodeCache(size_t capacity) : m_inner(/*buckets=*/0, static_cast<int64_t>(capacity)) {}

bcos::task::Task<std::optional<bcos::bytes>> NodeCache::get(bcos::h256 const& key)
{
    co_return co_await bcos::storage2::readOne(m_inner, key);
}

bcos::task::Task<void> NodeCache::put(bcos::h256 const& key, bcos::bytes value)
{
    co_await bcos::storage2::writeOne(m_inner, key, std::move(value));
}
}  // namespace bcos::ledger::mpt
