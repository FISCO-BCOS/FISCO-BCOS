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
 * @file ShardTableWalk.h
 * @brief The ONE seek-walk over a reverse-history shard table (layout spec §1.2)
 *
 * Three components read the same rows in the same order: the whole-block read and the startup
 * rebuild inside ReverseHistoryStore, and the offline B.10 audit. What they do with a row differs
 * completely — one materializes a diff, one builds an index, one counts damage — but HOW the rows
 * are reached does not: seek into the table, walk forward, stop when the table changes, and reduce
 * each row to "these bytes" or "a deletion sentinel".
 *
 * It lives here, on its own, because the audit is the component that exists to detect layout damage
 * and it must not read the layout through a second implementation of the walk. A copy that drifted
 * — a different stop condition, a different reading of a sentinel — would make the audit agree with
 * itself while disagreeing with the store it is auditing.
 *
 * Everything that CAN differ between callers is the visitor's: whether a sentinel is fatal or
 * skipped, how rows are grouped into blocks, and where the walk stops early.
 */
#pragma once

#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Task.h>
#include <cstddef>
#include <memory>
#include <string_view>
#include <variant>

namespace bcos::ledger::mpt::history
{

/// A storage that can position an iterator at the first row at or after a key and walk forward.
/// Every whole-block walk and the startup rebuild need it; a plain point-read storage serves
/// neither.
///
/// Declared beside the walk rather than beside the store: it is the capability the walk requires,
/// and the store's own `QueryableStateStorage` refines it.
template <class Storage>
concept SeekableStateStorage = requires(Storage& storage, executor_v1::StateKey key) {
    { storage2::range(storage, storage2::RANGE_SEEK, key) } -> task::IsAwaitable;
};

namespace detail
{
/// Storage iterators hand back either a `StorageValueType<Value>` variant (MemoryStorage,
/// RocksDBStorage2) or a bare value. Reduce both to "the entry, or nullptr when this row carries a
/// deletion sentinel instead of bytes".
template <class RowValue>
inline const executor_v1::StateValue* asStateValue(RowValue const& value) noexcept
{
    if constexpr (requires { std::get_if<executor_v1::StateValue>(std::addressof(value)); })
    {
        return std::get_if<executor_v1::StateValue>(std::addressof(value));
    }
    else
    {
        return std::addressof(value);
    }
}
}  // namespace detail

/// Seek to (@p table, @p fromRowKey) and hand every row of @p table forward to @p visitor as
/// (row key view, entry or nullptr), stopping at the end of the table or when the visitor returns
/// false. Returns the payload bytes read.
///
/// @p fromRowKey is a RAW row key, not a block number, because the three callers start in two
/// different places: a block walk seeks at that block's meta row key, while the audit sweeps the
/// whole table from `""` — including rows an interrupted expiry left below the retention boundary,
/// which are exactly what it has to see and the rebuild deliberately does not.
///
/// Seeking at a block's META row key positions before its shards, because an 8-byte key sorts
/// before every 10-byte key sharing its first eight bytes — so one seek yields meta, shard 0,
/// shard 1, ... and then the next block. The iterator runs on past the table, so the VISITOR, not
/// the seek, defines where a walk ends.
///
/// The byte count includes only rows that carry bytes; a sentinel contributes nothing, which is
/// what makes `bytesScanned` "what was actually read" rather than "what was iterated over".
template <SeekableStateStorage Storage, class Visitor>
task::Task<std::size_t> walkShardTable(
    Storage& backend, std::string_view table, std::string_view fromRowKey, Visitor&& visitor)
{
    std::size_t bytesScanned = 0;
    auto iterator = co_await storage2::range(
        backend, storage2::RANGE_SEEK, executor_v1::StateKey{table, fromRowKey});
    while (true)
    {
        auto row = co_await iterator.next();
        if (!row)
        {
            break;
        }
        auto const& [rowKey, rowValue] = *row;
        executor_v1::StateKeyView rowKeyView{rowKey};
        if (rowKeyView.m_table != table)
        {
            break;
        }
        auto const* entry = detail::asStateValue(rowValue);
        if (entry != nullptr)
        {
            bytesScanned += entry->get().size();
        }
        if (!visitor(rowKeyView, entry))
        {
            break;
        }
    }
    co_return bytesScanned;
}

}  // namespace bcos::ledger::mpt::history
