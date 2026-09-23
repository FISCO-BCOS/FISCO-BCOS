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
 * @file AccountTableMigration.cpp
 * @brief One-shot hex→binary account-table migration of the state RocksDB.
 */
#include "AccountTableMigration.h"
#include "AddressTableModeDetection.h"
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-tool/Exceptions.h>
#include <bcos-utilities/BoostLog.h>
#include <rocksdb/db.h>
#include <rocksdb/iterator.h>
#include <rocksdb/snapshot.h>
#include <rocksdb/write_batch.h>
#include <boost/throw_exception.hpp>
#include <chrono>

namespace
{
constexpr size_t kBatchSize = 10000;  // write ops per WriteBatch

[[noreturn]] void throwMigrationFailure(std::string const& reason)
{
    BOOST_THROW_EXCEPTION(
        bcos::tool::InvalidConfig() << bcos::errinfo_comment(
            "account-table hex->binary migration failed: " + reason +
            ". The state DB is left partially migrated (a mixed layout; there is no runtime "
            "mixed mode): fix the cause and restart with [storage] "
            "migrate_account_tables_to_binary=true to resume the migration"));
}
}  // namespace

bcos::initializer::AccountTableMigrationStats bcos::initializer::migrateAccountTablesToBinary(
    ::rocksdb::DB& stateDB, bool hexOnlyLane)
{
    if (hexOnlyLane)
    {
        BOOST_THROW_EXCEPTION(
            bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                "[storage] migrate_account_tables_to_binary is set, but the chain runs a "
                "hex-only executor lane (OP / Eth engine / legacy executor): those executors "
                "name account tables /apps/<40-hex> directly, and migrating the tables to the "
                "binary layout would split their reads and writes onto disjoint tables. "
                "Remove the flag (or switch the chain to the baseline executor, "
                "executor_version = 1, before migrating)"));
    }

    AccountTableMigrationStats stats;
    // Flag already "bin": a completed migration. Skip the full-table scan — on an
    // archive-scale DB the scan itself is the expensive part, and there is nothing left
    // to rename.
    if (auto const flag = readAccountTableLayoutFlag(stateDB);
        flag.has_value() && *flag == ACCOUNT_TABLE_LAYOUT_BINARY)
    {
        stats.alreadyMigrated = true;
        BCOS_LOG(INFO) << LOG_BADGE("AccountTableMigration")
                       << LOG_DESC("layout flag is already \"bin\"; skipping")
                       << LOG_KV("key", ACCOUNT_TABLE_LAYOUT_KEY);
        return stats;
    }

    uint64_t estimatedTotal = 0;
    stateDB.GetIntProperty("rocksdb.estimate-num-keys", &estimatedTotal);
    BCOS_LOG(INFO) << LOG_BADGE("AccountTableMigration")
                   << LOG_DESC("start hex->binary account-table migration")
                   << LOG_KV("estimatedKeys", estimatedTotal);

    // "migrating" BEFORE the first mutation. The registration rows cannot witness an
    // interruption in the account-row phase (every /apps/ row sorts before the first
    // s_tables:/apps/ registration), so this synced flag is the only durable record that
    // a migration has started but not finished: a crash from here on leaves it behind,
    // and the next boot either resumes (switch on — the twin-dedup below makes the resume
    // idempotent) or refuses to publish a mode over a half-migrated DB (switch off).
    writeAccountTableLayoutFlag(stateDB, ACCOUNT_TABLE_LAYOUT_MIGRATING);

    // Snapshot the scan: renames and deletes land in batches while the iterator walks, and a
    // snapshot keeps the walk pinned to the pre-migration view. RAII release: every
    // throwMigrationFailure path must unpin it too, not just the success path.
    struct SnapshotRelease
    {
        ::rocksdb::DB* db;
        void operator()(::rocksdb::Snapshot const* s) const { db->ReleaseSnapshot(s); }
    };
    ::rocksdb::ReadOptions readOptions;
    std::unique_ptr<::rocksdb::Snapshot const, SnapshotRelease> snapshotGuard(
        stateDB.GetSnapshot(), SnapshotRelease{&stateDB});
    readOptions.snapshot = snapshotGuard.get();
    std::unique_ptr<::rocksdb::Iterator> it(stateDB.NewIterator(readOptions));

    ::rocksdb::WriteBatch batch;
    size_t pendingOps = 0;
    std::string targetValue;  // reused conflict-probe buffer

    // Progress reporting: a migration over an archive-scale DB can run for a long time at
    // boot with the node otherwise silent, so report progress periodically. Time-throttled
    // (not per-batch): batch flushes only happen on renames, so a DB whose account rows are
    // a minority of keys would otherwise log nothing for long stretches. The scan below is
    // bounded to the two migratable ranges, so there is no honest total to divide by —
    // report scanned keys, throughput and the renamed rows instead of a fake percentage.
    auto const startTime = std::chrono::steady_clock::now();
    auto lastProgressLog = startTime;
    auto logProgress = [&]() {
        auto const now = std::chrono::steady_clock::now();
        if (now - lastProgressLog < std::chrono::seconds(5))
        {
            return;
        }
        lastProgressLog = now;
        auto const elapsed =
            std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();
        BCOS_LOG(INFO) << LOG_BADGE("AccountTableMigration") << LOG_DESC("migration in progress")
                       << LOG_KV("scanned", stats.scanned) << LOG_KV("elapsedSec", elapsed)
                       << LOG_KV("keysPerSec", elapsed > 0 ? stats.scanned / elapsed : 0)
                       << LOG_KV("accountRows", stats.migratedAccountRows)
                       << LOG_KV("registrations", stats.migratedRegistrations)
                       << LOG_KV("deduped", stats.dedupedRows);
    };

    auto flushBatch = [&](bool sync) {
        if (pendingOps == 0)
        {
            return;
        }
        ::rocksdb::WriteOptions writeOptions;
        writeOptions.sync = sync;
        auto status = stateDB.Write(writeOptions, &batch);
        if (!status.ok())
        {
            throwMigrationFailure("WriteBatch failed: " + status.ToString());
        }
        batch.Clear();
        pendingOps = 0;
    };

    // Queue the rename hexKey → binaryKey (both under hexTable), resolving an existing
    // binary twin first. The probe stays unconditional: on a resume the twin may
    // legitimately exist (interrupted previous run), and even on a first run it is the
    // only guard against a hand-mixed backup that already carries /s/ rows — the cost is
    // one point lookup per migrated row against the snapshot, cheap next to the batched
    // writes.
    auto queueRename = [&](::rocksdb::Slice const& hexKey, std::string const& binaryKey,
                           std::string_view hexTable, bool isRegistration) {
        targetValue.clear();
        auto getStatus = stateDB.Get(readOptions, binaryKey, &targetValue);
        if (getStatus.ok())
        {
            if (targetValue != it->value())
            {
                throwMigrationFailure("data conflict on account table " + std::string(hexTable) +
                                      ": the binary twin key already exists with a different "
                                      "value");
            }
            // Interrupted previous run: the twin is already in place, drop the hex source.
            batch.Delete(hexKey);
            ++stats.dedupedRows;
            ++pendingOps;
        }
        else if (getStatus.IsNotFound())
        {
            batch.Put(binaryKey, it->value());
            batch.Delete(hexKey);
            if (isRegistration)
            {
                ++stats.migratedRegistrations;
            }
            else
            {
                ++stats.migratedAccountRows;
            }
            pendingOps += 2;
        }
        else
        {
            throwMigrationFailure("conflict probe read failed: " + getStatus.ToString());
        }
        if (pendingOps >= kBatchSize)
        {
            flushBatch(false);
        }
    };

    constexpr std::string_view registrationPrefix = "s_tables:/apps/";
    constexpr size_t hexTableNameSize =
        ledger::account::APPS_PREFIX.size() + ledger::account::HEX_ADDRESS_SIZE;
    // Exclusive upper bounds: increment the trailing '/' (0x2f → 0x30 '0').
    auto const prefixSuccessor = [](std::string_view prefix) {
        std::string successor(prefix);
        ++successor.back();
        return successor;
    };
    std::string const accountRowsUpper = prefixSuccessor(ledger::account::APPS_PREFIX);
    std::string const registrationsUpper = prefixSuccessor(registrationPrefix);
    // The migratable rows live in exactly two contiguous prefix ranges — the account rows
    // under "/apps/" and their registrations under "s_tables:/apps/" ('/' is 0x2f, so '0'
    // is the prefix successor and "<prefix>0" the exclusive upper bound). Nothing else in
    // the DB — the /mpt/ trie nodes that dominate the key count on an MPT chain, /sys/
    // rows, code/abi blobs — is walked. The binary rename targets can never feed back into
    // either scan: "/s/<20 bytes>:<field>" rows sort after "/apps0", the "s_tables:/s/"
    // registrations after "s_tables:/apps0", and the iterator is pinned to the
    // pre-migration snapshot anyway, so freshly written targets are invisible regardless.
    auto scanRange = [&](std::string_view lower, std::string_view upper, auto&& onKey) {
        auto const upperSlice = ::rocksdb::Slice(upper.data(), upper.size());
        for (it->Seek(::rocksdb::Slice(lower.data(), lower.size())); it->Valid(); it->Next())
        {
            auto const key = it->key();
            if (key.compare(upperSlice) >= 0)
            {
                break;
            }
            ++stats.scanned;
            // Cheap pre-check: the clock read is throttled to 1/1024 keys, the log itself
            // to one line per 5s.
            if ((stats.scanned & 0x3FF) == 0)
            {
                logProgress();
            }
            onKey(key);
        }
    };
    scanRange(std::string(ledger::account::APPS_PREFIX), accountRowsUpper,
        [&](::rocksdb::Slice const& key) {
            // Account-table row: "/apps/<40hex>:<field>" — the ':' separator sits exactly after
            // the 46-char table name.
            if (key.size() > hexTableNameSize + 1 && key[hexTableNameSize] == ':' &&
                ledger::account::isHexAccountTableName(
                    std::string_view(key.data(), hexTableNameSize)))
            {
                std::string_view const hexTable(key.data(), hexTableNameSize);
                std::string binaryKey = ledger::account::hexToBinaryAccountTableName(hexTable);
                binaryKey.append(key.data() + hexTableNameSize, key.size() - hexTableNameSize);
                queueRename(key, binaryKey, hexTable, false);
            }
        });
    scanRange(registrationPrefix, registrationsUpper, [&](::rocksdb::Slice const& key) {
        // Registration row: strip "s_tables:" and probe the registered table name.
        std::string_view const table(
            key.data() + ledger::SYS_TABLES.size() + 1, key.size() - ledger::SYS_TABLES.size() - 1);
        if (ledger::account::isHexAccountTableName(table))
        {
            queueRename(key,
                std::string(ledger::SYS_TABLES) + ":" +
                    ledger::account::hexToBinaryAccountTableName(table),
                table, true);
        }
    });
    if (!it->status().ok())
    {
        throwMigrationFailure("scan iterator failed: " + it->status().ToString());
    }
    // The "bin" verdict rides the final SYNCED batch: atomically with the last renames, so
    // a durable "bin" implies every rename is durable, and a crash at any earlier point
    // leaves "migrating" — the next boot resumes (switch on) or refuses (switch off).
    batch.Put(::rocksdb::Slice(ACCOUNT_TABLE_LAYOUT_KEY.data(), ACCOUNT_TABLE_LAYOUT_KEY.size()),
        ::rocksdb::Slice(ACCOUNT_TABLE_LAYOUT_BINARY.data(), ACCOUNT_TABLE_LAYOUT_BINARY.size()));
    ++pendingOps;
    flushBatch(true);

    BCOS_LOG(INFO) << LOG_BADGE("AccountTableMigration") << LOG_DESC("migration completed")
                   << LOG_KV("scanned", stats.scanned)
                   << LOG_KV("accountRows", stats.migratedAccountRows)
                   << LOG_KV("registrations", stats.migratedRegistrations)
                   << LOG_KV("deduped", stats.dedupedRows)
                   << LOG_KV("elapsedSec", std::chrono::duration_cast<std::chrono::seconds>(
                                               std::chrono::steady_clock::now() - startTime)
                                               .count())
                   << LOG_KV("layoutFlag", ACCOUNT_TABLE_LAYOUT_KEY);
    return stats;
}
