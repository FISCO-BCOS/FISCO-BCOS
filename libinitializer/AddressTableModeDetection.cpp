#include "AddressTableModeDetection.h"

#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-tool/Exceptions.h>
#include <rocksdb/db.h>
#include <rocksdb/iterator.h>
#include <boost/throw_exception.hpp>

std::optional<std::string> bcos::initializer::readAccountTableLayoutFlag(::rocksdb::DB& stateDB)
{
    std::string value;
    auto status = stateDB.Get(::rocksdb::ReadOptions{},
        ::rocksdb::Slice(ACCOUNT_TABLE_LAYOUT_KEY.data(), ACCOUNT_TABLE_LAYOUT_KEY.size()),
        &value);
    if (status.IsNotFound())
    {
        return std::nullopt;
    }
    if (!status.ok())
    {
        BOOST_THROW_EXCEPTION(
            bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                "failed to read the account-table layout flag (" + status.ToString() +
                "); cannot determine the node-local account-table encoding"));
    }
    return value;
}

void bcos::initializer::writeAccountTableLayoutFlag(
    ::rocksdb::DB& stateDB, std::string_view value)
{
    ::rocksdb::WriteOptions writeOptions;
    writeOptions.sync = true;  // the flag is the migration's commit record: survive power loss
    auto status = stateDB.Put(writeOptions,
        ::rocksdb::Slice(ACCOUNT_TABLE_LAYOUT_KEY.data(), ACCOUNT_TABLE_LAYOUT_KEY.size()),
        ::rocksdb::Slice(value.data(), value.size()));
    if (!status.ok())
    {
        BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                                  "failed to write the account-table layout flag (" +
                                  status.ToString() + ")"));
    }
}

bool bcos::initializer::hasAnyTableRegistration(::rocksdb::DB& stateDB)
{
    // Genesis registers the system tables, so any committed chain has "s_tables:" rows and
    // a brand-new DB has none. One Seek: the flag key sorts before "s_tables:" and is
    // skipped. Read-your-writes is not needed — this runs before any state write.
    constexpr std::string_view prefix = "s_tables:";  // ledger::SYS_TABLES + ':'
    std::unique_ptr<::rocksdb::Iterator> it(stateDB.NewIterator(::rocksdb::ReadOptions{}));
    it->Seek(::rocksdb::Slice(prefix.data(), prefix.size()));
    return it->Valid() && it->key().starts_with(
                              ::rocksdb::Slice(prefix.data(), prefix.size()));
}

bool bcos::initializer::hasBinaryTableRegistration(::rocksdb::DB& stateDB)
{
    // Binary registrations are "s_tables:/s/<20 raw bytes>"; "/s/" sorts right after
    // "/apps/", so one Seek lands on the first candidate. Verify the shape: a "/s/" name
    // of the wrong length is not a binary account table.
    constexpr std::string_view prefix = "s_tables:/s/";
    std::unique_ptr<::rocksdb::Iterator> it(stateDB.NewIterator(::rocksdb::ReadOptions{}));
    it->Seek(::rocksdb::Slice(prefix.data(), prefix.size()));
    if (!it->Valid() ||
        !it->key().starts_with(::rocksdb::Slice(prefix.data(), prefix.size())))
    {
        return false;
    }
    auto const key = it->key();
    std::string_view const table(key.data() + ledger::SYS_TABLES.size() + 1,
        key.size() - ledger::SYS_TABLES.size() - 1);
    return ledger::account::isBinaryAccountTableName(table);
}

void bcos::initializer::refuseBinaryDataWithoutFlag(
    ::rocksdb::DB& stateDB, std::optional<std::string> const& layoutFlag)
{
    if (layoutFlag.has_value() || !hasBinaryTableRegistration(stateDB))
    {
        return;
    }
    // The flag is the only LEGAL witness of a binary layout, and it is absent — but
    // binary registrations exist: the flag was lost (a partial backup/restore that
    // dropped s_node_local:*). Refuse; re-running the migration is idempotent and
    // rewrites the flag.
    BOOST_THROW_EXCEPTION(
        bcos::tool::InvalidConfig() << bcos::errinfo_comment(
            "the state DB holds binary-layout account tables (s_tables:/s/ registrations) "
            "but the account-table layout flag (" +
            std::string(ACCOUNT_TABLE_LAYOUT_KEY) +
            ") is absent — the flag was lost, e.g. by a backup/restore that dropped the "
            "s_node_local keys. Refusing to boot Hex over binary data: set [storage] "
            "migrate_account_tables_to_binary=true and restart (the migration is "
            "idempotent and rewrites the flag), or restore a consistent snapshot"));
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
    std::optional<std::string> const& layoutFlag, bool hexOnlyLane, bool chainHasState)
{
    using ledger::account::AddressTableMode;
    if (hexOnlyLane)
    {
        if (layoutFlag.has_value())
        {
            BOOST_THROW_EXCEPTION(
                bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                    "this node's state DB holds binary-layout account tables or an "
                    "unfinished hex->binary migration (the " +
                    std::string(ACCOUNT_TABLE_LAYOUT_KEY) + " flag is '" + *layoutFlag +
                    "'), but the chain runs a hex-only executor lane (OP / Eth engine / "
                    "legacy executor): those executors name account tables /apps/<40-hex> "
                    "directly and would split reads and writes onto disjoint tables. "
                    "Recovery: if this chain was migrated from hex, roll the state DB back "
                    "to the pre-migration snapshot. A chain born binary has no hex snapshot "
                    "to return to, and the governance block that switched lanes is already "
                    "committed — the chain cannot simply vote it back: recovery is "
                    "operational. Roll EVERY node's state DB back to a snapshot taken "
                    "before that block and re-form consensus without the offending config "
                    "transaction"));
        }
        return AddressTableMode::Hex;
    }
    if (layoutFlag.has_value())
    {
        if (*layoutFlag == ACCOUNT_TABLE_LAYOUT_BINARY)
        {
            return AddressTableMode::Binary;
        }
        if (*layoutFlag == ACCOUNT_TABLE_LAYOUT_MIGRATING)
        {
            BOOST_THROW_EXCEPTION(
                bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                    "the state DB holds an unfinished hex->binary account-table migration "
                    "(the " +
                    std::string(ACCOUNT_TABLE_LAYOUT_KEY) +
                    " flag is 'migrating'): set [storage] "
                    "migrate_account_tables_to_binary=true and restart to resume and finish "
                    "the migration, or roll the state DB back to a pre-migration snapshot"));
        }
        // Forward compatibility: refuse rather than guess at a state a newer binary wrote.
        BOOST_THROW_EXCEPTION(
            bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                "unknown account-table layout flag '" + *layoutFlag + "' at " +
                std::string(ACCOUNT_TABLE_LAYOUT_KEY) +
                ": written by a newer binary? Refusing to guess the node-local "
                "account-table encoding"));
    }
    // No flag: a chain that predates the mechanism (hex account tables only — nothing
    // binary ever shipped) or a brand-new DB. The registration probe distinguishes them;
    // the caller persists "bin" for a fresh chain BEFORE genesis so binary tables never
    // exist without the flag.
    return chainHasState ? AddressTableMode::Hex : AddressTableMode::Binary;
}
