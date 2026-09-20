#include "AddressTableModeDetection.h"

#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-tool/Exceptions.h>
#include <bcos-utilities/BoostLog.h>
#include <rocksdb/db.h>
#include <rocksdb/iterator.h>
#include <boost/throw_exception.hpp>
#include <filesystem>

std::string bcos::initializer::binaryAccountTablesMarkerPath(std::string_view storageRootPath)
{
    return (std::filesystem::path(storageRootPath) / BINARY_ACCOUNT_TABLES_MARKER).string();
}

bcos::initializer::AccountTableLayout bcos::initializer::detectAccountTableLayout(
    ::rocksdb::DB& stateDB, std::string_view storageRootPath)
{
    AccountTableLayout layout;
    layout.markerFile =
        std::filesystem::exists(binaryAccountTablesMarkerPath(storageRootPath));

    // Physical keys are the flat "table:key" form (executor_v1::StateKey encoding, shared
    // by the storage2 and the legacy storage layers — both use TABLE_KEY_SPLIT ':'), so the
    // account-table registration rows sort together under "s_tables:/": hex account tables
    // register as "s_tables:/apps/<40 hex>", binary ones as "s_tables:/s/<20 raw bytes>"
    // ("/apps/" < "/s/" < "/sys/", so both families land inside this one seek range).
    // The scan also sees every OTHER "/"-rooted registration (/sys/, /tables/, auth
    // tables, short-name BFS tables) — the is* probes below ignore all of them by
    // prefix+shape, including a "/apps/" name of exactly 20 chars (a plain BFS table,
    // never a binary account table: the binary layout lives under "/s/", prefix-free
    // from everything BFS can produce — AccountTableName.h).
    constexpr std::string_view prefix = "s_tables:/";  // SYS_TABLES + ':' + the "/" root
    std::unique_ptr<::rocksdb::Iterator> it(stateDB.NewIterator(::rocksdb::ReadOptions{}));
    for (it->Seek(::rocksdb::Slice(prefix)); it->Valid(); it->Next())
    {
        auto key = it->key();
        if (key.size() < prefix.size() ||
            std::string_view(key.data(), prefix.size()) != prefix)
        {
            break;
        }
        // Strip "s_tables:": the rest is the registered table name itself.
        std::string_view table(key.data() + ledger::SYS_TABLES.size() + 1,
            key.size() - ledger::SYS_TABLES.size() - 1);
        layout.sawHexTables |= ledger::account::isHexAccountTableName(table);
        layout.sawBinaryTables |= ledger::account::isBinaryAccountTableName(table);
        if (layout.sawHexTables && layout.sawBinaryTables)
        {
            break;  // the mixed verdict is already decided
        }
    }
    if (!it->status().ok())
    {
        BOOST_THROW_EXCEPTION(
            bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                "failed to scan the state DB for account-table registrations (" +
                it->status().ToString() +
                "); cannot determine the node-local account-table encoding"));
    }
    return layout;
}

bool bcos::initializer::isHexOnlyExecutorLane(
    const ledger::Features& features, int executorVersion)
{
    return features.get(ledger::Features::Flag::feature_l2_ethereum_compat) ||
           executorVersion == 0 ||  // legacy bcos-executor lane (SchedulerManager)
           executorVersion == ledger::ETHEREUM_EXECUTOR_VERSION ||
           executorVersion >= ledger::OPSTACK_EXECUTOR_VERSION;
}

bcos::ledger::account::AddressTableMode bcos::initializer::resolveNodeAddressTableMode(
    AccountTableLayout const& layout, bool hexOnlyLane)
{
    using ledger::account::AddressTableMode;
    if (hexOnlyLane)
    {
        if (layout.markerFile || layout.sawBinaryTables)
        {
            BOOST_THROW_EXCEPTION(
                bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                    "this node's state DB holds binary-layout account tables ("
                    ".binary_account_tables marker or s_tables:/s/<20-byte> rows), but "
                    "the chain runs a hex-only executor lane (OP / Eth engine / legacy "
                    "executor): those executors name account tables /apps/<40-hex> "
                    "directly and would split reads and writes onto disjoint tables. "
                    "Recovery: roll the state DB back to the pre-migration snapshot (or "
                    "delete the .binary_account_tables marker if migration never ran), or "
                    "switch the chain to the baseline executor (executor_version = 1) "
                    "before migrating"));
        }
        return AddressTableMode::Hex;
    }
    if (layout.markerFile)
    {
        return AddressTableMode::Binary;
    }
    if (layout.sawHexTables && layout.sawBinaryTables)
    {
        // Defensive invariant: the boot sequence (LedgerInitializer::build) resolves a mixed
        // layout before calling here — resume the migration when
        // [storage] migrate_account_tables_to_binary is set, refuse to start otherwise.
        BOOST_THROW_EXCEPTION(
            bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                "the state DB holds an unfinished hex->binary account-table migration (both "
                "s_tables:/apps/<40-hex> and s_tables:/s/<20-byte> registrations exist): "
                "set [storage] migrate_account_tables_to_binary=true and restart to finish "
                "the migration, or roll the state DB back to a pre-migration snapshot"));
    }
    if (layout.sawBinaryTables)
    {
        return AddressTableMode::Binary;
    }
    if (layout.sawHexTables)
    {
        return AddressTableMode::Hex;
    }
    // Brand-new chain: default to the new encoding. The encoding is node-local and the
    // normalized Entry::hash folds binary table names back to hex, so the genesis state
    // root is byte-identical to the hex layout — no fork risk from this default.
    return AddressTableMode::Binary;
}
