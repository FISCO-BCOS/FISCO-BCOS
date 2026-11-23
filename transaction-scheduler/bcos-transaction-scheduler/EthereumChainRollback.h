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
 * Shared per-block undo log for the execution lanes.
 */
#pragma once

#include "bcos-codec/rlp/RLPDecode.h"
#include "bcos-codec/rlp/RLPEncode.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-ledger/EngineMeta.h"
#include "bcos-storage/KeyPrefixes.h"
#include "bcos-task/Task.h"
#include <boost/throw_exception.hpp>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace bcos::scheduler_v1
{

/// One journaled row: the pre-block value of (table, key); nullopt oldValue = the row did
/// not exist before the block, so a rollback DELETES it.
struct RollbackJournalEntry
{
    std::string table;
    std::string key;
    std::optional<bcos::bytes> oldValue;
};

/// ADL hooks so the shared RLP codec (bcos-codec/rlp) handles RollbackJournalEntry inside
/// vectors/optionals: an entry encodes as the list [table, key, oldValue?] — the optional
/// is simply absent from the list tail when the row did not exist.
inline size_t length(RollbackJournalEntry const& entry) noexcept
{
    return bcos::codec::rlp::length(entry.table, entry.key, entry.oldValue);
}
inline void encode(bcos::bytes& to, RollbackJournalEntry const& entry) noexcept
{
    bcos::codec::rlp::encode(to, entry.table, entry.key, entry.oldValue);
}
inline void decode(bcos::bytesRef& from, RollbackJournalEntry& entry)
{
    bcos::codec::rlp::decode(from, entry.table, entry.key, entry.oldValue);
}

struct RollbackJournal
{
    static constexpr uint64_t VERSION = 2;
    ledger::BlockAnchor block;
    h256 parentHash;
    h256 preBlockStateRoot;
    std::vector<RollbackJournalEntry> entries;
};

inline bytes encodeRollbackJournal(RollbackJournal const& journal)
{
    bytes out;
    codec::rlp::encode(out, RollbackJournal::VERSION, journal.block, journal.parentHash,
        journal.preBlockStateRoot, journal.entries);
    return out;
}

struct RollbackRefused : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

struct FinalizedRollbackRefused : RollbackRefused
{
    using RollbackRefused::RollbackRefused;
};

inline void decode(bytesRef& in, RollbackJournal& journal)
{
    uint64_t version = 0;
    codec::rlp::decode(
        in, version, journal.block, journal.parentHash, journal.preBlockStateRoot, journal.entries);
    if (version != RollbackJournal::VERSION)
    {
        throw RollbackRefused("Unsupported rollback journal version");
    }
}

inline RollbackJournal decodeRollbackJournal(bytesConstRef encoded)
{
    RollbackJournal journal;
    codec::rlp::decodeExact(encoded, journal);
    return journal;
}

inline executor_v1::StateKey journalKey(protocol::BlockNumber number)
{
    return {ledger::SYS_ROLLBACK_JOURNAL, std::to_string(number)};
}

inline bool isJournaledTable(std::string_view table)
{
    return table != storage2::kMPTTable && table != ledger::SYS_CODE_BINARY &&
           table != ledger::SYS_ROLLBACK_JOURNAL;
}

/// Stage undo and the new disk tip in the block's ledger layer, before adding prune rows.
task::Task<void> stageReversibleBlock(auto& committed, auto& state, auto& ledgerRows,
    ledger::PersistedEngineMeta meta, ledger::BlockAnchor block, h256 parentHash,
    h256 preBlockStateRoot)
{
    if (meta.diskTip.number != block.number - 1 || meta.diskTip.hash != parentHash)
    {
        throw RollbackRefused("Block does not extend the committed engine tip");
    }
    RollbackJournal journal{block, parentHash, preBlockStateRoot, {}};
    auto collect = [](auto& layer, std::set<executor_v1::StateKey>& keys) -> task::Task<void> {
        auto rows = co_await storage2::range(layer);
        while (auto row = co_await rows.next())
        {
            auto const& [key, value] = *row;
            if (isJournaledTable(executor_v1::StateKeyView{key}.m_table))
            {
                keys.insert(key);
            }
        }
    };
    std::set<executor_v1::StateKey> keys;
    co_await collect(state, keys);
    co_await collect(ledgerRows, keys);
    auto oldEntries = co_await storage2::readSome(committed, keys);
    auto oldEntry = oldEntries.begin();
    journal.entries.reserve(keys.size());
    for (auto const& key : keys)
    {
        auto const keyView = executor_v1::StateKeyView{key};
        auto& entry = journal.entries.emplace_back(
            std::string(keyView.m_table), std::string(keyView.m_key), std::nullopt);
        if (*oldEntry)
        {
            auto raw = (*oldEntry)->get();
            entry.oldValue.emplace(raw.begin(), raw.end());
        }
        ++oldEntry;
    }

    storage::Entry row;
    row.set(encodeRollbackJournal(journal));
    co_await storage2::writeOne(ledgerRows, journalKey(block.number), std::move(row));
    meta.diskTip = block;
    co_await ledger::writeEngineMeta(ledgerRows, meta);
}

inline void rewindEngineMeta(ledger::PersistedEngineMeta& meta, ledger::BlockAnchor ancestor)
{
    meta.diskTip = ancestor;
    if (meta.safe && meta.safe->number > ancestor.number)
    {
        meta.safe = meta.finalized;
    }
}

task::Task<void> stageFinalization(auto& batch, ledger::PersistedEngineMeta const& before,
    ledger::PersistedEngineMeta const& after)
{
    for (auto number = before.finalizedNumber(); number < after.finalizedNumber();)
    {
        co_await storage2::removeOne(batch, journalKey(++number));
    }
    co_await ledger::writeEngineMeta(batch, after);
}

/// Build an undo layer without writing to the committed plane. Descending traversal lets
/// older preimages overwrite later ones and also validates each link before it is used.
template <class MutableStorage>
task::Task<MutableStorage> buildRollbackBatch(auto& committed, ledger::BlockAnchor ancestor,
    ledger::PersistedEngineMeta const& meta, protocol::BlockFactory& blockFactory)
{
    if (ancestor.number < meta.finalizedNumber() || ancestor.number >= meta.diskTip.number)
    {
        throw RollbackRefused("Rollback target must be between finalized and the disk tip");
    }

    auto headerAt = [&](ledger::BlockAnchor anchor) -> task::Task<protocol::BlockHeader::Ptr> {
        auto const number = anchor.number;
        try
        {
            auto block =
                co_await ledger::getBlockData(committed, number, ledger::HEADER, blockFactory);
            if (block && block->blockHeader() && block->blockHeader()->number() == number &&
                block->blockHeader()->hash() == anchor.hash)
            {
                co_return block->blockHeader();
            }
        }
        catch (std::exception const& error)
        {
            throw RollbackRefused("Cannot read rollback header at height " +
                                  std::to_string(number) + ": " + error.what());
        }
        throw RollbackRefused("Invalid rollback header at height " + std::to_string(number));
    };

    MutableStorage batch;
    auto child = meta.diskTip;
    auto childHeader = co_await headerAt(child);
    while (child.number > ancestor.number)
    {
        auto entry = co_await storage2::readOne(committed, journalKey(child.number));
        if (!entry)
        {
            throw RollbackRefused(
                "Missing rollback journal at height " + std::to_string(child.number));
        }
        RollbackJournal journal;
        try
        {
            auto raw = entry->get();
            journal = decodeRollbackJournal(
                bytesConstRef(reinterpret_cast<byte const*>(raw.data()), raw.size()));
        }
        catch (std::exception const& error)
        {
            throw RollbackRefused("Invalid rollback journal at height " +
                                  std::to_string(child.number) + ": " + error.what());
        }

        auto parentNumber = child.number - 1;
        auto parentHash =
            co_await ledger::getBlockHash(committed, parentNumber, ledger::fromStorage);
        if (!parentHash)
        {
            throw RollbackRefused(
                "Missing canonical hash at height " + std::to_string(parentNumber));
        }
        auto parent = ledger::BlockAnchor{parentNumber, *parentHash};
        auto parentHeader = co_await headerAt(parent);
        if (journal.block != child || journal.parentHash != *parentHash ||
            childHeader->parentInfo().blockHash != *parentHash ||
            childHeader->parentInfo().blockNumber != parentNumber ||
            journal.preBlockStateRoot != parentHeader->stateRoot())
        {
            throw RollbackRefused("Rollback journal does not match canonical chain at height " +
                                  std::to_string(child.number));
        }
        for (auto const& row : journal.entries)
        {
            if (!isJournaledTable(row.table) ||
                (row.table == ledger::SYS_CURRENT_STATE && row.key == ledger::ENGINE_META_KEY))
            {
                throw RollbackRefused("Rollback journal contains a reserved row");
            }
            executor_v1::StateKey key{row.table, row.key};
            if (row.oldValue)
            {
                storage::Entry restore;
                restore.set(*row.oldValue);
                co_await storage2::writeOne(batch, std::move(key), std::move(restore));
            }
            else
            {
                co_await storage2::removeOne(batch, std::move(key));
            }
        }
        co_await storage2::removeOne(batch, journalKey(child.number));
        child = parent;
        childHeader = std::move(parentHeader);
    }
    if (child != ancestor)
    {
        throw RollbackRefused("Rollback target hash does not match canonical chain");
    }
    co_return batch;
}

template <class GlobalStateStorage>
task::Task<typename GlobalStateStorage::ViewType> forkRollbackView(GlobalStateStorage& storage,
    ledger::BlockAnchor ancestor, ledger::PersistedEngineMeta const& meta,
    protocol::BlockFactory& blockFactory)
{
    using MutableStorage = typename GlobalStateStorage::MutableStorage;
    auto view = storage.forkCommitted();
    auto batch = co_await buildRollbackBatch<MutableStorage>(view, ancestor, meta, blockFactory);
    view.m_immutableStorages.push_front(std::make_shared<MutableStorage>(std::move(batch)));
    co_return view;
}

struct RollbackResult
{
    protocol::BlockNumber oldHead;
    protocol::BlockNumber newHead;
};

}  // namespace bcos::scheduler_v1
