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
 * @file FlatProof.h
 * @brief EIP-1186 proof generation by REBUILDING the trie from the committed flat state —
 *        the OP-stack execution path, whose stateRoot is recomputed per block and whose MPT
 *        nodes are never persisted
 */
#pragma once

#include "Account.h"
#include "Classify.h"
#include "Constants.h"
#include "Errors.h"
#include "FlatToMPT.h"
#include "HashBuilder.h"
#include "Proof.h"
#include "StorageValueCodec.h"
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/AwaitableValue.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/throw_exception.hpp>
#include <cstring>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace bcos::ledger::mpt
{

namespace detail
{

/// The rebuilt trie's node set, served to generateProof()/proofWalk() as a storage2-readable
/// store: hash -> raw RLP. Only hash-referenced nodes are kept (computeTrieRoot's newNodes) —
/// inline children live inside their parent's encoding, which is exactly what the proof walk
/// expects to re-encounter.
struct RebuiltNodeStorage
{
    std::unordered_map<bcos::h256, bcos::bytes> nodes;

    task::AwaitableValue<std::optional<bcos::bytes>> readOne(bcos::h256 const& key) const
    {
        if (auto it = nodes.find(key); it != nodes.end())
        {
            return task::AwaitableValue<std::optional<bcos::bytes>>(it->second);
        }
        return task::AwaitableValue<std::optional<bcos::bytes>>(std::nullopt);
    }

    /// Concept-completion for storage2::ReadableStorage (generateProof's Storage constraint
    /// requires readSome alongside readOne); the proof walk only ever reads one node at a time.
    task::AwaitableValue<std::vector<std::optional<bcos::bytes>>> readSome(
        ::ranges::input_range auto const& keys) const
    {
        std::vector<std::optional<bcos::bytes>> out;
        for (auto const& key : keys)
        {
            out.emplace_back(readOne(key).value);
        }
        return task::AwaitableValue<std::vector<std::optional<bcos::bytes>>>(std::move(out));
    }
};

/// Live-entry extraction for range items: storage2 logical deletion means a raw range value is
/// the StorageValueType variant (NOT_EXISTS/DELETED/Entry) and tombstones must be skipped, or a
/// logically-deleted row would resurrect into the rebuilt root. Non-variant storages (no
/// LOGICAL_DELETION) yield the value directly; both shapes resolve to an Entry pointer or
/// nullptr for "not live".
template <class DataValue>
bcos::storage::Entry const* liveEntry(DataValue const& dataValue)
{
    if constexpr (requires { std::get_if<bcos::storage::Entry>(std::addressof(dataValue)); })
    {
        return std::get_if<bcos::storage::Entry>(std::addressof(dataValue));
    }
    else
    {
        return std::addressof(dataValue);
    }
}

/// One account's Ethereum four-tuple ingredients, gathered from a single range scan of its
/// flat table. Present-but-empty metadata rows decode as the Yellow Paper defaults, mirroring
/// Storage2State::fetchAccount (the OP execution path's reader): nonce/balance 0, codeHash
/// keccak256("").
struct FlatAccount
{
    bcos::u256 nonce = 0;
    bcos::u256 balance = 0;
    bcos::h256 codeHash = emptyCodeHash();
};

/// Fold one classified row of an account table into @p account / @p storageEntries. Shape
/// violations (a slot value that is not 32 bytes, a malformed codeHash) throw
/// MPTInvariantViolation — the same rows would fail the OP block's own stateRoot computation
/// (Storage2State::fetchAccount/fetchAllStorage), so they cannot honestly exist in committed
/// state; a silent default would instead produce a root that matches nothing.
template <bcos::crypto::hasher::Hasher HasherT>
void foldFlatRow(FlatAccount& account, std::map<bcos::h256, bcos::bytes>& storageEntries,
    RowKind kind, std::string_view fieldKey, bcos::storage::Entry const& entry, HasherT& hasher)
{
    auto const raw = entry.get();
    switch (kind)
    {
    case RowKind::Nonce:
    {
        if (!raw.empty())
        {
            account.nonce = detail::entryToU256(entry);
        }
        break;
    }
    case RowKind::Balance:
    {
        if (!raw.empty())
        {
            account.balance = detail::entryToU256(entry);
        }
        break;
    }
    case RowKind::CodeHash:
    {
        if (!raw.empty())
        {
            if (raw.size() != static_cast<size_t>(bcos::h256::SIZE))
            {
                BOOST_THROW_EXCEPTION(MPTInvariantViolation{} << bcos::errinfo_comment(
                                          "flat codeHash row is not 32 raw bytes; size=" +
                                          std::to_string(raw.size())));
            }
            account.codeHash = bcos::h256(
                bcos::bytesConstRef(reinterpret_cast<bcos::byte const*>(raw.data()), raw.size()));
        }
        break;
    }
    case RowKind::StorageSlot:
    {
        // A slot value that is not exactly 32 bytes is a storage-layout violation (the executor
        // writes raw 32-byte values); the size check doubles as the parse guard for
        // encodeStorageValue, which would otherwise happily trim any width.
        if (raw.size() != static_cast<size_t>(bcos::h256::SIZE))
        {
            BOOST_THROW_EXCEPTION(MPTInvariantViolation{} << bcos::errinfo_comment(
                                      "flat storage slot value is not 32 raw bytes; size=" +
                                      std::to_string(raw.size())));
        }
        auto encoded = encodeStorageValue(
            bcos::bytesConstRef(reinterpret_cast<bcos::byte const*>(raw.data()), raw.size()));
        if (encoded.empty())
        {
            break;  // all-zero slot: absent under Ethereum semantics ("zero slot == no slot")
        }
        bcos::h256 slot(bcos::bytesConstRef(
            reinterpret_cast<bcos::byte const*>(fieldKey.data()), fieldKey.size()));
        storageEntries[slotKeyHash(slot, hasher)] = std::move(encoded);
        break;
    }
    case RowKind::Code:
    case RowKind::BcosExtension:
        // Never part of the Ethereum four-tuple: code is committed via its paired codeHash row,
        // BCOS extension fields are deliberately excluded (Classify.h's whitelist judgment).
        break;
    case RowKind::UnknownField:
        // The caller filters this before folding; kept here so the switch stays exhaustive
        // (-Wswitch guards the RowKind contract, same as MPTBuilder's accumulateRow).
        BOOST_THROW_EXCEPTION(MPTInvariantViolation{} << bcos::errinfo_comment(
                                  "unclassified field must not enter the rebuilt trie"));
    }
}

}  // namespace detail

/// Generate an EIP-1186 proof for @p address (and @p slots) against @p expectedRoot by
/// rebuilding the whole state trie from the committed FLAT state in @p flatStorage.
///
/// This serves the OP-stack execution path: its stateRoot is recomputed per block from the flat
/// state (bcos::evm::stateRootOf over Storage2State) and no MPT node rows are ever persisted,
/// so the PBMT generateProof() — which walks persisted nodes — has nothing to read. The rebuild
/// enumerates the /apps/ account tables exactly the way the seal-time stateRootOf does
/// (Storage2State::visitAccounts semantics: SYS_TABLES prefix scan, tombstone/zero filtering,
/// same field decoders), builds every storage trie plus the account trie via computeTrieRoot,
/// and serves generateProof() over the in-memory node set.
///
/// Integrity gate: if the rebuilt root != expectedRoot, NO proof is returned
/// (ProofErrorCode::RootMismatch). The flat plane holds one state version (latest committed);
/// a request for an older block, or a commit landing between the header read and the fork of
/// @p flatStorage, both surface here. This check is what makes the whole rebuild safe — any
/// layer skew can only fail the request, never produce a proof against the wrong state.
///
/// Absent accounts keep op-geth's GetProof semantics (internal/ethapi/api.go: a Prove() walk
/// with no leaf): accountProof carries the exclusion path, balance/nonce are zero, codeHash and
/// storageHash are the ZERO hash (StateDB.GetCodeHash/GetStorageRoot on a missing object), and
/// every requested slot answers value 0x0 with an empty proof.
///
/// Read-cost note: the rebuild is a full-state scan (every account, every slot). Correctness
/// first — the devnet state is small; an archive of persisted per-block node sets is the
/// long-term answer for both cost and historical proofs.
///
/// @param flatStorage a consistent view of the COMMITTED flat plane (NodeService's
///                    StateStorageProvider in production; range() must be ordered — the
///                    merged MultiLayerStorage iterator is, and it excludes the read-through
///                    cache, so the enumeration observes exactly the committed backend).
/// @param expectedRoot the requested block header's stateRoot.
/// @throws MPTInvariantViolation on flat-state shape violations (unknown account-table field,
///         malformed rows) — programming/data errors, mapped by callers to -32603.
template <
    bcos::crypto::hasher::Hasher HasherT = bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher>
bcos::task::Task<std::variant<EIP1186Proof, ProofErrorCode>> generateProofFromFlat(
    auto& flatStorage, bcos::h256 expectedRoot, bcos::Address address,
    std::span<bcos::h256 const> slots)
{
    if (expectedRoot == emptyRootHash())
    {
        // The empty trie has no walkable nodes: geth answers an empty accountProof.
        co_return absentAccountProof(address, slots, {});
    }

    HasherT hasher;
    std::map<bcos::h256, bcos::bytes> accountEntries;
    detail::RebuiltNodeStorage nodeStore;

    // ---- enumeration: SYS_TABLES /apps/ prefix scan, then one range scan per account table ----
    // Seek keys are StateKey, not StateKeyView: AnyStorage's range(RANGE_SEEK, Key) matches the
    // erased Key type exactly, and StateKey's view constructor is explicit.
    auto tableIterator = co_await storage2::range(flatStorage, storage2::RANGE_SEEK,
        executor_v1::StateKey{executor_v1::StateKeyView{
            bcos::ledger::SYS_TABLES, bcos::ledger::SYS_DIRECTORY::USER_APPS}});
    while (auto item = co_await tableIterator.next())
    {
        auto const& [key, dataValue] = *item;
        executor_v1::StateKeyView keyView(key);
        auto const [table, tableKey] = keyView.get();
        // Merged iteration is globally ordered, but the AnyStorage seek fallback documents
        // no-seek layers contributing from their beginning — rows below the seek point are
        // skipped, the scan ends only at the first row PAST the /apps/ prefix range.
        if (table != bcos::ledger::SYS_TABLES)
        {
            if (table > bcos::ledger::SYS_TABLES)
            {
                break;
            }
            continue;
        }
        if (!tableKey.starts_with(bcos::ledger::SYS_DIRECTORY::USER_APPS))
        {
            if (tableKey > bcos::ledger::SYS_DIRECTORY::USER_APPS)
            {
                break;
            }
            continue;
        }
        // Tombstoned directory row: the account (or non-account table) is gone.
        if (detail::liveEntry(dataValue) == nullptr)
        {
            continue;
        }
        // /apps/ also holds authorization and BFS link tables; only /apps/<40-hex> are accounts.
        auto const parsed = parseAccountTable(tableKey);
        if (!parsed.has_value())
        {
            continue;
        }

        // One range scan yields metadata rows and slots together — a single-plane snapshot of
        // the table (readOne would consult the read-through cache and could observe a NEWER
        // committed row than the range plane; the root gate at the end converts any such skew
        // into an honest error).
        detail::FlatAccount account;
        std::map<bcos::h256, bcos::bytes> storageEntries;
        auto rowIterator = co_await storage2::range(flatStorage, storage2::RANGE_SEEK,
            executor_v1::StateKey{
                executor_v1::StateKeyView{std::string{tableKey}, std::string_view{}}});
        while (auto row = co_await rowIterator.next())
        {
            auto const& [rowKey, rowData] = *row;
            executor_v1::StateKeyView rowView(rowKey);
            auto const [rowTable, rowField] = rowView.get();
            if (rowTable != tableKey)
            {
                break;
            }
            auto const kind = classifyRowKey(rowField);
            if (kind == RowKind::UnknownField)
            {
                // A row nobody ever classified must not silently fall out of the commitment —
                // same loud rule as the MPT builder (Classify.h's whitelist contract).
                BOOST_THROW_EXCEPTION(
                    MPTInvariantViolation{} << bcos::errinfo_comment(
                        "unknown field in account table '" + std::string{tableKey} +
                        "': neither a known field name nor a 32-byte slot"));
            }
            auto const* entry = detail::liveEntry(rowData);
            if (entry == nullptr)
            {
                continue;  // tombstoned row: a logically-deleted slot must not resurrect
            }
            detail::foldFlatRow(account, storageEntries, kind, rowField, *entry, hasher);
        }

        auto storageBuild = computeTrieRoot(storageEntries);
        for (auto& [hash, rlp] : storageBuild.newNodes)
        {
            nodeStore.nodes.emplace(hash, std::move(rlp));
        }
        Account leaf;
        leaf.nonce = account.nonce;
        leaf.balance = account.balance;
        leaf.storageRoot = storageBuild.root;
        leaf.codeHash = account.codeHash;
        accountEntries[accountKeyHash(*parsed)] = leaf.encode();
    }

    // ---- integrity gate: the rebuild must root exactly at the requested block's root ----
    auto accountBuild = computeTrieRoot(accountEntries);
    if (accountBuild.root != expectedRoot)
    {
        co_return ProofErrorCode::RootMismatch;
    }
    for (auto& [hash, rlp] : accountBuild.newNodes)
    {
        nodeStore.nodes.emplace(hash, std::move(rlp));
    }

    if (!accountEntries.contains(accountKeyHash(address)))
    {
        // Absent account, op-geth semantics: exclusion walk + zero fields.
        auto walk = co_await detail::proofWalk(
            nodeStore, expectedRoot, bytesToNibbles(accountKeyHash(address).ref()));
        co_return absentAccountProof(address, slots, std::move(walk.nodes));
    }

    // Complete flat state: every exclusion walk IS a provable zero — fullTrie semantics.
    co_return co_await generateProof(nodeStore, expectedRoot, address, slots, /*fullTrie=*/true);
}

}  // namespace bcos::ledger::mpt
