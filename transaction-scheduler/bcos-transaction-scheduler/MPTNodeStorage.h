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
 *        reads and writes expressed as ordinary "/mpt/" state rows on the SAME view
 *        executeBlock writes into (spec §5.6)
 */
#pragma once

#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-storage/KeyPrefixes.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <memory>
#include <optional>
#include <range/v3/view/transform.hpp>
#include <utility>
#include <vector>

namespace bcos::scheduler_v1
{

/// The trie-node storage coExecuteBlock hands to buildAndCollect: a thin adapter mapping the
/// builder's (h256 → RLP bytes) contract onto ordinary state rows of the block's execute view
/// — StateKey{"/mpt/", digest} → Entry(RLP) (KeyPrefixes.h::mptNodeStateKey).
///
///  - node WRITES (buildAndCollect's end-of-build flush) land in the view's TOP mutable
///    layer, the same layer the block's flat-state delta lives in;
///  - node READS resolve through the FULL view (mutable → immutable pending layers → cache →
///    backend), the same mechanism readFlatAccountMeta uses for flat parent reads.
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
/// completes, and a stray "/mpt/" row can never be classified as account state because
/// parseAccountTable() only accepts "/apps/<40-hex>" tables (pinned by the stray-row test in
/// TestMPTSchedulerWiring.cpp).
template <class View>
class ViewNodeStorage
{
public:
    using Key = bcos::h256;
    using Value = bcos::bytes;

    explicit ViewNodeStorage(View& view) : m_view(std::addressof(view)) {}

    task::Task<std::optional<bcos::bytes>> readOne(bcos::h256 key)
    {
        auto entry = co_await storage2::readOne(*m_view, storage2::mptNodeStateKey(key));
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
        auto entries = co_await storage2::readSome(
            *m_view, keys | ::ranges::views::transform(
                                [](auto const& key) { return storage2::mptNodeStateKey(key); }));

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

    task::Task<void> writeOne(bcos::h256 key, bcos::bytes value)
    {
        storage::Entry entry;
        entry.set(std::move(value));
        co_await storage2::writeOne(
            mutableStorage(*m_view), storage2::mptNodeStateKey(key), std::move(entry));
    }

    task::Task<void> writeSome(::ranges::input_range auto keyValues)
    {
        // One batched write into the top mutable layer, same reason as readSome above. The
        // RLP is COPIED into each row's Entry, never moved out: flushTrieNodes hands const
        // references and MPTDeltaLayer::newNodes must stay intact for the CommitObserver.
        co_await storage2::writeSome(
            mutableStorage(*m_view), keyValues | ::ranges::views::transform([](auto&& keyValue) {
                auto const& [key, value] = keyValue;
                storage::Entry entry;
                entry.set(bcos::bytes(value.begin(), value.end()));
                return std::make_pair(storage2::mptNodeStateKey(key), std::move(entry));
            }));
    }

private:
    View* m_view;
};

}  // namespace bcos::scheduler_v1
