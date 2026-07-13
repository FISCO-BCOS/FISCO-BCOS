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
 * @file MPTBuilder.h
 * @brief Commit-time MPT entry point — subsequent-touch, first-touch and tombstone paths over
 *        the block's fork view (spec §5.2–§5.4, Revisions 2026-07-09/09b)
 */
#pragma once

#include "Account.h"
#include "Classify.h"
#include "Errors.h"
#include "FlatToMPT.h"
#include "HashBuilder.h"
#include "MPTDeltaLayer.h"
#include "MPTReadView.h"
#include "StorageValueCodec.h"
// Named directly for the m_hasher member (also reachable transitively).
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/throw_exception.hpp>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace bcos::ledger::mpt
{

/// Builds the post-block MPT from the block's fork view on top of the parent block's state root
/// (spec §5.2, Revision 2026-07-09b): for each touched account the builder prefix-ranges the
/// view's top mutable layer — the block's flat delta — and reads every changed Entry in place;
/// there is no intermediate value-copy layer (design3 §4.1 rule 6).
///
/// Covers all three per-account shapes (spec §5.3), decided from the data itself rather than
/// classify-time flags:
///  - tombstone: the delta holds all three core-field rows (nonce/balance/codeHash) deleted —
///    the SELFDESTRUCT convention — and the account leaf is removed;
///  - first-touch: the parent MPT has no leaf (readAccount nullopt). The storage trie starts
///    from emptyRootHash() and only holds THIS BLOCK's written slots — cold flat slots are never
///    back-filled (slot-level commitment, spec §4.2); unwritten core fields come from the flat
///    KV via one O(1) metadata read (FlatToMPT.h);
///  - subsequent-touch: the parent leaf is the baseline and the storage trie updates
///    incrementally.
///
/// Keccak-pinned like commitTrie itself (its from-empty stateless core is not yet
/// hasher-parameterized); the m_hasher member exists only to reuse one hash context across the
/// block's slot-key transforms.
///
/// @tparam Storage the trie-node storage (storage2 ReadWriteStorage over h256 → RLP bytes):
/// commitTrie merge-reads prior-version nodes through it, and buildAndCollect batch-flushes the
/// block's aggregated newNodes into it once at the end.
template <bcos::storage2::ReadWriteStorage<bcos::h256, bcos::bytes> Storage>
class MPTBuilder
{
public:
    /// @param storage         node storage shared by reads and the end-of-build flush.
    /// @param parentStateRoot the parent block's MPT state root (emptyRootHash() = empty state).
    MPTBuilder(Storage& storage, bcos::h256 parentStateRoot)
      : m_storage(storage), m_parentStateRoot(parentStateRoot)
    {}

    /// Apply the block's flat delta to the MPT and return the new state root plus the node
    /// delta.
    ///
    /// @param flatView        the block's fork of the flat MultiLayerStorage (the same view
    ///                        coCommitBlock forks for prewrite): its top mutable layer is the
    ///                        block's delta, ranged per account; reads for first-touch metadata
    ///                        go through the whole view (delta first, then parent state).
    /// @param touchedAccounts extractTouchedAccounts(mutableStorage(flatView)) — passed in
    ///                        rather than recomputed so the caller can log/meter the set.
    /// @param l2Mode          scenario B (Ethereum-compatible chain): a BCOS extension row in
    ///                        the delta throws UnexpectedBCOSFieldInL2; scenario A skips it.
    /// @throws MPTInvariantViolation on a deleted core-field row outside a tombstone
    ///         (spec §5.4 treats that as an error).
    bcos::task::Task<MPTDeltaLayer> buildAndCollect(
        auto& flatView, std::vector<bcos::Address> const& touchedAccounts, bool l2Mode)
    {
        MPTDeltaLayer output;
        MPTReadView<Storage> parentView(m_storage.get(), m_parentStateRoot);
        // The account-trie change-set, committed once through commitTrie at the end. std::map
        // on purpose (commitTrie's signature requires it): the type itself guarantees ascending
        // iteration and unique keys, and h256 lexicographic order IS 64-nibble path order — the
        // ordering the trie build relies on.
        std::map<bcos::h256, std::optional<bcos::bytes>> accountChanges;

        // Per account: commit its storage trie (nodes go into output) and record its new leaf
        // encoding — or its removal — into accountChanges.
        for (auto const& addr : touchedAccounts)
        {
            co_await buildOneAccount(addr, flatView, parentView, l2Mode, accountChanges, output);
        }

        auto merged = co_await commitTrie(m_storage.get(), m_parentStateRoot, accountChanges);
        output.stateRoot = merged.root;
        mergeNodeDelta(std::move(merged), output);

        // MPTDeltaLayer aggregates one commitTrie result per touched storage trie plus the
        // account trie. Unlike a single mergeTrie() result the union is not disjoint by
        // construction: identical RLP encodings hash identically ACROSS tries, so a node one
        // account's rebuild obsoletes can byte-match a node another account's build emits.
        // Re-establish disjointness the way mergeTrie() does (end-subtraction): the node stays
        // in newNodes — it is flushed and referenced by the new version — and leaves the prune
        // ledger; the subtracted hashes move to intraBlockObsoleted rather than vanishing, so
        // the pruning spec keeps full information.
        for (auto const& [hash, raw] : output.newNodes)
        {
            if (output.obsoletedNodes.erase(hash) != 0U)
            {
                output.intraBlockObsoleted.insert(hash);
            }
        }

        // One batched flush for the whole block (spec §5.4): nothing inside this build reads a
        // node it produced — storage-trie merges read parent-version nodes only, and the account
        // trie never dereferences a storage root.
        co_await flushTrieNodes(m_storage.get(), output.newNodes);
        co_return output;
    }

private:
    /// One core-field row's fate in the block's delta layer.
    template <typename ValueT>
    struct CoreFieldRow
    {
        bool deleted = false;
        std::optional<ValueT> value;  ///< engaged when the block wrote the row

        /// Deleted outside a tombstone means the delta is not a shape the executor produces
        /// (spec §5.4) — fail loudly instead of guessing a field value.
        void requireNotDeleted() const
        {
            if (deleted)
            {
                BOOST_THROW_EXCEPTION(MPTInvariantViolation{} << bcos::errinfo_comment(
                                          "MPTBuilder: core-field row deleted outside a "
                                          "tombstone"));
            }
        }
    };

    bcos::task::Task<void> buildOneAccount(bcos::Address const& addr, auto& flatView,
        MPTReadView<Storage> const& parentView, bool l2Mode,
        std::map<bcos::h256, std::optional<bcos::bytes>>& accountChanges, MPTDeltaLayer& output)
    {
        auto const table = accountTableName(addr);
        CoreFieldRow<bcos::u256> nonceRow;
        CoreFieldRow<bcos::u256> balanceRow;
        CoreFieldRow<bcos::h256> codeHashRow;
        // This account's storage-trie change-set: slotKeyHash → RLP(value) or delete.
        std::map<bcos::h256, std::optional<bcos::bytes>> storageChanges;

        auto& flatDelta = mutableStorage(flatView);
        auto iterator = co_await bcos::storage2::range(
            flatDelta, bcos::storage2::RANGE_SEEK, executor_v1::StateKeyView{table, {}});
        while (true)
        {
            auto keyValue = co_await iterator.next();
            if (!keyValue)
            {
                break;
            }
            auto const& [stateKey, dataValue] = *keyValue;
            executor_v1::StateKeyView const keyView{stateKey};
            if (keyView.m_table != table)
            {
                break;  // rows are (table, key)-ordered: past this account's table
            }

            // A row is "deleted" through either signal: storage-level logical deletion
            // (removeSome on the LOGICAL_DELETION mutable layer) or an Entry written with
            // status DELETED (spec §5.2's selfdestruct convention). NOT_EXISTS never sits in a
            // container; skip it defensively.
            bcos::storage::Entry const* entry =
                std::get_if<bcos::storage::Entry>(std::addressof(dataValue));
            bool const deleted =
                std::holds_alternative<bcos::storage2::DELETED_TYPE>(dataValue) ||
                (entry != nullptr && entry->status() == bcos::storage::Entry::DELETED);
            if (entry == nullptr && !deleted)
            {
                continue;
            }

            // Not a double dispatch: the string compares happen once inside classifyRowKey and
            // the switch is a jump on the enum. Kept as enum + switch (no default) so adding a
            // RowKind makes -Wswitch flag this spot, and the key-format knowledge stays
            // unit-tested in Classify.h.
            switch (classifyRowKey(keyView.m_key))
            {
            case RowKind::Nonce:
                nonceRow.deleted = deleted;
                if (!deleted)
                {
                    nonceRow.value = detail::entryToU256(*entry);
                }
                break;
            case RowKind::Balance:
                balanceRow.deleted = deleted;
                if (!deleted)
                {
                    balanceRow.value = detail::entryToU256(*entry);
                }
                break;
            case RowKind::CodeHash:
                codeHashRow.deleted = deleted;
                if (!deleted)
                {
                    codeHashRow.value = detail::entryToH256(*entry);
                }
                break;
            case RowKind::StorageSlot:
            {
                auto const slot = bcos::h256(
                    bcos::bytesConstRef(reinterpret_cast<bcos::byte const*>(keyView.m_key.data()),
                        keyView.m_key.size()));
                auto const keyHash = slotKeyHash(slot, m_hasher);
                if (deleted)
                {
                    storageChanges[keyHash] = std::nullopt;
                    break;
                }
                // A value that trims to nothing (all-zero slot) also leaves the trie.
                std::string_view const raw = entry->get();
                auto encoded = encodeStorageValue(bcos::bytesConstRef(
                    reinterpret_cast<bcos::byte const*>(raw.data()), raw.size()));
                if (encoded.empty())
                {
                    storageChanges[keyHash] = std::nullopt;
                }
                else
                {
                    storageChanges[keyHash] = std::move(encoded);
                }
                break;
            }
            case RowKind::Code:
                break;  // represented by the paired codeHash row
            case RowKind::BcosExtension:
                if (l2Mode)
                {
                    BOOST_THROW_EXCEPTION(
                        UnexpectedBCOSFieldInL2{} << bcos::errinfo_comment(
                            "MPTBuilder: BCOS extension field present in L2 "
                            "(Ethereum-compatible) mode; key=" +
                            std::string{keyView.m_table} + ":" + std::string{keyView.m_key}));
                }
                break;  // native BCOS chain: not part of the Ethereum 4-tuple
            }
        }

        // Tombstone (spec §5.3 path 3): all three core rows deleted = SELFDESTRUCT. Slot rows
        // in the same delta are ignored — the whole leaf goes away.
        if (nonceRow.deleted && balanceRow.deleted && codeHashRow.deleted)
        {
            // Record the prior storage root for future pathdb pruning — the full subtree walk
            // is deferred to the pruning spec, the root hash is the ledger entry. Removing an
            // absent leaf is a legal no-op (commitTrie treats it as such).
            auto prior = co_await parentView.readAccount(addr);
            if (prior && prior->storageRoot != emptyRootHash())
            {
                output.obsoletedNodes.insert(prior->storageRoot);
            }
            accountChanges[accountKeyHash(addr)] = std::nullopt;
            co_return;
        }
        nonceRow.requireNotDeleted();
        balanceRow.requireNotDeleted();
        codeHashRow.requireNotDeleted();

        // Path selection is the readAccount result itself (Revision 2026-07-09b): a parent leaf
        // means subsequent-touch, none means first-touch — no flag to disagree with.
        auto baseline = co_await parentView.readAccount(addr);
        Account updated;
        bcos::h256 priorStorageRoot = emptyRootHash();
        if (baseline)
        {
            priorStorageRoot = baseline->storageRoot;
            updated = std::move(*baseline);
        }
        else if (!nonceRow.value || !balanceRow.value || !codeHashRow.value)
        {
            // First-touch fields the block left unwritten have no parent leaf to fall back on:
            // one O(1) flat metadata read through the fork view (spec §5.3 path 2).
            auto meta = co_await readFlatAccountMeta(flatView, addr);
            updated.nonce = meta.nonce;
            updated.balance = meta.balance;
            updated.codeHash = meta.codeHash;
        }

        if (!storageChanges.empty())
        {
            // First-touch: priorStorageRoot == emptyRootHash() — the trie holds exactly this
            // block's written slots (spec §4.2); deletes of never-written slots are no-ops.
            auto merged = co_await commitTrie(m_storage.get(), priorStorageRoot, storageChanges);
            updated.storageRoot = merged.root;
            mergeNodeDelta(std::move(merged), output);
        }
        else
        {
            updated.storageRoot = priorStorageRoot;
        }

        if (nonceRow.value)
        {
            updated.nonce = *nonceRow.value;
        }
        if (balanceRow.value)
        {
            updated.balance = *balanceRow.value;
        }
        if (codeHashRow.value)
        {
            updated.codeHash = *codeHashRow.value;
        }
        accountChanges[accountKeyHash(addr)] = updated.encode();
    }

    std::reference_wrapper<Storage> m_storage;
    bcos::h256 m_parentStateRoot;
    /// One hash context per builder, reused for every slot-key transform of this block
    /// (slotKeyHash injection form). Single-threaded, like buildAndCollect itself.
    bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher m_hasher;
};

}  // namespace bcos::ledger::mpt
