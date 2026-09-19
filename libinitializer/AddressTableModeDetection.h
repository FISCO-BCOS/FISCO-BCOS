#pragma once

#include <bcos-framework/ledger/AccountTableName.h>
#include <bcos-framework/ledger/Features.h>
#include <functional>
#include <string>
#include <string_view>

namespace rocksdb
{
class DB;
}

namespace bcos::initializer
{
/// Marker file in the state-DB directory (NodeConfig::storagePath()) declaring that the
/// account tables have been fully migrated to the binary layout. Written by
/// AccountTableMigration (migrateAccountTablesToBinary) after the final synced batch;
/// read here at boot.
inline constexpr std::string_view BINARY_ACCOUNT_TABLES_MARKER = ".binary_account_tables";

/// <storageRootPath>/.binary_account_tables — the one shared spelling of the marker path.
std::string binaryAccountTablesMarkerPath(std::string_view storageRootPath);

/// What the state RocksDB physically holds, as seen by the boot-time scan
/// (detectAccountTableLayout).
struct AccountTableLayout
{
    bool markerFile = false;     ///< .binary_account_tables present in the state-DB dir
    bool sawHexTables = false;   ///< an s_tables:/apps/<40 lowercase hex> registration row
    bool sawBinaryTables = false;  ///< an s_tables:/apps/<20 raw bytes> registration row
};

/// Scan the state RocksDB for the account-table registration rows and check the marker
/// file. Every account table is registered in s_tables (EVMAccount::create writes the
/// SYS_TABLES row; the legacy executor's table-commit path registers every table it
/// touches), and the registration row's key IS the table name, so the physical key prefix
/// "s_tables:/apps/" enumerates every account table that has ever been created.
AccountTableLayout detectAccountTableLayout(
    ::rocksdb::DB& stateDB, std::string_view storageRootPath);

/// The executor lanes whose account-table writes are hex-only: the OP lane
/// (feature_l2_ethereum_compat / executor_version >= OPSTACK_EXECUTOR_VERSION — the
/// Storage2State bridge derives /apps/<40-hex> names itself), the Eth engine lane
/// (executor_version == ETHEREUM_EXECUTOR_VERSION — ethereum-executor's EthereumState
/// hard-codes AddressTableMode::Hex) and the legacy bcos-executor lane (executor_version
/// == 0 — SchedulerManager's executor names hex tables directly). Only the baseline v1
/// lane (executor_version == 1, no L2 flag) routes every account-table name through
/// EVMAccount's mode routing.
bool isHexOnlyExecutorLane(const ledger::Features& features, int executorVersion);

/// Resolve the node-local account-table mode from the physical layout and the lane:
///   - hex-only lane: forced Hex; binary evidence (marker file or binary tables) is a loud
///     boot failure (throws bcos::tool::InvalidConfig with recovery instructions) — those
///     executors would split reads/writes onto disjoint tables;
///   - marker file → Binary (migration completed);
///   - binary only → Binary; hex only → Hex (every existing chain);
///   - neither (a brand-new chain) → Binary: the new encoding by default. The normalized
///     Entry::hash folds binary names to their hex form, so the genesis state root is
///     byte-identical either way — no fork risk from the default.
///
/// A MIXED layout (hex AND binary registrations) has no legal mode: it means an interrupted
/// hex→binary migration (or a hand-mixed backup), handled by the caller at boot — resume the
/// migration when [storage] migrate_account_tables_to_binary is set, refuse to start
/// otherwise. resolveNodeAddressTableMode throws on it as a defensive invariant.
///
/// @throws bcos::tool::InvalidConfig on the hex-only-lane/binary-data combination and on a
///         mixed layout.
ledger::account::AddressTableMode resolveNodeAddressTableMode(
    AccountTableLayout const& layout, bool hexOnlyLane);

/// Boot-time account-table handling, carried from Initializer to LedgerInitializer::build:
/// the already-open state DB (RocksDB's single-instance lock forbids a second open) plus the
/// one-shot migration switch ([storage] migrate_account_tables_to_binary). The boot sequence
/// inside LedgerInitializer::build is: lane check → layout detection → (a mixed layout =
/// interrupted migration: resume it when the switch is on, refuse to start otherwise) →
/// optional one-shot migration → re-detection → mode publication, all before the genesis
/// block is built. The lane verdict needs the ledger's (decryption-aware) reads of the
/// on-chain config rows, so none of this can happen in Initializer ahead of the ledger
/// construction.
struct AccountTableBoot
{
    std::reference_wrapper<::rocksdb::DB> stateDB;
    std::string storageRootPath;
    bool migrateToBinary = false;
};
}  // namespace bcos::initializer
