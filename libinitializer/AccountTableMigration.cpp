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
#include <cstdio>
#include <filesystem>
#include <unistd.h>

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

void writeMarkerFile(std::string_view storageRootPath,
    bcos::initializer::AccountTableMigrationStats const& stats)
{
    auto const path = bcos::initializer::binaryAccountTablesMarkerPath(storageRootPath);
    // stdio + fsync: the marker is the migration's commit record, it must survive the same
    // power failure the synced RocksDB batches survive.
    std::unique_ptr<std::FILE, int (*)(std::FILE*)> file(
        std::fopen(path.c_str(), "w"), &std::fclose);
    if (!file)
    {
        throwMigrationFailure("cannot open marker file " + path);
    }
    auto const body = "binary account tables migrated: accountRows=" +
                      std::to_string(stats.migratedAccountRows) +
                      " registrations=" + std::to_string(stats.migratedRegistrations) +
                      " deduped=" + std::to_string(stats.dedupedRows) + "\n";
    if (std::fwrite(body.data(), 1, body.size(), file.get()) != body.size() ||
        std::fflush(file.get()) != 0 || ::fsync(::fileno(file.get())) != 0)
    {
        throwMigrationFailure("cannot write/fsync marker file " + path);
    }
}
}  // namespace

bcos::initializer::AccountTableMigrationStats bcos::initializer::migrateAccountTablesToBinary(
    ::rocksdb::DB& stateDB, std::string_view storageRootPath, bool hexOnlyLane)
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
    // Marker present: a completed migration. Skip the full-table scan — on an archive-scale
    // DB the scan itself is the expensive part, and there is nothing left to rename.
    if (std::filesystem::exists(binaryAccountTablesMarkerPath(storageRootPath)))
    {
        stats.alreadyMigrated = true;
        BCOS_LOG(INFO) << LOG_BADGE("AccountTableMigration")
                       << LOG_DESC("marker file present, migration already completed; skipping")
                       << LOG_KV("marker", binaryAccountTablesMarkerPath(storageRootPath));
        return stats;
    }

    uint64_t estimatedTotal = 0;
    stateDB.GetIntProperty("rocksdb.estimate-num-keys", &estimatedTotal);
    BCOS_LOG(INFO) << LOG_BADGE("AccountTableMigration")
                   << LOG_DESC("start hex->binary account-table migration")
                   << LOG_KV("estimatedKeys", estimatedTotal);

    // Snapshot the scan: renames and deletes land in batches while the iterator walks, and a
    // snapshot keeps the walk pinned to the pre-migration view.
    ::rocksdb::ReadOptions readOptions;
    readOptions.snapshot = stateDB.GetSnapshot();
    std::unique_ptr<::rocksdb::Iterator> it(stateDB.NewIterator(readOptions));

    ::rocksdb::WriteBatch batch;
    size_t pendingOps = 0;
    std::string targetValue;  // reused conflict-probe buffer

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
        BCOS_LOG(INFO) << LOG_BADGE("AccountTableMigration")
                       << LOG_DESC("progress") << LOG_KV("scanned", stats.scanned)
                       << LOG_KV("estimatedTotal", estimatedTotal)
                       << LOG_KV("accountRows", stats.migratedAccountRows)
                       << LOG_KV("registrations", stats.migratedRegistrations)
                       << LOG_KV("deduped", stats.dedupedRows);
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
                throwMigrationFailure("data conflict on account table " +
                                      std::string(hexTable) +
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
    // Key-ordering note: the binary rename targets can never feed back into this scan.
    // "/s/<20 bytes>:<field>" row targets start with '/' (0x2f) and sort before every
    // "s_tables:..." registration source (0x73), and even where families interleave the
    // iterator is pinned to the pre-migration snapshot, so freshly written targets are
    // invisible to it regardless of where they sort.
    for (it->SeekToFirst(); it->Valid(); it->Next())
    {
        auto const key = it->key();
        ++stats.scanned;
        if (key.size() > registrationPrefix.size() &&
            std::string_view(key.data(), registrationPrefix.size()) == registrationPrefix)
        {
            // Registration row: strip "s_tables:" and probe the registered table name.
            std::string_view const table(key.data() + ledger::SYS_TABLES.size() + 1,
                key.size() - ledger::SYS_TABLES.size() - 1);
            if (ledger::account::isHexAccountTableName(table))
            {
                queueRename(key,
                    std::string(ledger::SYS_TABLES) + ":" +
                        ledger::account::hexToBinaryAccountTableName(table),
                    table, true);
            }
            continue;
        }
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
    }
    if (!it->status().ok())
    {
        throwMigrationFailure("scan iterator failed: " + it->status().ToString());
    }
    flushBatch(true);  // final batch synced before the marker lands
    stateDB.ReleaseSnapshot(readOptions.snapshot);

    writeMarkerFile(storageRootPath, stats);
    BCOS_LOG(INFO) << LOG_BADGE("AccountTableMigration")
                   << LOG_DESC("migration completed") << LOG_KV("scanned", stats.scanned)
                   << LOG_KV("accountRows", stats.migratedAccountRows)
                   << LOG_KV("registrations", stats.migratedRegistrations)
                   << LOG_KV("deduped", stats.dedupedRows)
                   << LOG_KV("marker", binaryAccountTablesMarkerPath(storageRootPath));
    return stats;
}
