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
// Named directly for the hash context buildAndCollect owns (also reachable transitively).
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
#include <ranges>
#include <string>
#include <utility>
#include <variant>

namespace bcos::ledger::mpt
{

namespace detail
{

/// Everything that stays fixed across the block's accounts. Passed by reference so the per-account
/// helpers carry only what varies.
template <typename Storage>
struct BuildContext
{
    Storage& nodeStorage;                    ///< trie-node storage: merge reads + final flush
    MPTReadView<Storage> const& parentView;  ///< the parent block's MPT, for baseline lookups
    bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher& hasher;  ///< reused slot-key context
    bool l2Mode;  ///< scenario B: a BCOS extension row is an error rather than a skip
};

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
                                      "MPTBuilder: core-field row deleted outside a tombstone"));
        }
    }
};

/// One account's rows, accumulated as the block's delta is scanned. Reset between accounts.
struct AccountRows
{
    CoreFieldRow<bcos::u256> nonce;
    CoreFieldRow<bcos::u256> balance;
    CoreFieldRow<bcos::h256> codeHash;
    /// This account's storage-trie change-set: slotKeyHash → RLP(value) or delete.
    std::map<bcos::h256, std::optional<bcos::bytes>> storageChanges;
    /// Set once a row carrying Ethereum state (core field or storage slot) is seen. Code and BCOS
    /// extension rows leave it false: an account touched only by those has no Ethereum state
    /// change, and finalizeAccount must not invent a leaf for it (see there).
    ///
    /// This closes ONE member of the EIP-161 empty-account class, not the class. A block that
    /// writes a zero balance to an otherwise-untouched account still produces {0, 0,
    /// emptyCodeHash, emptyRoot}, because EIP-158/161 empty-account clearing is not implemented
    /// (see finalizeAccount's tombstone comment).
    bool sawEthereumRow = false;
};

/// Fold one row of the block's delta into @p rows. Synchronous: classification and value decoding
/// touch nothing but the row itself.
template <typename Storage>
void accumulateRow(BuildContext<Storage> const& context, AccountRows& rows,
    executor_v1::StateKeyView const& keyView, auto const& dataValue)
{
    // Deletion is storage-level logical deletion (removeSome on the LOGICAL_DELETION mutable
    // layer) — the only deletion the storage layer definitively expresses. NOT_EXISTS never
    // sits in a container; skip it defensively.
    bcos::storage::Entry const* entry =
        std::get_if<bcos::storage::Entry>(std::addressof(dataValue));
    bool const deleted = std::holds_alternative<bcos::storage2::DELETED_TYPE>(dataValue);
    if (entry == nullptr && !deleted)
    {
        return;
    }

    // Not a double dispatch: the string compares happen once inside classifyRowKey and
    // the switch is a jump on the enum. Kept as enum + switch (no default) so adding a
    // RowKind makes -Wswitch flag this spot, and the key-format knowledge stays
    // unit-tested in Classify.h.
    auto const kind = classifyRowKey(keyView.m_key);
    // Stated positively — these four ARE the Ethereum state. The complement (code, known BCOS
    // extensions, unclassified fields) must leave the flag alone.
    if (kind == RowKind::Nonce || kind == RowKind::Balance || kind == RowKind::CodeHash ||
        kind == RowKind::StorageSlot)
    {
        rows.sawEthereumRow = true;
    }
    switch (kind)
    {
    case RowKind::Nonce:
        rows.nonce.deleted = deleted;
        if (!deleted)
        {
            rows.nonce.value = detail::entryToU256(*entry);
        }
        break;
    case RowKind::Balance:
        rows.balance.deleted = deleted;
        if (!deleted)
        {
            rows.balance.value = detail::entryToU256(*entry);
        }
        break;
    case RowKind::CodeHash:
        rows.codeHash.deleted = deleted;
        if (!deleted)
        {
            rows.codeHash.value = detail::entryToH256(*entry);
        }
        break;
    case RowKind::StorageSlot:
    {
        auto const slot = bcos::h256(bcos::bytesConstRef(
            reinterpret_cast<bcos::byte const*>(keyView.m_key.data()), keyView.m_key.size()));
        auto const keyHash = slotKeyHash(slot, context.hasher);
        if (deleted)
        {
            rows.storageChanges[keyHash] = std::nullopt;
            break;
        }
        // A value that trims to nothing (all-zero slot) also leaves the trie.
        std::string_view const raw = entry->get();
        auto encoded = encodeStorageValue(
            bcos::bytesConstRef(reinterpret_cast<bcos::byte const*>(raw.data()), raw.size()));
        if (encoded.empty())
        {
            rows.storageChanges[keyHash] = std::nullopt;
        }
        else
        {
            rows.storageChanges[keyHash] = std::move(encoded);
        }
        break;
    }
    case RowKind::Code:
        // Code never enters the trie in any Ethereum revision — the account leaf commits to
        // codeHash only, so a code change reaches the MPT through its paired codeHash row. That
        // holds under EIP-7702 too: a delegation designator (0xef0100 || address) is just code,
        // and its keccak IS the codeHash, so an EOA gaining or clearing a delegation shows up in
        // the leaf's codeHash field and nowhere else.
        //
        // As of today the v1 executor does not even write this row — EVMAccount::setCode puts
        // the bytes in the global SYS_CODE_BINARY table keyed by codeHash — so the case fires
        // only on scenario-A chains whose legacy executor left code rows in the account table.
        // Whether a future 7702 implementation routes designators through this row does not
        // change the rule; it only changes how often this case is taken.
        break;
    case RowKind::BcosExtension:
        if (context.l2Mode)
        {
            BOOST_THROW_EXCEPTION(
                UnexpectedBCOSFieldInL2{} << bcos::errinfo_comment(
                    "MPTBuilder: BCOS extension field present in L2 "
                    "(Ethereum-compatible) mode; key=" +
                    std::string{keyView.m_table} + ":" + std::string{keyView.m_key}));
        }
        break;  // native BCOS chain: not part of the Ethereum 4-tuple
    case RowKind::UnknownField:
        // Not in KNOWN_BCOS_EXTENSION_FIELDS, so nobody has judged whether it carries Ethereum
        // state. Skipping it would drop that state from the commitment with no signal, so throw
        // in BOTH modes and make the judgement happen once, when the field is introduced.
        BOOST_THROW_EXCEPTION(UnknownAccountRowField{} << bcos::errinfo_comment(
                                  "MPTBuilder: unclassified account row field; add it to "
                                  "KNOWN_BCOS_EXTENSION_FIELDS if it is not Ethereum state. key=" +
                                  std::string{keyView.m_table} + ":" + std::string{keyView.m_key}));
    }
}

/// Turn one account's accumulated rows into its new leaf encoding (or its removal), committing
/// that account's storage trie on the way. Appends to @p accountChanges and @p output.
template <typename Storage>
bcos::task::Task<void> finalizeAccount(BuildContext<Storage>& context, bcos::Address const& address,
    AccountRows const& rows, auto& flatView,
    std::map<bcos::h256, std::optional<bcos::bytes>>& accountChanges, MPTDeltaLayer& output)
{
    // Nothing in this run carried Ethereum state — the account was touched only by code and/or
    // BCOS extension rows. It must not reach the trie: with no parent leaf, the path below would
    // read Yellow Paper defaults from the flat KV (nonce 0, balance 0, emptyCodeHash) and insert
    // an EIP-161 EMPTY ACCOUNT, which no Ethereum implementation keeps in state — a fork. The old
    // classify() got this for free by never creating an AccountDelta for such an account; the
    // single-pass scan has to say it explicitly.
    if (!rows.sawEthereumRow)
    {
        co_return;
    }

    // Tombstone (spec §5.3 path 3): all three core rows deleted = SELFDESTRUCT. Slot rows in the
    // same delta are ignored — this account does not reach the MPT.
    //
    // Under current Ethereum rules this branch's job is to recognize an account that was BORN AND
    // DIED inside this block and keep it out of the trie, NOT to remove a pre-existing leaf.
    // EIP-6780 leaves exactly one case where SELFDESTRUCT still deletes an account — a contract
    // created within the same transaction — and such an account was never in the parent MPT, so
    // the removal below resolves to a no-op. It is still a branch that must exist: CREATE really
    // wrote the three core rows into the delta and SELFDESTRUCT really deleted them (it is not a
    // revert), so the run reaches us as "all three core rows deleted"; without this case
    // requireNotDeleted() would reject a perfectly legal transaction.
    //
    // Removing a pre-existing leaf is the same one line, kept as compatibility headroom: there is
    // no path to it today (6780 spares already-existing contracts; EIP-158/161 empty-account
    // clearing is not implemented) — but if one appears, silently leaving a stale leaf behind
    // would be a fork.
    if (rows.nonce.deleted && rows.balance.deleted && rows.codeHash.deleted)
    {
        // Record the prior storage root for future pathdb pruning — the full subtree walk
        // is deferred to the pruning spec, the root hash is the ledger entry. Removing an
        // absent leaf is a legal no-op (commitTrie treats it as such).
        auto prior = co_await context.parentView.readAccount(address);
        if (prior && prior->storageRoot != emptyRootHash())
        {
            output.obsoletedNodes.insert(prior->storageRoot);
        }
        accountChanges[accountKeyHash(address)] = std::nullopt;
        co_return;
    }
    // No protocol operation removes a core field from a LIVE account, so a lone deletion here is
    // a delta shape the executor should not produce — fail loudly rather than guess a value.
    //
    // codeHash included, and EIP-7702 does not change that. Clearing a delegation does return an
    // EOA to a code-less state, but the standard expresses "code-less" as a VALUE: an account
    // leaf always carries a codeHash and a code-less account's is keccak256(""), so the executor
    // WRITES emptyCodeHash() (handled below like any other written value) and never deletes the
    // row. Resolving a deletion to emptyCodeHash() instead would be unsound both ways: on a
    // subsequent touch it would overwrite the parent leaf's real contract codeHash — the leaf is
    // positive evidence that this account HAS code, unlike the first-touch case where
    // readFlatAccountMeta's missing-row default is an inference from the absence of evidence —
    // and on a first touch it would let an account whose only delta row is a deleted codeHash
    // reach the leaf write below as {0, 0, emptyCodeHash, emptyRoot}, the very EIP-161 empty
    // account the sawEthereumRow guard above exists to keep out.
    rows.nonce.requireNotDeleted();
    rows.balance.requireNotDeleted();
    rows.codeHash.requireNotDeleted();

    // Path selection is the readAccount result itself (Revision 2026-07-09b): a parent leaf
    // means subsequent-touch, none means first-touch — no flag to disagree with.
    auto baseline = co_await context.parentView.readAccount(address);
    Account updated;
    bcos::h256 priorStorageRoot = emptyRootHash();
    if (baseline)
    {
        priorStorageRoot = baseline->storageRoot;
        updated = std::move(*baseline);
    }
    else if (!rows.nonce.value || !rows.balance.value || !rows.codeHash.value)
    {
        // First-touch fields the block left unwritten have no parent leaf to fall back on:
        // one O(1) flat metadata read through the fork view (spec §5.3 path 2).
        auto meta = co_await readFlatAccountMeta(flatView, address);
        updated.nonce = meta.nonce;
        updated.balance = meta.balance;
        updated.codeHash = meta.codeHash;
    }

    if (!rows.storageChanges.empty())
    {
        // First trie level: commit THIS account's storage trie; the new root is embedded in
        // the leaf encoded below, which is how it reaches the account trie.
        // First-touch: priorStorageRoot == emptyRootHash() — the trie holds exactly this
        // block's written slots (spec §4.2); deletes of never-written slots are no-ops.
        auto merged =
            co_await commitTrie(context.nodeStorage, priorStorageRoot, rows.storageChanges);
        updated.storageRoot = merged.root;
        mergeNodeDelta(std::move(merged), output);
    }
    else
    {
        updated.storageRoot = priorStorageRoot;
    }

    if (rows.nonce.value)
    {
        updated.nonce = *rows.nonce.value;
    }
    if (rows.balance.value)
    {
        updated.balance = *rows.balance.value;
    }
    if (rows.codeHash.value)
    {
        updated.codeHash = *rows.codeHash.value;
    }
    accountChanges[accountKeyHash(address)] = updated.encode();
}

}  // namespace detail

/// Build the post-block MPT from the block's fork view on top of the parent block's state root
/// (spec §5.2, Revision 2026-07-09b) and return the new state root plus the node delta.
///
/// Stateless by construction: everything it needs arrives as an argument and nothing outlives the
/// call, so there is no builder object to configure, reuse or mis-sequence (the same reasoning
/// that retired the HashBuilder class — see commitTrie).
///
/// The block's delta is scanned exactly ONCE. Rows are (table, key)-ordered, so one account's rows
/// arrive as a contiguous run: the scan accumulates a run and settles it when the table changes.
/// That ordering was already load-bearing — the previous per-account seek stopped at the first
/// foreign table — so the single pass adds no assumption, it just drops a redundant full scan and
/// N seeks.
///
/// The build mirrors Ethereum's two-level trie-of-tries, bottom-up: each account settles its own
/// STORAGE trie (slotKeyHash → RLP(value)) and embeds the resulting root in its leaf encoding;
/// then ONE account-trie commit over the collected leaves (accountKeyHash → RLP(leaf)) yields the
/// block's state root. All N+1 commitTrie results accumulate into the returned delta; nothing is
/// written until the single flush at the end.
///
/// Per-account shapes are decided from the data itself, not from classify-time flags (spec §5.3):
///  - tombstone: the delta holds all three core-field rows (nonce/balance/codeHash) deleted —
///    the SELFDESTRUCT convention — and the account is kept out of the MPT. Post-EIP-6780 this
///    means an account born and died within this block (see finalizeAccount); removing a
///    pre-existing leaf is the same code path but has no trigger under current rules;
///  - first-touch: the parent MPT has no leaf (readAccount nullopt). The storage trie starts
///    from emptyRootHash() and only holds THIS BLOCK's written slots — cold flat slots are never
///    back-filled (slot-level commitment, spec §4.2); unwritten core fields come from the flat
///    KV via one O(1) metadata read (FlatToMPT.h);
///  - subsequent-touch: the parent leaf is the baseline and the storage trie updates
///    incrementally.
///
/// The leaf is the standard Ethereum four-tuple and nothing else, so new account-model revisions
/// need no handling here as long as they express themselves through it. EIP-7702 is the current
/// example: a delegated EOA's designator is code (out of the trie, committed via its codeHash),
/// the authorization bumps nonce, and a delegated call writes the AUTHORITY's own storage — all
/// three arrive as ordinary rows. Resist adding a 7702-specific branch; if some revision cannot
/// be expressed through the four-tuple, the account encoding is what has to change.
///
/// Keccak-pinned like commitTrie itself (its from-empty stateless core is not yet
/// hasher-parameterized); the local hash context exists only to reuse one hasher across the
/// block's slot-key transforms.
///
/// @todo HasherT — this function, commitTrie, computeTrieRootFromSorted, accountKeyHash and
/// Account's default storageRoot/codeHash all hard-code keccak256. An SM3 deployment has to
/// parameterize every one of them together; doing a subset silently mixes hash functions.
///
/// @tparam Storage the trie-node storage (storage2 ReadWriteStorage over h256 → RLP bytes):
/// commitTrie merge-reads prior-version nodes through it, and the block's aggregated newNodes are
/// batch-flushed into it once at the end.
/// @param nodeStorage     node storage shared by reads and the end-of-build flush.
/// @param parentStateRoot the parent block's MPT state root (emptyRootHash() = empty state).
/// @param flatView        a flat-state view with TWO properties, both required: its top mutable
///                        layer must be exactly this block's delta (that layer, and only it, is
///                        scanned), and reading through the view must resolve to the PARENT
///                        state, because a core field the block never wrote exists nowhere else
///                        (readFlatAccountMeta). A view carrying newer blocks' deltas underneath
///                        would let future writes leak into a first-touch baseline.
///
///                        Wiring note for the caller (PR-14b/PR-18): do NOT expect to obtain this
///                        by forking at commit time. coExecuteBlock builds exactly such a view
///                        (BaselineScheduler.h:355-356) but hands its mutable layer to the
///                        storage stack via pushView before commit runs, and coCommitBlock forks
///                        nothing. A bare fork() there has no mutable layer at all
///                        (NotExistsMutableStorageError); fork() + newMutable() yields an EMPTY
///                        one, which is worse — the scan finds nothing, accountChanges stays
///                        empty, commitTrie short-circuits to priorRoot and the MPT silently
///                        stops advancing.
/// @param l2Mode          scenario B (Ethereum-compatible chain): a KNOWN BCOS extension row in
///                        the delta throws UnexpectedBCOSFieldInL2; scenario A skips it.
/// @throws MPTInvariantViolation on a deleted core-field row outside a tombstone
///         (spec §5.4 treats that as an error).
/// @throws UnknownAccountRowField on an account row whose field name is not classified, in
///         either mode (spec §5.2).
template <bcos::storage2::ReadWriteStorage<bcos::h256, bcos::bytes> Storage>
bcos::task::Task<MPTDeltaLayer> buildAndCollect(
    Storage& nodeStorage, bcos::h256 parentStateRoot, auto& flatView, bool l2Mode)
{
    MPTDeltaLayer output;
    MPTReadView<Storage> const parentView(nodeStorage, parentStateRoot);
    bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher hasher;
    detail::BuildContext<Storage> context{
        .nodeStorage = nodeStorage, .parentView = parentView, .hasher = hasher, .l2Mode = l2Mode};

    // The ACCOUNT trie's change-set: accountKeyHash(addr) → the account's new leaf encoding
    // (nullopt = tombstone removal). Slot changes never appear here — each account digests
    // its slots into a storage-trie root first, and only that root reaches the leaf. std::map
    // on purpose (commitTrie's signature requires it): the type itself guarantees ascending
    // iteration and unique keys, and h256 lexicographic order IS 64-nibble path order — the
    // ordering the trie build relies on.
    std::map<bcos::h256, std::optional<bcos::bytes>> accountChanges;

    // Single pass over the block's delta, grouped by account. Rows arrive (table, key)-ordered,
    // so one account's rows form a contiguous run and a table change marks its end. Scanning
    //     /apps/0a..:balance   /apps/0a..:nonce   /apps/0b..:nonce   /sys/config:x
    // settles account 0a when the /apps/0b.. row arrives, settles 0b when /sys/config arrives,
    // and skips the system row — currentAddress stays unset for any table outside /apps/.
    //
    // currentTable is a view into the row the iterator last yielded: elements live in the delta
    // container, which this read-only scan never mutates, so it stays valid across iterations.
    // Comparison is by content, so a same-named table always matches.
    std::string_view currentTable;
    std::optional<bcos::Address> currentAddress;
    detail::AccountRows rows;

    auto iterator = co_await bcos::storage2::range(mutableStorage(flatView));
    while (true)
    {
        auto keyValue = co_await iterator.next();
        if (!keyValue)
        {
            break;
        }
        auto const& [stateKey, dataValue] = *keyValue;
        executor_v1::StateKeyView const keyView{stateKey};

        if (keyView.m_table != currentTable)
        {
            // Run boundary. Settle the account that just ended (none on the first row, and none
            // when the run that ended was a non-account table), then reset the accumulator — a
            // missed reset would leak one account's fields into the next.
            if (currentAddress)
            {
                co_await detail::finalizeAccount(
                    context, *currentAddress, rows, flatView, accountChanges, output);
                rows = {};
            }
            currentTable = keyView.m_table;
            currentAddress = parseAccountTable(currentTable);
        }
        if (!currentAddress)
        {
            continue;
        }
        detail::accumulateRow(context, rows, keyView, dataValue);
    }
    // N runs have only N-1 internal boundaries: a run that reaches the end of the delta never
    // sees a table change, so the last account is settled here.
    if (currentAddress)
    {
        co_await detail::finalizeAccount(
            context, *currentAddress, rows, flatView, accountChanges, output);
    }

    // Second trie level: commit the account trie over the collected leaf encodings. Runs
    // after the scan by necessity — every leaf embeds its storage-trie root, so the storage
    // tries must be committed first. This root is the block's new MPT state root.
    auto merged = co_await commitTrie(nodeStorage, parentStateRoot, accountChanges);
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
    for (const auto& hash : output.newNodes | std::views::keys)
    {
        if (output.obsoletedNodes.erase(hash) != 0U)
        {
            output.intraBlockObsoleted.insert(hash);
        }
    }

    // One batched flush for the whole block (spec §5.4): nothing inside this build reads a
    // node it produced — storage-trie merges read parent-version nodes only, and the account
    // trie never dereferences a storage root.
    co_await flushTrieNodes(nodeStorage, output.newNodes);
    co_return output;
}

}  // namespace bcos::ledger::mpt
