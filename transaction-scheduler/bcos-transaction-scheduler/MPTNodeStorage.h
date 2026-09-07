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
 * @file MPTNodeStorage.h
 * @brief ViewNodeStorage — the trie-node plane of the execute view: buildAndCollect's node
 *        reads, writes and deletes expressed as ordinary path-addressed state rows on the SAME
 *        view executeBlock writes into (spec §5.6)
 */
#pragma once

#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/mpt/PathKey.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <memory>
#include <optional>
#include <range/v3/view/transform.hpp>
#include <tuple>
#include <utility>
#include <vector>

namespace bcos::scheduler_v1
{

/// The trie-node storage coExecuteBlock hands to buildAndCollect: a thin adapter mapping the
/// builder's (position → RLP bytes) contract onto ordinary state rows of the block's execute view
/// — StateKey{"/mptp/a" | "/mptp/s", <owner?><compactPath>} → Entry(RLP)
/// (ledger::mpt::pathNodeStateKey).
///
///  - node WRITES and DELETES (buildAndCollect's end-of-build flush) land in the view's TOP
///    mutable layer, the same layer the block's flat-state delta lives in;
///  - node READS resolve through the FULL view (mutable → immutable pending layers → cache →
///    backend), the same mechanism readFlatAccountMeta uses for flat parent reads;
///  - a SEEK RANGE over one owner's prefix is what lets a destroyed account's whole storage trie
///    be removed (MPTBuilder::dropStorageTrie). Rows arrive in physical order, which groups by
///    table and then by owner, so the scan stops at the first foreign key.
///
/// Everything the retired MPTNodeOverlay implemented by hand now falls out of the view's
/// layering:
///  - pipeline visibility: block N+1's fork() carries block N's not-yet-committed mutable
///    layer as an immutable layer, node rows included — no pending-delta snapshot, and the
///    shared_ptr layer chain keeps a layer readable across a concurrent commit-side pop;
///  - rollback: dropping a block's layer drops its node rows with it;
///  - commit atomicity: node rows sit in the back layer, so the existing
///    mergeBackStorage(prewriteStorage) merges them into the backend with the flat state in
///    the same single WriteBatch — no extra merge sources, no special casing.
///
/// Ordering inside one build is safe by construction: the flush runs after the delta scan
/// completes, and a stray node row can never be classified as account state because
/// parseAccountTable() only accepts "/apps/<40-hex>" tables (pinned by the stray-row test in
/// TestMPTSchedulerWiring.cpp).
template <class View>
class ViewNodeStorage
{
public:
    using Key = bcos::ledger::mpt::PathKey;
    using Value = bcos::bytes;

    explicit ViewNodeStorage(View& view) : m_view(std::addressof(view)) {}

    task::Task<std::optional<bcos::bytes>> readOne(Key key)
    {
        auto entry = co_await storage2::readOne(*m_view, ledger::mpt::pathNodeStateKey(key));
        if (!entry)
        {
            co_return std::nullopt;
        }
        auto raw = entry->get();
        co_return bcos::bytes(raw.begin(), raw.end());
    }

    task::Task<std::vector<std::optional<bcos::bytes>>> readSome(::ranges::input_range auto keys)
    {
        // One batched read through the view rather than a coroutine round-trip per node: a
        // block's build resolves thousands of parent nodes.
        auto entries = co_await storage2::readSome(*m_view,
            keys | ::ranges::views::transform(
                       [](auto const& key) { return ledger::mpt::pathNodeStateKey(key); }));

        std::vector<std::optional<bcos::bytes>> values;
        values.reserve(entries.size());
        for (auto const& entry : entries)
        {
            if (!entry)
            {
                values.emplace_back(std::nullopt);
                continue;
            }
            auto raw = entry->get();
            values.emplace_back(bcos::bytes(raw.begin(), raw.end()));
        }
        co_return values;
    }

    task::Task<void> writeOne(Key key, bcos::bytes value)
    {
        storage::Entry entry;
        entry.set(std::move(value));
        co_await storage2::writeOne(
            mutableStorage(*m_view), ledger::mpt::pathNodeStateKey(key), std::move(entry));
    }

    task::Task<void> writeSome(::ranges::input_range auto keyValues)
    {
        // One batched write into the top mutable layer, same reason as readSome above. The
        // RLP is COPIED into each row's Entry, never moved out: flushTrieNodes hands const
        // references and PathDiff::upserts must stay intact for the CommitObserver.
        co_await storage2::writeSome(
            mutableStorage(*m_view), keyValues | ::ranges::views::transform([](auto&& keyValue) {
                auto const& [key, value] = keyValue;
                storage::Entry entry;
                entry.set(bcos::bytes(value.begin(), value.end()));
                return std::make_pair(ledger::mpt::pathNodeStateKey(key), std::move(entry));
            }));
    }

    task::Task<void> removeOne(Key key)
    {
        co_await storage2::removeOne(mutableStorage(*m_view), ledger::mpt::pathNodeStateKey(key));
    }

    /// Logical deletion in the block's mutable layer, exactly like a removed flat row: the
    /// tombstone rides the same WriteBatch and reaches the backend as a Delete.
    task::Task<void> removeSome(::ranges::input_range auto keys)
    {
        co_await storage2::removeSome(mutableStorage(*m_view),
            keys | ::ranges::views::transform(
                       [](auto const& key) { return ledger::mpt::pathNodeStateKey(key); }));
    }

    /// Node rows in physical order from @p start, decoded back into PathKeys. Ends at the first
    /// row that is not a node row at all; rows of a DIFFERENT trie are still yielded, so a caller
    /// scanning one owner's prefix compares scopes and stops itself.
    template <class ViewIterator>
    class Iterator
    {
    public:
        explicit Iterator(ViewIterator inner) : m_inner(std::move(inner)) {}

        task::Task<std::optional<
            std::tuple<bcos::ledger::mpt::PathKey, storage2::StorageValueType<bcos::bytes>>>>
        next()
        {
            auto item = co_await m_inner.next();
            if (!item)
            {
                co_return std::nullopt;
            }
            auto const& [stateKey, stateValue] = *item;
            auto pathKey = ledger::mpt::parsePathNodeStateKey(stateKey);
            if (!pathKey)
            {
                co_return std::nullopt;  // past the node tables
            }
            if (auto const* entry = std::get_if<storage::Entry>(std::addressof(stateValue)))
            {
                auto raw = entry->get();
                co_return std::make_tuple(std::move(*pathKey),
                    storage2::StorageValueType<bcos::bytes>{bcos::bytes(raw.begin(), raw.end())});
            }
            co_return std::make_tuple(
                std::move(*pathKey), storage2::StorageValueType<bcos::bytes>{storage2::deleteItem});
        }

    private:
        ViewIterator m_inner;
    };

    task::Task<Iterator<typename View::Iterator>> range(
        storage2::RANGE_SEEK_TYPE /*unused*/, Key const& start)
    {
        auto inner = co_await storage2::range(
            *m_view, storage2::RANGE_SEEK, ledger::mpt::pathNodeStateKey(start));
        co_return Iterator<typename View::Iterator>{std::move(inner)};
    }

private:
    View* m_view;
};

}  // namespace bcos::scheduler_v1
