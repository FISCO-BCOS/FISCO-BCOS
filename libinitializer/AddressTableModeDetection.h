#pragma once

#include <bcos-framework/ledger/AccountTableName.h>
#include <bcos-framework/ledger/Features.h>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace rocksdb
{
class DB;
}

namespace bcos::initializer
{
/// The node-local account-table layout state machine lives in ONE key of the state RocksDB
/// itself — not in marker files next to it — so a RocksDB checkpoint/backup carries the
/// state machine together with the data it describes (a file-based marker is lost by a
/// DB-level backup, and restoring such a backup taken mid-migration would boot Hex
/// silently over half-migrated data). The key is written with sync=true and never flows
/// through the block-execution storage layers, so it never enters any block's write set
/// and never touches the (encoding-agnostic) state root. Its name sits outside every
/// table namespace: no '/' prefix (BFS can only create "/apps/", "/tables/", "/usr/"
/// tables), it sorts before "s_tables:" so the registration probe below skips it, and it
/// matches neither the hex nor the binary account-table shapes the migration scans for.
inline constexpr std::string_view ACCOUNT_TABLE_LAYOUT_KEY = "s_node_local:account_table_layout";

/// Values of ACCOUNT_TABLE_LAYOUT_KEY. A chain that predates this mechanism has NO key —
/// absence IS the hex verdict: pre-3.18 chains only ever wrote hex account tables
/// (feature_raw_address never shipped on any chain), so the steady-state boot path is a
/// single point Get with no registration scan at all.
inline constexpr std::string_view ACCOUNT_TABLE_LAYOUT_BINARY = "bin";
inline constexpr std::string_view ACCOUNT_TABLE_LAYOUT_MIGRATING = "migrating";

/// Point read of the layout flag: nullopt = a pre-flag (hex) chain or a brand-new DB,
/// "migrating" = an interrupted hex→binary migration, "bin" = a binary-layout DB
/// (migrated, or born binary). One Get, no scan.
/// @throws bcos::tool::InvalidConfig on a RocksDB read failure.
std::optional<std::string> readAccountTableLayoutFlag(::rocksdb::DB& stateDB);

/// Synced write of the layout flag (the same durability the fsync'd marker files had).
/// @throws bcos::tool::InvalidConfig on write failure.
void writeAccountTableLayoutFlag(::rocksdb::DB& stateDB, std::string_view value);

/// Has this DB ever held a chain (any "s_tables:" registration)? A single bounded Seek —
/// the only probe left on the flag-absent path, used to tell a pre-flag hex chain (Hex)
/// from a brand-new DB (born in the resolved encoding). Genesis always registers the
/// system tables, so the probe is exact; the flag key itself sorts before "s_tables:"
/// and cannot count as chain state.
bool hasAnyTableRegistration(::rocksdb::DB& stateDB);

/// Does this DB hold binary-layout account tables (an "s_tables:/s/<20 bytes>"
/// registration)? A single bounded Seek — one half of the cross-check on the flag-absent
/// path: the flag is the only LEGAL witness of a binary layout, but it is one key, and
/// anything that copies the /s/ rows without s_node_local:* (a partial backup/restore)
/// would otherwise boot Hex silently over binary data. Flag present → never called.
bool hasBinaryTableRegistration(::rocksdb::DB& stateDB);

/// The other half of that cross-check: does this DB hold binary account ROWS
/// ("/s/<20 bytes>:<field>")? A migration crashed in its account-row phase holds exactly
/// this shape — registrations all hex, data rows already renamed — which the registration
/// probe cannot see. A single bounded Seek with the fixed-offset separator shape check
/// (StateKey's splitPosition rule: ':' at offset 23). Flag present → never called.
bool hasBinaryAccountRow(::rocksdb::DB& stateDB);

/// Refuse to boot when the layout flag is absent but the DB holds binary-layout
/// registrations OR account rows (the flag was lost, e.g. by a backup/restore that
/// dropped the s_node_local keys): booting Hex would read every migrated account as
/// absent and fork the node's roots from the chain. No-op when the flag is present or no
/// binary data exists. Costs two bounded Seeks, only on the flag-absent boot path.
/// @throws bcos::tool::InvalidConfig with the resume instructions.
void refuseBinaryDataWithoutFlag(
    ::rocksdb::DB& stateDB, std::optional<std::string> const& layoutFlag);

/// The executor lane whose account-table writes are hex-only: the legacy bcos-executor
/// lane (executor_version == 0 — SchedulerManager's executor names hex tables directly).
/// Every other lane is mode-aware: the baseline v1 lane routes through EVMAccount's mode
/// routing, and the Eth engine / OP lanes derive their names through
/// account::ethLaneAccountTableName (the lane's /apps/ logical rule, no /sys/ routing,
/// re-encoded to the node-local layout — EVMAccount.h), so they run on either encoding.
bool isHexOnlyExecutorLane(int executorVersion);

/// Resolve the node-local account-table mode from the layout flag and the lane:
///   - hex-only lane: any flag value is binary-layout evidence → a loud boot failure
///     (throws bcos::tool::InvalidConfig with recovery instructions) — those executors
///     would split reads/writes onto disjoint tables; absent flag → Hex;
///   - "bin" → Binary;
///   - "migrating" → throw: an unfinished migration. Resume by setting [storage]
///     migrate_account_tables_to_binary and restarting (the caller does exactly that when
///     the switch is on, so this throw is the switch-OFF refusal), or roll the state DB
///     back to a pre-migration snapshot. There is no runtime mixed mode, and silently
///     publishing Hex over a half-migrated DB would read every migrated account as
///     absent;
///   - an unknown value → throw (forward compatibility: a state written by a newer
///     binary must not be guessed at);
///   - absent: an existing chain (chainHasState) is Hex — it predates the mechanism. The
///     caller cross-checks this verdict with two bounded probes first
///     (hasBinaryTableRegistration + hasBinaryAccountRow: a binary registration, or a
///     binary account row left by a migration crashed in its account-row phase): a
///     binary-layout DB whose flag was lost (a partial backup/restore) must refuse to
///     boot, not publish Hex over data it cannot read. A brand-new DB is born Binary (the
///     normalized Entry::hash folds binary names back to hex, so the genesis state root is
///     byte-identical either way — no fork risk from the default). The caller persists "bin" BEFORE
///     building genesis, so binary tables never exist without the flag: a crash in between would
///     otherwise read as a pre-flag hex chain and boot Hex over a binary genesis.
///
/// @throws bcos::tool::InvalidConfig on the combinations listed above.
ledger::account::AddressTableMode resolveNodeAddressTableMode(
    std::optional<std::string> const& layoutFlag, bool hexOnlyLane, bool chainHasState);

/// Boot-time account-table handling, carried from Initializer to LedgerInitializer::build:
/// the already-open state DB (RocksDB's single-instance lock forbids a second open) plus the
/// one-shot migration switch ([storage] migrate_account_tables_to_binary). The boot sequence
/// inside LedgerInitializer::build is: lane check → flag read (one point Get) → optional
/// one-shot migration (resumed when the flag says "migrating") → mode resolution → for a
/// brand-new binary chain, persist "bin" → mode publication, all before the genesis block
/// is built. The lane verdict needs the ledger's (decryption-aware) reads of the on-chain
/// config rows, so none of this can happen in Initializer ahead of the ledger construction.
struct AccountTableBoot
{
    std::reference_wrapper<::rocksdb::DB> stateDB;
    bool migrateToBinary = false;
};
}  // namespace bcos::initializer
