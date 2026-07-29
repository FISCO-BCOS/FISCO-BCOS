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
 * @file MPTNodeReadStorage.h
 * @brief MPTNodeReadStorage — the eth_getProof node reader (M8.3 wiring): a read-only
 *        (h256 -> raw RLP bytes) facade over the ordinary "/mpt/" state rows of a committed
 *        state storage, plus makeMPTNodeReader() producing the owning AnyStorage handle
 *        NodeService::setMPTNodeReader expects
 */
#pragma once

#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/AnyStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-storage/KeyPrefixes.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/throw_exception.hpp>
#include <memory>
#include <optional>
#include <range/v3/view/transform.hpp>
#include <stdexcept>
#include <utility>
#include <vector>

namespace bcos::storage2
{

/// The read-side sibling of the scheduler's ViewNodeStorage (MPTNodeStorage.h): where that
/// adapter rides a block's execute view so the builder sees pending layers, this one wraps a
/// COMMITTED storage — production wires it over GlobalStateStorage::latestBackend(), the
/// plane every committed block's node rows were merged into — because eth_getProof answers
/// for committed headers only and must never see an in-flight block's rows.
///
/// Reads translate h256 -> StateKey{"/mpt/", digest} (KeyPrefixes.h::mptNodeStateKey) and
/// unwrap the Entry value to its raw bytes; a missing row is std::nullopt, the miss shape
/// generateProof's proofWalk maps to its rootMissing / BlockNotCommitted handling.
///
/// AnyStorage's type-erasure model instantiates the FULL storage surface (its StorageModel
/// virtuals are not SFINAE-guarded), so the mutating and scanning entry points exist here
/// only to satisfy that instantiation — each throws std::logic_error: the proof path is
/// strictly read-only and nothing may write state through an RPC-held handle.
template <class Storage>
class MPTNodeReadStorage
{
public:
    using Key = bcos::h256;
    using Value = bcos::bytes;

    explicit MPTNodeReadStorage(Storage& storage) : m_storage(std::addressof(storage)) {}

    task::Task<std::optional<bcos::bytes>> readOne(bcos::h256 key)
    {
        auto entry = co_await storage2::readOne(*m_storage, storage2::mptNodeStateKey(key));
        if (!entry)
        {
            co_return std::nullopt;
        }
        auto raw = entry->get();
        co_return bcos::bytes(raw.begin(), raw.end());
    }

    task::Task<std::vector<std::optional<bcos::bytes>>> readSome(::ranges::input_range auto keys)
    {
        // One batched read: a proof walk resolves whole node paths at a time.
        auto entries = co_await storage2::readSome(
            *m_storage, keys | ::ranges::views::transform(
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

    // --- read-only guard rail: everything below throws, see the class comment ---

    /// The iterator type range() nominally yields; never actually produced. The unreachable
    /// co_returns below keep each guard a coroutine, so the throw surfaces at co_await like
    /// every other storage error.
    struct NoIterator
    {
        task::Task<std::optional<std::pair<bcos::h256, StorageValueType<bcos::bytes>>>> next()
        {
            throwReadOnly();
            co_return std::nullopt;
        }
    };

    task::Task<void> writeOne(bcos::h256 /*key*/, bcos::bytes /*value*/)
    {
        throwReadOnly();
        co_return;
    }
    task::Task<void> writeSome(::ranges::input_range auto /*keyValues*/)
    {
        throwReadOnly();
        co_return;
    }
    task::Task<void> removeOne(bcos::h256 /*key*/, auto&&... /*tags*/)
    {
        throwReadOnly();
        co_return;
    }
    task::Task<void> removeSome(::ranges::input_range auto /*keys*/, auto&&... /*tags*/)
    {
        throwReadOnly();
        co_return;
    }
    task::Task<NoIterator> range(auto&&... /*args*/)
    {
        throwReadOnly();
        co_return NoIterator{};
    }

private:
    [[noreturn]] static void throwReadOnly()
    {
        BOOST_THROW_EXCEPTION(std::logic_error(
            "MPTNodeReadStorage is a read-only view over committed MPT node rows"));
    }

    Storage* m_storage;
};

/// Build the handle NodeService::setMPTNodeReader takes: an AnyStorage<h256, bytes> that
/// OWNS its MPTNodeReadStorage adapter via an aliasing shared_ptr, so callers manage exactly
/// one lifetime. The only borrowed piece is @p storage itself (production: the state
/// backend, owned by the Initializer), which must outlive the returned handle.
template <class Storage>
[[nodiscard]] std::shared_ptr<AnyStorage<bcos::h256, bcos::bytes>> makeMPTNodeReader(
    Storage& storage)
{
    struct OwningReader
    {
        MPTNodeReadStorage<Storage> adapter;
        std::optional<AnyStorage<bcos::h256, bcos::bytes>> erased;

        explicit OwningReader(Storage& storage) : adapter(storage) { erased.emplace(adapter); }
    };
    auto owner = std::make_shared<OwningReader>(storage);
    return {owner, std::addressof(*owner->erased)};
}

}  // namespace bcos::storage2
