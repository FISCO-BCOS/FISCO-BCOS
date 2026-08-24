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
 * @file AccountStorageRoot.h
 * @brief One account's Ethereum storage-trie root, computed from the flat KV state
 */
#pragma once

#include "Classify.h"
#include "Constants.h"
#include "HashBuilder.h"
#include "StorageValueCodec.h"
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace bcos::ledger::mpt
{

/// The Ethereum storage-trie root of ONE account, built from every live storage slot the flat
/// state currently holds for it. Unlike MPTBuilder's per-block path, which merges a change-set
/// onto a parent trie version and needs the MPT node store, this reads only the flat KV and
/// rebuilds the trie from scratch: it answers "what is this account's storage root right now"
/// for a caller that keeps no trie of its own.
///
/// Its one production caller today is the OP Stack Isthmus header field withdrawalsRoot, which
/// the spec defines as the storage root of the L2ToL1MessagePasser predeploy
/// (op-geth params/protocol_params.go:31, validation side block_validator.go:190-198). The
/// function itself knows nothing about OP — the caller supplies the address.
///
/// Construction (identical to MPTBuilder's storage level, and to bcos::evm::accountStorageRoot
/// on the evmone side): a secure trie whose key is slotKeyHash(slot) = keccak256(32-byte slot
/// key) and whose value is encodeStorageValue(value) = RLP of the value with leading zero bytes
/// trimmed. Slots that trim to nothing (an all-zero value) are NOT part of the trie — zero is
/// Ethereum's "the slot does not exist" — and neither are tombstoned rows. An account with no
/// live slot yields emptyRootHash(), not a zero h256.
///
/// The scan is a prefix range over @p flatView seeded with RANGE_SEEK, so it touches this
/// account's rows only, not the whole state. It reads through the view, so a slot written
/// earlier in the same block (the fork view's mutable layer) shadows the committed value — the
/// merge-sort iterator returns the highest-priority layer's row for a duplicated key
/// (MultiLayerStorage.h View::Iterator::next).
///
/// Requires every layer of @p flatView to yield keys in ascending order from the seek point,
/// which is what lets the loop stop at the first row of the next table. Production satisfies
/// this — the delta layer is a single-bucket ORDERED MemoryStorage and the backend a RocksDB
/// cursor. A CONCURRENT MemoryStorage does NOT: its Iterator seeks bucket 0 only and then
/// walks the remaining buckets from their start (MemoryStorage.h Iterator::seek / next), so a
/// scan over one would stop early and return a partial root. The same precondition already
/// governs the two other prefix scans over an account table, EthereumState::
/// clearAccountStorage and Storage2State::fetchAllStorage.
///
/// Cost: O(live slots of this account) per call, with no incremental reuse across blocks. For
/// the L2ToL1MessagePasser that count is the number of withdrawals the chain has ever initiated
/// (currently zero, since no L2->L1 message path is wired). Making this incremental belongs with
/// the MPT work that owns a persistent trie per account, not here.
///
/// @param flatView an executor-shaped flat state (StateKey rows) supporting a seeded range
/// @param addr     the account whose storage trie is wanted
template <class FlatView>
bcos::task::Task<bcos::h256> accountStorageRootFromFlat(
    FlatView& flatView, bcos::Address const& addr)
{
    auto const table = accountTableName(addr);
    bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher hasher;
    std::map<bcos::h256, bcos::bytes> entries;

    auto iterator = co_await bcos::storage2::range(flatView, bcos::storage2::RANGE_SEEK,
        bcos::executor_v1::StateKey{table, std::string_view{}});
    while (auto keyValue = co_await iterator.next())
    {
        auto const& [stateKey, dataValue] = *keyValue;
        bcos::executor_v1::StateKeyView const keyView{stateKey};
        if (keyView.m_table != table)
        {
            // The seek landed at or before this account's first row; a different table name
            // means the prefix range is over.
            break;
        }
        if (classifyRowKey(keyView.m_key) != RowKind::StorageSlot)
        {
            // nonce / balance / codeHash / code / BCOS extension rows: none belong to the
            // storage trie. Unlike MPTBuilder this does not throw on an unclassified field —
            // this function commits nothing and forks nothing, so an unknown row is simply
            // not a storage slot.
            continue;
        }
        // A tombstoned (deleted) or absent row must not resurrect the slot.
        auto const* entry = std::get_if<bcos::storage::Entry>(std::addressof(dataValue));
        if (entry == nullptr)
        {
            continue;
        }
        std::string_view const raw = entry->get();
        auto encoded = encodeStorageValue(
            bcos::bytesConstRef(reinterpret_cast<bcos::byte const*>(raw.data()), raw.size()));
        if (encoded.empty())
        {
            continue;  // all-zero value: the slot is not in the trie
        }
        auto const slot = bcos::h256(bcos::bytesConstRef(
            reinterpret_cast<bcos::byte const*>(keyView.m_key.data()), keyView.m_key.size()));
        entries.emplace(slotKeyHash(slot, hasher), std::move(encoded));
    }

    // computeTrieRoot over an empty map is emptyRootHash() by construction, which is exactly
    // the "no live slots" answer Ethereum expects.
    co_return computeTrieRoot(entries).root;
}

}  // namespace bcos::ledger::mpt
