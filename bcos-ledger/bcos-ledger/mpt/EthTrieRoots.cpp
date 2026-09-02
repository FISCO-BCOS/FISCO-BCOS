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

    // Key each item by its RLP-encoded index, packed into ONE flat buffer (an
    // rlp(uint64) key is at most 9 bytes) and referenced from the ref-pair vector.
    // The scratch `key` is hoisted out of the loop and cleared each iteration — a
    // cleared std::vector keeps its capacity, so the per-key encode reuses one
    // buffer instead of heap-allocating per item. Two O(N) index vectors remain
    // (keySpans + keyRefs), plus one nibble-path allocation per entry inside
    // computeRawTrieRootImpl. The values stay VIEWS into the caller's `items`,
    // which outlive the call; computeRawTrieRoot sorts by ENCODED KEY BYTES
    // internally (rlp(0)=0x80 > rlp(1)=0x01, so NOT numeric index order), so no
    // ordering or value copying is needed here. The root-only entry point is used
    // deliberately: the tx/receipt/withdrawal tries are never persisted, so
    // accumulating the node map would only be thrown away.
    bcos::bytes keyBytes;
    keyBytes.reserve(items.size() * 2);
    std::vector<std::pair<size_t, size_t>> keySpans;  // (offset, length) into keyBytes
    keySpans.reserve(items.size());
    bcos::bytes key;
    for (size_t i = 0; i < items.size(); ++i)
    {
        key.clear();
        codec::rlp::encode(key, static_cast<uint64_t>(i));
        keySpans.emplace_back(keyBytes.size(), key.size());
        keyBytes.insert(keyBytes.end(), key.begin(), key.end());
    }
    std::vector<std::pair<bcos::bytesConstRef, bcos::bytesConstRef>> keyRefs;
    keyRefs.reserve(items.size());
    for (size_t i = 0; i < items.size(); ++i)
    {
        auto [offset, length] = keySpans[i];
        keyRefs.emplace_back(bcos::bytesConstRef(keyBytes.data() + offset, length), items[i]);
    }
    return computeRawTrieRoot(keyRefs);
}

}  // namespace bcos::ledger::mpt
