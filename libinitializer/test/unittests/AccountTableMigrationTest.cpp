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
 * @file AccountTableMigrationTest.cpp
 * @brief The one-shot hex→binary account-table migration: rename coverage, marker file,
 *        idempotency/crash-resume, conflict abort, and the hex-only lane refusal.
 */
#include "libinitializer/AccountTableMigration.h"
#include "libinitializer/AddressTableModeDetection.h"
#include <bcos-tool/Exceptions.h>
#include <boost/test/unit_test.hpp>
#include <rocksdb/db.h>
#include <filesystem>
#include <fstream>

using namespace bcos;
using namespace bcos::initializer;
using namespace bcos::ledger;

namespace
{
constexpr std::string_view kHexTable = "/apps/4200000000000000000000000000000000001234";
constexpr std::string_view kHexTable2 = "/apps/abcdefabcdefabcdefabcdefabcdefabcdef1234";

std::string binaryTable(std::string_view hexTable)
{
    return account::hexToBinaryAccountTableName(hexTable);
}

struct TempRocksDB
{
    TempRocksDB() : dir(std::filesystem::temp_directory_path() / "account_table_migration_test")
    {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        rocksdb::Options options;
        options.create_if_missing = true;
        rocksdb::DB* raw = nullptr;
        auto status = rocksdb::DB::Open(options, dir.string(), &raw);
        BOOST_REQUIRE(status.ok());
        db.reset(raw);
    }
    ~TempRocksDB()
    {
        db.reset();
        std::filesystem::remove_all(dir);
    }

    void put(std::string const& key, std::string const& value)
    {
        auto status = db->Put(rocksdb::WriteOptions{}, key, value);
        BOOST_REQUIRE(status.ok());
    }

    std::optional<std::string> get(std::string const& key)
    {
        std::string value;
        auto status = db->Get(rocksdb::ReadOptions{}, key, &value);
        if (status.IsNotFound())
        {
            return std::nullopt;
        }
        BOOST_REQUIRE(status.ok());
        return value;
    }

    bool markerExists() const
    {
        return std::filesystem::exists(binaryAccountTablesMarkerPath(dir.string()));
    }

    std::filesystem::path dir;
    std::unique_ptr<rocksdb::DB> db;
};

// The full pre-migration mix: two account tables (rows + registrations), plus every key
// family that must NOT move.
void seedChain(TempRocksDB& f)
{
    f.put(std::string(kHexTable) + ":balance", "1000");
    f.put(std::string(kHexTable) + ":nonce", "7");
    f.put(std::string(kHexTable2) + ":code_hash", "deadbeef");
    f.put("s_tables:" + std::string(kHexTable), "value");
    f.put("s_tables:" + std::string(kHexTable2), "value");
    // Untouched families:
    f.put("s_tables:/sys/status", "value");  // /sys/ registration
    f.put("s_current_state:current_number", "42");
    f.put("/mpt/" + std::string(64, 'a'), "node");
    f.put(std::string(kHexTable) + "_accessAuth:value", "admin");  // 51-char auth table
    f.put("s_tables:" + std::string(kHexTable) + "_accessAuth", "value");
    f.put("/apps/MyContract:balance", "5");  // short-name contract table
    f.put("s_tables:/apps/MyContract", "value");
}

void assertUntouched(TempRocksDB& f)
{
    BOOST_CHECK(f.get("s_tables:/sys/status") == std::optional<std::string>("value"));
    BOOST_CHECK(f.get("s_current_state:current_number") == std::optional<std::string>("42"));
    BOOST_CHECK(f.get("/mpt/" + std::string(64, 'a')) == std::optional<std::string>("node"));
    BOOST_CHECK(f.get(std::string(kHexTable) + "_accessAuth:value") ==
                std::optional<std::string>("admin"));
    BOOST_CHECK(f.get("s_tables:" + std::string(kHexTable) + "_accessAuth") ==
                std::optional<std::string>("value"));
    BOOST_CHECK(f.get("/apps/MyContract:balance") == std::optional<std::string>("5"));
    BOOST_CHECK(f.get("s_tables:/apps/MyContract") == std::optional<std::string>("value"));
}
}  // namespace

BOOST_AUTO_TEST_SUITE(AccountTableMigrationSuite)

BOOST_AUTO_TEST_CASE(MigratesAccountRowsAndRegistrationsOnly)
{
    TempRocksDB f;
    seedChain(f);

    auto stats = migrateAccountTablesToBinary(*f.db, f.dir.string(), /*hexOnlyLane=*/false);
    BOOST_CHECK(!stats.alreadyMigrated);
    BOOST_CHECK_EQUAL(stats.migratedAccountRows, 3);
    BOOST_CHECK_EQUAL(stats.migratedRegistrations, 2);
    BOOST_CHECK_EQUAL(stats.dedupedRows, 0);

    // Renamed, values preserved.
    BOOST_CHECK(f.get(binaryTable(kHexTable) + ":balance") ==
                std::optional<std::string>("1000"));
    BOOST_CHECK(f.get(binaryTable(kHexTable) + ":nonce") == std::optional<std::string>("7"));
    BOOST_CHECK(f.get(binaryTable(kHexTable2) + ":code_hash") ==
                std::optional<std::string>("deadbeef"));
    BOOST_CHECK(f.get("s_tables:" + binaryTable(kHexTable)) ==
                std::optional<std::string>("value"));
    BOOST_CHECK(f.get("s_tables:" + binaryTable(kHexTable2)) ==
                std::optional<std::string>("value"));
    // Hex sources gone.
    BOOST_CHECK(!f.get(std::string(kHexTable) + ":balance"));
    BOOST_CHECK(!f.get(std::string(kHexTable) + ":nonce"));
    BOOST_CHECK(!f.get(std::string(kHexTable2) + ":code_hash"));
    BOOST_CHECK(!f.get("s_tables:" + std::string(kHexTable)));
    BOOST_CHECK(!f.get("s_tables:" + std::string(kHexTable2)));
    // Everything else untouched.
    assertUntouched(f);

    // Marker written; the boot-time detection sees a pure binary layout.
    BOOST_CHECK(f.markerExists());
    auto layout = detectAccountTableLayout(*f.db, f.dir.string());
    BOOST_CHECK(layout.markerFile);
    BOOST_CHECK(!layout.sawHexTables);
    BOOST_CHECK(layout.sawBinaryTables);
    BOOST_CHECK(resolveNodeAddressTableMode(layout, /*hexOnlyLane=*/false) ==
                account::AddressTableMode::Binary);
}

BOOST_AUTO_TEST_CASE(SecondRunSkipsScanAndMarkerlessResumeIsIdempotent)
{
    TempRocksDB f;
    seedChain(f);
    migrateAccountTablesToBinary(*f.db, f.dir.string(), /*hexOnlyLane=*/false);

    // Marker present: no scan at all.
    auto stats = migrateAccountTablesToBinary(*f.db, f.dir.string(), /*hexOnlyLane=*/false);
    BOOST_CHECK(stats.alreadyMigrated);
    BOOST_CHECK_EQUAL(stats.scanned, 0);
    BOOST_CHECK_EQUAL(stats.migratedAccountRows, 0);
    BOOST_CHECK_EQUAL(stats.migratedRegistrations, 0);
    BOOST_CHECK_EQUAL(stats.dedupedRows, 0);

    // Marker lost (crash between the last batch and the marker write, or operator rollback):
    // the re-run scans, finds nothing left to rename, and rewrites the marker.
    std::filesystem::remove(binaryAccountTablesMarkerPath(f.dir.string()));
    stats = migrateAccountTablesToBinary(*f.db, f.dir.string(), /*hexOnlyLane=*/false);
    BOOST_CHECK(!stats.alreadyMigrated);
    BOOST_CHECK(stats.scanned > 0);
    BOOST_CHECK_EQUAL(stats.migratedAccountRows, 0);
    BOOST_CHECK_EQUAL(stats.migratedRegistrations, 0);
    BOOST_CHECK_EQUAL(stats.dedupedRows, 0);
    BOOST_CHECK(f.markerExists());
    assertUntouched(f);
}

BOOST_AUTO_TEST_CASE(DedupsInterruptedRun)
{
    TempRocksDB f;
    seedChain(f);
    // An interrupted previous run left this row renamed but its hex source undeleted.
    f.put(binaryTable(kHexTable) + ":balance", "1000");

    auto stats = migrateAccountTablesToBinary(*f.db, f.dir.string(), /*hexOnlyLane=*/false);
    BOOST_CHECK_EQUAL(stats.migratedAccountRows, 2);  // nonce + code_hash
    BOOST_CHECK_EQUAL(stats.dedupedRows, 1);          // balance
    BOOST_CHECK(!f.get(std::string(kHexTable) + ":balance"));
    BOOST_CHECK(f.get(binaryTable(kHexTable) + ":balance") ==
                std::optional<std::string>("1000"));
    assertUntouched(f);
}

BOOST_AUTO_TEST_CASE(AbortsOnValueConflict)
{
    TempRocksDB f;
    seedChain(f);
    // The binary twin exists with a DIFFERENT value: genuine data conflict, abort loudly.
    f.put(binaryTable(kHexTable) + ":balance", "9999");

    BOOST_CHECK_THROW(migrateAccountTablesToBinary(*f.db, f.dir.string(), /*hexOnlyLane=*/false),
        bcos::tool::InvalidConfig);
    // No marker: the migration did not complete.
    BOOST_CHECK(!f.markerExists());
}

BOOST_AUTO_TEST_CASE(RefusesHexOnlyLane)
{
    TempRocksDB f;
    seedChain(f);
    BOOST_CHECK_THROW(migrateAccountTablesToBinary(*f.db, f.dir.string(), /*hexOnlyLane=*/true),
        bcos::tool::InvalidConfig);
    // Nothing moved.
    BOOST_CHECK(f.get(std::string(kHexTable) + ":balance") ==
                std::optional<std::string>("1000"));
    BOOST_CHECK(!f.markerExists());
}

BOOST_AUTO_TEST_SUITE_END()
