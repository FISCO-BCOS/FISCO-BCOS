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
 *        stack: flat-state reads answered from the state reverse history at a past block
 *        (M13.2, spec §5.13; pathdb spec §10.1, §11)
 */
#pragma once

#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/mpt/Classify.h>
#include <bcos-ledger/mpt/history/HistoryCommit.h>
#include <bcos-ledger/mpt/history/HistoryRead.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <array>
#include <concepts>
#include <memory>
#include <optional>
#include <range/v3/range/concepts.hpp>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace bcos::scheduler_v1
{

/// The backend of a historical eth_call view (spec §5.13): BaselineScheduler::callAtBlock
/// stacks `storage2::View<MutableStorage, void, HistoricalStateBackend>` — a fresh writable
/// layer over this read-through layer — so the call's own writes land in the mutable layer
/// and read back (read-your-writes, the PR #5324 review blocker), while every MISS resolves
/// here against the state block N committed:
///
///  - the four Ethereum row kinds of an account table ("/apps/<40-hex>": nonce / balance /
///    codeHash / 32-byte slots) answer from the STATE REVERSE HISTORY: the first change after
///    block N recorded what the row held before it, and that is the block-N value; a row not
///    changed since N still holds its block-N value on the committed plane, and is read from
///    there (pathdb spec §10.1, §11). A row that did not exist at N reads as absent. This is a
///    per-key index lookup, NOT a trie walk: the answer does not depend on the trie's shape and
///    is exact for every row the block-N state contained, scenario B or not — the caller still
///    gates to scenario B for the OTHER reason (OQ6: a scenario-A chain's trie, and therefore
///    everything a call's semantics assume about completeness, is incomplete);
///  - the SYS_TABLES row of an account table: exists() means "the account had an Ethereum
///    four-tuple at N", resolved as "at least one of nonce / balance / codeHash was present"
///    — the same condition under which MPTBuilder gives the account a trie leaf and drops it
///    (MPTBuilder.h's tombstone rule);
///  - the flat CODE field reads as absent: at blockVersion >= 3.1 code lives in s_code_binary
///    under the leaf's codeHash, so the flat CODE probe EVMAccount::code() falls back to must
///    miss;
///  - everything else passes through to the LATEST view — deliberately. s_code_binary and
///    s_number_2_hash are content-addressed / append-only, so their rows are valid at any
///    height; abi and the other KNOWN_BCOS_EXTENSION_FIELDS are BCOS metadata outside the
///    committed 4-tuple, deliberately not historicised (PR #5324); system tables (/sys/,
///    config, auth) have no per-block commitment to answer from, so latest is the only
///    answer there is — a documented v1 limitation, not an accident.
///
/// Fail-loud, never a plausible wrong answer (G6): a query older than the retained window
/// throws HistoryPruned BEFORE the seek — that guard is the only thing that can tell "the row
/// never changed after N" from "the record was expired away" — and a hole inside the window
/// throws MPTInvariantViolation. callAtBlock catches both at the scheduler boundary and turns
/// them into an Error the RPC can report.
///
/// Read-only by construction: this class exposes no write surface, and the View above it
/// sends every write to its own mutable layer.
///
/// NOT thread-safe: one instance serves one call coroutine.
///
/// @tparam LatestView a full fork() of the node's MultiLayerStorage — the pass-through plane
///         described above.
/// @tparam HistoryBackend the COMMITTED, seekable plane the history rows live in
///         (`latestBackend()` in production). It is also where an unchanged-since-N row is read
///         from, which is what makes "unchanged since N" and the window guard's @p tip agree on
///         one plane: a pending, not-yet-committed block is invisible to both.
template <class LatestView, class HistoryBackend>
    requires ledger::mpt::history::SeekableStateStorage<HistoryBackend>
class HistoricalStateBackend
{
public:
    using Key = executor_v1::StateKey;
    using Value = executor_v1::StateValue;

    /// Marker consumed by executor_v1::hostcontext::isHistoricalStorage(): storages carrying
    /// it must bypass the global address-keyed executable cache (HostContext.h::getExecutable).
    constexpr static bool isHistoricalStateStorage = true;

    /// @param store the node's ONE state-history store (MPTHistory::state()), which owns the
    ///        in-memory index every row below is located through. A second instance would carry
    ///        an empty index and answer "unchanged since N" for every key — today's state under
    ///        block N's number (G10).
    /// @param blockNumber the height being read, @p tip the chain's committed tip and @p depth
    ///        this node's H_state — together the window guard (spec B.3, G5).
    HistoricalStateBackend(LatestView& latestView,
        ledger::mpt::history::StateHistoryStore const& store, HistoryBackend& historyBackend,
        protocol::BlockNumber blockNumber, protocol::BlockNumber tip, protocol::BlockNumber depth)
      : m_latestView(std::addressof(latestView)),
        m_store(std::addressof(store)),
        m_history(std::addressof(historyBackend)),
        m_blockNumber(blockNumber),
        m_tip(tip),
        m_depth(depth)
    {}
    // Not movable either: the one consumer (callAtBlock) hands the View a pointer, so nothing
    // needs to move it, and a defaulted move would silently rebind the borrowed planes.
    HistoricalStateBackend(const HistoricalStateBackend&) = delete;
    HistoricalStateBackend& operator=(const HistoricalStateBackend&) = delete;
    HistoricalStateBackend(HistoricalStateBackend&&) noexcept = delete;
    HistoricalStateBackend& operator=(HistoricalStateBackend&&) noexcept = delete;
    ~HistoricalStateBackend() noexcept = default;

    task::Task<storage2::StorageValueType<Value>> readOneRaw(auto const& key)
    {
        auto const keyView = asStateKeyView(key);
        if (ledger::mpt::parseAccountTable(keyView.m_table))
        {
            // ONE definition of "the state history covers this row" — the same predicate the
            // commit path captures by (HistoryCommit.h), so a row can never be read from a
            // plane it was not written to.
            if (ledger::mpt::history::isHistoricalStateRow(keyView))
            {
                co_return co_await resolveHistoricalRow(keyView);
            }
            if (ledger::mpt::classifyRowKey(keyView.m_key) == ledger::mpt::RowKind::Code)
            {
                co_return storage2::NOT_EXISTS_TYPE{};
            }
            // abi and friends: BCOS metadata outside the committed 4-tuple, deliberately not
            // historicised (PR #5324). An unclassified field cannot be in the commitment at all
            // (MPTBuilder throws on it at build time), so latest is the only answer.
            co_return co_await m_latestView->readOneRaw(keyView);
        }
        // The SYS_TABLES row of an account table: existence at the queried height.
        if (keyView.m_table == ledger::SYS_TABLES)
        {
            if (ledger::mpt::parseAccountTable(keyView.m_key))
            {
                co_return co_await resolveAccountExistence(keyView.m_key);
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

    /// Ordered iteration is not a thing the reverse history can answer — it indexes each key
    /// on its own — and the execute path never range-scans ACCOUNT state (HostContext reads
    /// point keys only). What does range during a call is the legacy precompiled table plane
    /// (LegacyStorageWrapper over /sys/ and BFS tables), which is pass-through territory here;
    /// serve it from the latest view like every other pass-through read.
    auto range(auto&&... args) -> task::Task<
        storage2::ReturnType<std::invoke_result_t<storage2::Range, LatestView&, decltype(args)...>>>
    {
        co_return co_await storage2::range(*m_latestView, std::forward<decltype(args)>(args)...);
    }

private:
    /// Fields whose presence defines "this account existed" — the same three MPTBuilder folds
    /// into the leaf and whose simultaneous deletion is its tombstone signal (MPTBuilder.h).
    static constexpr std::array<std::string_view, 3> c_existenceFields{
        ledger::mpt::ROW_NONCE, ledger::mpt::ROW_BALANCE, ledger::mpt::ROW_CODE_HASH};

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

    /// One captured row, as of m_blockNumber.
    task::Task<storage2::StorageValueType<Value>> resolveHistoricalRow(
        executor_v1::StateKeyView const& keyView)
    {
        auto version = co_await ledger::mpt::history::readStateAt(
            *m_store, *m_history, keyView, m_blockNumber, m_tip, m_depth);
        if (auto* recorded = std::get_if<bcos::bytes>(std::addressof(version)))
        {
            co_return storage::Entry{std::string_view{
                reinterpret_cast<char const*>(recorded->data()), recorded->size()}};
        }
        if (std::holds_alternative<ledger::mpt::history::HistoryAbsent>(version))
        {
            co_return storage2::NOT_EXISTS_TYPE{};
        }
        // HistoryUseCurrent: nothing changed the row after the queried block, so the committed
        // current value IS the value at that block. Read it from the SAME plane the window
        // guard's tip describes — a pending block's write must not leak into a historical read.
        auto current = co_await storage2::readOne(*m_history, executor_v1::StateKey{keyView});
        if (!current)
        {
            co_return storage2::NOT_EXISTS_TYPE{};
        }
        co_return std::move(*current);
    }

    /// "Did this account exist at the queried height?" — asked through the same rows, so it
    /// cannot disagree with what the four row reads above answer.
    task::Task<storage2::StorageValueType<Value>> resolveAccountExistence(std::string_view table)
    {
        for (auto const& field : c_existenceFields)
        {
            auto value = co_await resolveHistoricalRow(executor_v1::StateKeyView{table, field});
            if (std::get_if<Value>(std::addressof(value)) != nullptr)
            {
                co_return storage::Entry{std::string_view{"value"}};
            }
        }
        co_return storage2::NOT_EXISTS_TYPE{};
    }

    LatestView* m_latestView;
    ledger::mpt::history::StateHistoryStore const* m_store;
    HistoryBackend* m_history;
    protocol::BlockNumber m_blockNumber;
    protocol::BlockNumber m_tip;
    protocol::BlockNumber m_depth;
};

}  // namespace bcos::scheduler_v1
