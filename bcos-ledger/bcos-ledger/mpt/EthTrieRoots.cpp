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
 * @file EthTrieRoots.cpp
 * @brief EthTrieRoots — index-keyed (non-secure) trie root computation
 * @date 2026/8/18
 */
#include "EthTrieRoots.h"
#include <bcos-codec/rlp/RLPEncode.h>
#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace bcos::ledger::mpt
{

bcos::h256 computeIndexedTrieRoot(std::span<bcos::bytesConstRef const> items)
{
    if (items.empty())
    {
        return emptyRootHash();
    }

    // Key each item by its RLP-encoded index, then sort ascending by ENCODED KEY BYTES — the
    // raw-key trie contract (rlp(0)=0x80 > rlp(1)=0x01, so NOT numeric index order).
    std::vector<std::pair<bcos::bytes, bcos::bytes>> keyed;
    keyed.reserve(items.size());
    for (size_t i = 0; i < items.size(); ++i)
    {
        bcos::bytes key;
        codec::rlp::encode(key, static_cast<uint64_t>(i));
        keyed.emplace_back(std::move(key), items[i].toBytes());
    }
    std::sort(keyed.begin(), keyed.end(),
        [](auto const& lhs, auto const& rhs) { return lhs.first < rhs.first; });

    std::vector<std::pair<bcos::bytesConstRef, bcos::bytesConstRef>> sorted;
    sorted.reserve(keyed.size());
    for (auto const& [key, value] : keyed)
    {
        sorted.emplace_back(bcos::ref(key), bcos::ref(value));
    }
    return computeTrieRootFromRawKeys(sorted).root;
}

}  // namespace bcos::ledger::mpt
