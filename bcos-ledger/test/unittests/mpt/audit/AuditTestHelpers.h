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
 * @file AuditTestHelpers.h
 * @brief Fixtures for the audit suites: seed a trie the normal way, then land its rows on the
 *        FLAT plane the audits scan, plus the row-level tampering the negative controls need
 */
#pragma once

#include "../TestHelpers.h"
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/mpt/Account.h>
#include <bcos-ledger/mpt/MPTReadView.h>
#include <bcos-ledger/mpt/PathKey.h>
#include <bcos-ledger/mpt/audit/PathTreeAudit.h>
#include <bcos-storage/RocksDBStorage2.h>
#include <bcos-storage/StateKVResolver.h>
#include <bcos-task/Task.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace bcos::ledger::mpt::audit::test
{

/// The plane the audits read: ordinary state rows, ordered, seekable. Same type the MPT tests
/// already use for the flat backend (test::FlatBackendStorage), reused here because a node row IS
/// an ordinary state row — that is the whole premise of path addressing (spec §8.2).
using AuditFlatStorage = mpt::test::FlatBackendStorage;

using RocksDbStateStorage = bcos::storage2::rocksdb::RocksDBStorage2<bcos::executor_v1::StateKey,
    bcos::executor_v1::StateValue, bcos::storage2::rocksdb::StateKeyResolver,
    bcos::storage2::rocksdb::StateValueResolver>;

/// One RocksDB directory, kept when the caller asked for the databases to survive.
struct AuditRocksDb
{
    std::string path;
    bool keep{};
    std::unique_ptr<::rocksdb::DB> db;

    AuditRocksDb(std::string dbPath, bool keepDb) : path(std::move(dbPath)), keep(keepDb)
    {
        ::rocksdb::Options options;
        options.create_if_missing = true;
        ::rocksdb::DB* raw = nullptr;
        auto const status = ::rocksdb::DB::Open(options, path, &raw);
        BOOST_REQUIRE(status.ok());
        db.reset(raw);
    }
    AuditRocksDb(const AuditRocksDb&) = delete;
    AuditRocksDb(AuditRocksDb&&) = delete;
    AuditRocksDb& operator=(const AuditRocksDb&) = delete;
    AuditRocksDb& operator=(AuditRocksDb&&) = delete;
    ~AuditRocksDb()
    {
        db.reset();  // the CLI opens the same directory, so the handle must be closed first
        if (!keep)
        {
            boost::filesystem::remove_all(path);
        }
    }
};

/// Where this case puts its databases, and whether they outlive it.
inline std::pair<std::string, bool> auditRocksDbBase()
{
    if (const auto* kept = std::getenv("MPT_AUDIT_TEST_DB_DIR"); kept != nullptr)
    {
        return {std::string(kept), true};
    }
    return {"./path-tree-audit-rocksdb-" + std::to_string(std::random_device{}()), false};
}

/// Copy every node row of @p nodes onto the flat plane @p flat, translating PathKey -> StateKey.
///
/// The trie builders write through a PathKey-keyed storage; production lands the same rows on the
/// flat plane via pathNodeStateKey during the block's single merge. Tests build the normal way
/// and then transcribe, so the bytes under audit are produced by the real builder rather than by
/// hand.
template <class Storage>
void landNodeRowsOnFlat(mpt::test::NodeMemoryStorage& nodes, Storage& flat)
{
    bcos::task::syncWait([&]() -> bcos::task::Task<void> {
        auto iterator = co_await bcos::storage2::range(nodes);
        while (true)
        {
            auto keyValue = co_await iterator.next();
            if (!keyValue)
            {
                break;
            }
            auto const& [key, value] = *keyValue;
            auto const* raw = std::get_if<bcos::bytes>(std::addressof(value));
            if (raw == nullptr)
            {
                continue;
            }
            bcos::storage::Entry entry;
            entry.set(std::string(raw->begin(), raw->end()));
            co_await bcos::storage2::writeOne(flat, pathNodeStateKey(key), std::move(entry));
        }
        co_return;
    }());
}

/// Raw bytes of the node row at @p key, or nullopt when there is none.
template <class Storage>
std::optional<bcos::bytes> readNodeRow(Storage& flat, PathKey const& key)
{
    return bcos::task::syncWait([&]() -> bcos::task::Task<std::optional<bcos::bytes>> {
        auto entry = co_await bcos::storage2::readOne(flat, pathNodeStateKey(key));
        if (!entry)
        {
            co_return std::nullopt;
        }
        auto const raw = entry->get();
        co_return bcos::bytes(raw.begin(), raw.end());
    }());
}

/// Put @p raw at @p key, creating or overwriting the row.
template <class Storage>
void writeNodeRow(Storage& flat, PathKey const& key, bcos::bytes const& raw)
{
    bcos::storage::Entry entry;
    entry.set(std::string(raw.begin(), raw.end()));
    bcos::task::syncWait(bcos::storage2::writeOne(flat, pathNodeStateKey(key), std::move(entry)));
}

/// Delete the row at @p key — the "one delete too many" corruption (G4).
template <class Storage>
void removeNodeRow(Storage& flat, PathKey const& key)
{
    bcos::task::syncWait(bcos::storage2::removeOne(flat, pathNodeStateKey(key)));
}

/// Flip the low bit of byte @p offset of @p raw.
inline bcos::bytes flipByte(bcos::bytes raw, std::size_t offset)
{
    raw.at(offset) ^= bcos::byte{0x01};
    return raw;
}

/// findDigest's "not present" answer.
inline constexpr std::size_t kDigestNotFound = static_cast<std::size_t>(-1);

/// Where @p needle first occurs in @p haystack. Used to find the 32-byte digest a parent records
/// for a child, so a negative control can corrupt exactly that field and nothing else.
inline std::size_t findDigest(bcos::bytes const& haystack, bcos::h256 const& needle)
{
    auto const found = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end());
    return found == haystack.end() ? kDigestNotFound :
                                     static_cast<std::size_t>(found - haystack.begin());
}

/// HasherT of @p raw — the digest a parent records for the child holding those bytes.
inline bcos::h256 digestOf(bcos::bytes const& raw)
{
    bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher hasher;
    return mpt::detail::nodeDigest(hasher, bcos::ref(raw));
}

/// Run the path-tree audit over @p flat, optionally against the root the chain commits to.
template <class Storage>
PathTreeAuditReport runPathTreeAudit(
    Storage& flat, std::optional<bcos::h256> expectedRoot = std::nullopt)
{
    return bcos::task::syncWait(auditPathTree(flat, expectedRoot));
}

/// An account holding @p slots in its storage trie: the storage trie is built under
/// accountKeyHash(addr) and the resulting root goes into the account, exactly as the builder
/// wires the two tries together (spec §8.3).
inline Account makeAccountWithStorage(mpt::test::NodeMemoryStorage& nodes,
    bcos::Address const& addr, std::map<bcos::h256, bcos::bytes> const& slots, bcos::u256 nonce = 1)
{
    Account account;
    account.nonce = nonce;
    account.balance = 42;
    if (!slots.empty())
    {
        account.storageRoot = mpt::test::seedTrieFlushed(
            nodes, emptyRootHash(), slots, TrieScope::storage(accountKeyHash(addr)))
                                  .root;
    }
    return account;
}

}  // namespace bcos::ledger::mpt::audit::test
