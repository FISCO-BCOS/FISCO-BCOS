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
 * @file AccountTableMigration.h
 * @brief One-shot hex→binary account-table migration of the state RocksDB, driven by
 *        [storage] migrate_account_tables_to_binary.
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace rocksdb
{
class DB;
}

namespace bcos::initializer
{
struct AccountTableMigrationStats
{
    uint64_t scanned = 0;                ///< physical keys visited
    uint64_t migratedAccountRows = 0;    ///< /apps/<40hex>:<field> rows renamed to binary
    uint64_t migratedRegistrations = 0;  ///< s_tables:/apps/<40hex> rows renamed to binary
    uint64_t dedupedRows = 0;  ///< hex sources dropped because the binary twin already held
                               ///< the same value (interrupted previous run)
    bool alreadyMigrated = false;  ///< layout flag already "bin": no scan happened at all
};

/// Rename every hex-layout account-table row in the state DB to its binary-layout twin:
///   - "/apps/<40 lowercase hex>:<field>" → "/s/<20 raw bytes>:<field>" (the ':' and the
///     field part are preserved byte-for-byte);
///   - "s_tables:/apps/<40hex>"           → "s_tables:/s/<20bin>" (the registration row).
/// Everything else (/sys/ rows, /mpt/ trie nodes, "/apps/<hex>_accessAuth" auth tables —
/// 51 chars, not 40, so the length probe in AccountTableName.h excludes them — short-name
/// contract tables, and any "/apps/" table whose name is not exactly 40 hex chars, e.g. a
/// 20-char BFS table) is left untouched.
///
/// Idempotent and crash-safe, tracked by the ACCOUNT_TABLE_LAYOUT_KEY flag inside the
/// state DB itself (AddressTableModeDetection.h) — a RocksDB checkpoint/backup therefore
/// carries the state machine together with the data:
///   1. "migrating" is written (synced) BEFORE the first WriteBatch is flushed — the
///      registration scan cannot witness an interruption in the account-row phase (all
///      /apps/ rows sort before the first s_tables:/apps/ registration), so this flag is
///      the only durable record of a started-but-unfinished migration;
///   2. "bin" is written INSIDE the final SYNCED batch — atomically with the last renames:
///      a durable "bin" implies every rename is durable. A crash at any earlier point
///      leaves "migrating", which the next boot resolves explicitly: resume the migration
///      when the switch stays on (the twin-dedup makes the resume idempotent), refuse to
///      start otherwise — on every executor lane. There is no runtime mixed mode, and
///      silently publishing Hex over a half-migrated DB would read every migrated account
///      as absent.
/// A binary target already holding the SAME value means an interrupted previous run — the
/// hex source is deleted and the scan continues; a DIFFERENT value is a data conflict and
/// aborts the boot (bcos::tool::InvalidConfig).
///
/// @param hexOnlyLane the chain's executor lane is hex-only (OP / Eth engine / legacy v2
///        executor — isHexOnlyExecutorLane): migration is refused loudly, those executors
///        name account tables /apps/<40-hex> themselves.
///
/// @throws bcos::tool::InvalidConfig on a hex-only lane, a value conflict, or any RocksDB
///         failure.
AccountTableMigrationStats migrateAccountTablesToBinary(
    ::rocksdb::DB& stateDB, bool hexOnlyLane);
}  // namespace bcos::initializer
