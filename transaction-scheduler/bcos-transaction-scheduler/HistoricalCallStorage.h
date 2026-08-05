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
 * @file HistoricalCallStorage.h
 * @brief HistoricalStateBackend — the read-through layer of a historical eth_call's storage
 *        stack: flat-state reads answered from the MPT at a past block's state root (M13.2,
 *        spec §5.13)
 */
#pragma once

#include "MPTNodeStorage.h"
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/mpt/Classify.h>
#include <bcos-ledger/mpt/MPTAccount.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <concepts>
#include <map>
#include <memory>
#include <optional>
#include <range/v3/range/concepts.hpp>
#include <string_view>
#include <utility>
#include <vector>

namespace bcos::scheduler_v1
{

/// The backend of a historical eth_call view (spec §5.13): BaselineScheduler::callAtBlock
/// stacks `storage2::View<MutableStorage, void, HistoricalStateBackend>` — a fresh writable
/// layer over this read-through layer — so the call's own writes land in the mutable layer
/// and read back (read-your-writes, the PR #5324 review blocker), while every MISS resolves
/// here against the state block N committed:
///
///  - account-table rows ("/apps/<40-hex>": nonce / balance / codeHash / 32-byte slots)
///    answer from the block's MPT via MPTAccount's rooted reads, re-encoded in the exact
///    flat representations EVMAccount reads (decimal strings for nonce and balance, raw
///    32 bytes for codeHash and slot values). An account or slot with no leaf at that root
///    reads as absent — Ethereum semantics, exact for scenario B where the trie is the
///    complete state (gating to scenario B is the caller's job, per OQ6);
///  - the SYS_TABLES row of an account table answers leaf presence, so exists() means what
///    an Ethereum client means by it (MPTAccount.h's documented divergence, resolved the
///    Ethereum way);
///  - the flat CODE field reads as absent: at blockVersion >= 3.1 code lives in
///    s_code_binary under the leaf's codeHash (see MPTAccount::code()'s shape inventory),
///    and s_code_binary passes through below;
///  - everything else passes through to the LATEST view — deliberately. s_code_binary and
///    s_number_2_hash are content-addressed / append-only, so their rows are valid at any
///    height; abi and the other KNOWN_BCOS_EXTENSION_FIELDS are BCOS metadata outside the
///    committed 4-tuple, deliberately not historicised (PR #5324); system tables (/sys/,
///    config, auth) have no per-block commitment to answer from, so latest is the only
///    answer there is — a documented v1 limitation, not an accident.
///
/// Read-only by construction: this class exposes no write surface, and the View above it
/// sends every write to its own mutable layer. Trie reads can throw (MPTMissingNode /
/// MPTDecodeError / MPTInvariantViolation on a corrupt store); callAtBlock catches them at
/// the scheduler boundary and turns them into an Error the RPC can report.
///
/// NOT thread-safe: one instance serves one call coroutine (the per-address MPTAccount
/// cache and its per-root leaf cache are unsynchronised, exactly like MPTAccount itself).
///
/// @tparam LatestView a full fork() of the node's MultiLayerStorage. It serves two roles:
///         trie-NODE resolution (ViewNodeStorage reads "/mpt/" rows through it — correct for
///         ANY root because nodes are content-addressed, so pending layers can only add,
///         never change them) and the pass-through plane described above.
template <class LatestView>
class HistoricalStateBackend
{
public:
    using Key = executor_v1::StateKey;
    using Value = executor_v1::StateValue;
    using AccountType =
        ledger::mpt::MPTAccount<LatestView, ViewNodeStorage<LatestView>, LatestView>;

    /// Marker consumed by executor_v1::hostcontext::isHistoricalStorage(): storages carrying
    /// it must bypass the global address-keyed executable cache (HostContext.h::getExecutable).
    constexpr static bool isHistoricalStateStorage = true;

    /// @param stateRoot the MPT state root block N committed — its header's stateRoot.
    HistoricalStateBackend(LatestView& latestView, h256 stateRoot)
      : m_latestView(std::addressof(latestView)), m_nodeStorage(latestView), m_stateRoot(stateRoot)
    {}
    // Not movable either: cached MPTAccounts hold reference_wrappers into this object's
    // m_nodeStorage member, which a defaulted move would leave dangling. The one consumer
    // (callAtBlock) hands the View a pointer, so nothing needs to move it.
    HistoricalStateBackend(const HistoricalStateBackend&) = delete;
    HistoricalStateBackend& operator=(const HistoricalStateBackend&) = delete;
    HistoricalStateBackend(HistoricalStateBackend&&) noexcept = delete;
    HistoricalStateBackend& operator=(HistoricalStateBackend&&) noexcept = delete;
    ~HistoricalStateBackend() noexcept = default;

    task::Task<storage2::StorageValueType<Value>> readOneRaw(auto const& key)
    {
        auto const keyView = asStateKeyView(key);
        // Account-table rows: the block's MPT is the source of truth.
        if (auto address = ledger::mpt::parseAccountTable(keyView.m_table))
        {
            co_return co_await resolveAccountRow(*address, keyView);
        }
        // The SYS_TABLES row of an account table: existence == leaf presence at the root.
        if (keyView.m_table == ledger::SYS_TABLES)
        {
            if (auto address = ledger::mpt::parseAccountTable(keyView.m_key))
            {
                if (co_await accountAt(*address).exists(m_stateRoot))
                {
                    co_return storage::Entry{std::string_view{"value"}};
                }
                co_return storage2::NOT_EXISTS_TYPE{};
            }
        }
        // Everything else: pass through to the latest view (see class comment).
        co_return co_await m_latestView->readOneRaw(keyView);
    }

    task::Task<std::vector<storage2::StorageValueType<Value>>> readSomeRaw(
        ::ranges::input_range auto keys)
    {
        std::vector<storage2::StorageValueType<Value>> values;
        if constexpr (::ranges::sized_range<decltype(keys)>)
        {
            values.reserve(::ranges::size(keys));
        }
        for (auto const& key : keys)
        {
            values.emplace_back(co_await readOneRaw(key));
        }
        co_return values;
    }

    task::Task<std::optional<Value>> readOne(auto const& key)
    {
        auto value = co_await readOneRaw(key);
        if (auto* entry = std::get_if<Value>(std::addressof(value)))
        {
            co_return std::move(*entry);
        }
        co_return std::nullopt;
    }

    task::Task<std::vector<std::optional<Value>>> readSome(::ranges::input_range auto keys)
    {
        std::vector<std::optional<Value>> values;
        if constexpr (::ranges::sized_range<decltype(keys)>)
        {
            values.reserve(::ranges::size(keys));
        }
        for (auto const& key : keys)
        {
            values.emplace_back(co_await readOne(key));
        }
        co_return values;
    }

    /// Ordered iteration cannot be answered from an MPT — slot keys are hashed into the
    /// trie, so enumerating flat keys would need the whole preimage set — and the execute
    /// path never range-scans ACCOUNT state (HostContext reads point keys only). What does
    /// range during a call is the legacy precompiled table plane (LegacyStorageWrapper over
    /// /sys/ and BFS tables), which is pass-through territory here; serve it from the
    /// latest view like every other pass-through read.
    auto range(auto&&... args) -> task::Task<
        storage2::ReturnType<std::invoke_result_t<storage2::Range, LatestView&, decltype(args)...>>>
    {
        co_return co_await storage2::range(*m_latestView, std::forward<decltype(args)>(args)...);
    }

private:
    static executor_v1::StateKeyView asStateKeyView(auto const& key)
    {
        using KeyType = std::decay_t<decltype(key)>;
        if constexpr (std::same_as<KeyType, executor_v1::StateKeyView>)
        {
            return key;
        }
        else if constexpr (std::same_as<KeyType, executor_v1::StateKey>)
        {
            return executor_v1::StateKeyView{key};
        }
        else
        {
            // std::reference_wrapper (fillMissingValues batches keys this way).
            return asStateKeyView(key.get());
        }
    }

    /// One MPTAccount per address, kept for the whole call: the account leaf is decoded once
    /// per address (MPTAccount caches it per root) no matter how many field and slot reads
    /// the call performs.
    AccountType& accountAt(Address const& address)
    {
        auto it = m_accounts.find(address);
        if (it == m_accounts.end())
        {
            // binaryAddress is false by construction: feature_raw_address is mutually
            // exclusive with both MPT flags for the life of a chain
            // (BaselineSchedulerMPTHelpers.h::validateMPTFlagMatrix), and callAtBlock only
            // builds this backend for MPT blocks.
            it = m_accounts
                     .try_emplace(address, *m_latestView, m_nodeStorage, *m_latestView, address,
                         /*binaryAddress*/ false)
                     .first;
        }
        return it->second;
    }

    task::Task<storage2::StorageValueType<Value>> resolveAccountRow(
        Address const& address, executor_v1::StateKeyView const& keyView)
    {
        using ledger::mpt::RowKind;
        auto& account = accountAt(address);
        switch (ledger::mpt::classifyRowKey(keyView.m_key))
        {
        case RowKind::StorageSlot:
        {
            if (auto entry = co_await account.storageEntry(keyView.m_key, m_stateRoot))
            {
                co_return std::move(*entry);
            }
            co_return storage2::NOT_EXISTS_TYPE{};
        }
        case RowKind::Balance:
        {
            if (!co_await account.exists(m_stateRoot))
            {
                co_return storage2::NOT_EXISTS_TYPE{};
            }
            auto balance = co_await account.balance(m_stateRoot);
            // The flat representation EVMAccount::balance() lexical_casts back.
            co_return storage::Entry{balance.str({}, {})};
        }
        case RowKind::Nonce:
        {
            if (auto nonce = co_await account.nonce(m_stateRoot))
            {
                co_return storage::Entry{*nonce};
            }
            co_return storage2::NOT_EXISTS_TYPE{};
        }
        case RowKind::CodeHash:
        {
            if (!co_await account.exists(m_stateRoot))
            {
                co_return storage2::NOT_EXISTS_TYPE{};
            }
            auto codeHash = co_await account.codeHash(m_stateRoot);
            // Raw 32 bytes, exactly as EVMAccount::setCode writes the row.
            co_return storage::Entry{concepts::bytebuffer::toView(codeHash)};
        }
        case RowKind::Code:
            // Modern (>= 3.1) code lives in s_code_binary under the leaf's codeHash; the
            // flat CODE probe EVMAccount::code() falls back to must miss (class comment).
            co_return storage2::NOT_EXISTS_TYPE{};
        case RowKind::BcosExtension:
        case RowKind::UnknownField:
            // abi and friends: BCOS metadata outside the committed 4-tuple, deliberately
            // not historicised (PR #5324). Unclassified fields cannot be IN the trie at all
            // (MPTBuilder throws on them at build time), so latest is the only answer.
            co_return co_await m_latestView->readOneRaw(keyView);
        }
        // Unreachable: classifyRowKey covers every enumerator.
        co_return storage2::NOT_EXISTS_TYPE{};
    }

    LatestView* m_latestView;
    ViewNodeStorage<LatestView> m_nodeStorage;
    h256 m_stateRoot;
    std::map<Address, AccountType> m_accounts;
};

}  // namespace bcos::scheduler_v1
