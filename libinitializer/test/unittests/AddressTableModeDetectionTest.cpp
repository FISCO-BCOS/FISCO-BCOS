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
 * @file AddressTableModeDetectionTest.cpp
 * @brief Boot-time node-local account-table mode resolution: the in-DB layout flag
 *        (read/write), the bounded hasAnyTableRegistration probe, and the lane forcing in
 *        resolveNodeAddressTableMode.
 */
#include "libinitializer/AddressTableModeDetection.h"
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-tool/Exceptions.h>
#include <rocksdb/db.h>
#include <boost/test/unit_test.hpp>
#include <filesystem>

using namespace bcos;
using namespace bcos::initializer;
using namespace bcos::ledger;

namespace
{
constexpr std::string_view kHexTable = "/apps/4200000000000000000000000000000000001234";

std::string binaryTableName()
{
    std::string name("/s/");
    name.append(std::string(20, '\x42'));
    return name;
}

struct TempRocksDB
{
    TempRocksDB() : dir(std::filesystem::temp_directory_path() / "addr_mode_detect_test")
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

    void putRegistration(std::string const& table)
    {
        auto status = db->Put(rocksdb::WriteOptions{}, "s_tables:" + table, "value");
        BOOST_REQUIRE(status.ok());
    }

    std::filesystem::path dir;
    std::unique_ptr<rocksdb::DB> db;
};
}  // namespace

BOOST_AUTO_TEST_SUITE(AddressTableModeDetectionSuite)

BOOST_AUTO_TEST_CASE(LayoutFlagRoundtrip)
{
    TempRocksDB fixture;
    // Absent: a pre-flag (hex) chain or a brand-new DB.
    BOOST_CHECK(!readAccountTableLayoutFlag(*fixture.db).has_value());

    writeAccountTableLayoutFlag(*fixture.db, ACCOUNT_TABLE_LAYOUT_MIGRATING);
    BOOST_CHECK(readAccountTableLayoutFlag(*fixture.db) ==
                std::optional<std::string>(std::string(ACCOUNT_TABLE_LAYOUT_MIGRATING)));

    writeAccountTableLayoutFlag(*fixture.db, ACCOUNT_TABLE_LAYOUT_BINARY);
    BOOST_CHECK(readAccountTableLayoutFlag(*fixture.db) ==
                std::optional<std::string>(std::string(ACCOUNT_TABLE_LAYOUT_BINARY)));
}

BOOST_AUTO_TEST_CASE(HasAnyTableRegistrationIsBoundedAndExact)
{
    // An empty DB (a brand-new chain) has no registrations.
    {
        TempRocksDB fixture;
        BOOST_CHECK(!hasAnyTableRegistration(*fixture.db));
    }
    // Genesis registers the system tables, so any committed chain answers true — even one
    // holding nothing but a /sys/ row.
    {
        TempRocksDB fixture;
        fixture.putRegistration("/sys/status");
        BOOST_CHECK(hasAnyTableRegistration(*fixture.db));
    }
    // The layout flag itself must NOT count as chain state: "s_node_local:..." sorts before
    // "s_tables:" and is skipped by the probe — otherwise a fresh chain born binary (flag
    // written before genesis) would misread itself as a pre-flag hex chain on the next boot.
    {
        TempRocksDB fixture;
        writeAccountTableLayoutFlag(*fixture.db, ACCOUNT_TABLE_LAYOUT_BINARY);
        BOOST_CHECK(!hasAnyTableRegistration(*fixture.db));
    }
}

BOOST_AUTO_TEST_CASE(ResolvesModeFromFlag)
{
    using account::AddressTableMode;
    const std::optional<std::string> kBin{std::string(ACCOUNT_TABLE_LAYOUT_BINARY)};
    const std::optional<std::string> kMigrating{std::string(ACCOUNT_TABLE_LAYOUT_MIGRATING)};
    const std::optional<std::string> kAbsent{std::nullopt};

    // No flag: a brand-new DB is Binary by default (the normalized hash keeps the genesis
    // root encoding-identical); an existing chain predates the mechanism and is Hex.
    BOOST_CHECK(resolveNodeAddressTableMode(kAbsent, false, /*chainHasState=*/false) ==
                AddressTableMode::Binary);
    BOOST_CHECK(resolveNodeAddressTableMode(kAbsent, false, /*chainHasState=*/true) ==
                AddressTableMode::Hex);
    BOOST_CHECK(resolveNodeAddressTableMode(kBin, false, true) == AddressTableMode::Binary);
    // "migrating" is the switch-OFF refusal: an unfinished migration must never publish a
    // mode — including over an all-hex registration set, the account-row-phase crash shape
    // that registration-scan detection used to publish as silent Hex.
    BOOST_CHECK_THROW(
        resolveNodeAddressTableMode(kMigrating, false, true), bcos::tool::InvalidConfig);
    // An unknown value is refused, not guessed (forward compatibility).
    BOOST_CHECK_THROW(
        resolveNodeAddressTableMode(std::optional<std::string>("future"), false, true),
        bcos::tool::InvalidConfig);
}

BOOST_AUTO_TEST_CASE(HexOnlyLaneForcing)
{
    using account::AddressTableMode;
    const std::optional<std::string> kBin{std::string(ACCOUNT_TABLE_LAYOUT_BINARY)};
    const std::optional<std::string> kMigrating{std::string(ACCOUNT_TABLE_LAYOUT_MIGRATING)};
    const std::optional<std::string> kAbsent{std::nullopt};

    // A hex-only lane is pinned to Hex, over hex data and over an empty DB alike.
    BOOST_CHECK(resolveNodeAddressTableMode(kAbsent, true, true) == AddressTableMode::Hex);
    BOOST_CHECK(resolveNodeAddressTableMode(kAbsent, true, false) == AddressTableMode::Hex);
    // Binary evidence on a hex-only lane is a loud boot failure with recovery instructions.
    BOOST_CHECK_THROW(resolveNodeAddressTableMode(kBin, true, true), bcos::tool::InvalidConfig);
    BOOST_CHECK_THROW(
        resolveNodeAddressTableMode(kMigrating, true, true), bcos::tool::InvalidConfig);
}

BOOST_AUTO_TEST_CASE(RefuseBinaryDataWithoutFlag)
{
    const std::optional<std::string> kAbsent{std::nullopt};
    const std::optional<std::string> kBin{std::string(ACCOUNT_TABLE_LAYOUT_BINARY)};

    // Binary registrations but no flag: the flag was lost (partial backup/restore) —
    // refuse to boot, never publish Hex over binary data.
    {
        TempRocksDB fixture;
        fixture.putRegistration(std::string(kHexTable));
        fixture.putRegistration(binaryTableName());
        BOOST_CHECK(hasBinaryTableRegistration(*fixture.db));
        BOOST_CHECK_THROW(
            refuseBinaryDataWithoutFlag(*fixture.db, kAbsent), bcos::tool::InvalidConfig);
    }
    // Flag present (bin): the legal binary layout — no refusal, no probe needed.
    {
        TempRocksDB fixture;
        fixture.putRegistration(binaryTableName());
        BOOST_CHECK_NO_THROW(refuseBinaryDataWithoutFlag(*fixture.db, kBin));
    }
    // Hex-only chain, no flag: the pre-3.18 steady state — no refusal. A "/s/" name of
    // the wrong length is not a binary account table and must not trip the probe.
    {
        TempRocksDB fixture;
        fixture.putRegistration(std::string(kHexTable));
        fixture.putRegistration("/s/" + std::string(19, 'b'));
        BOOST_CHECK(!hasBinaryTableRegistration(*fixture.db));
        BOOST_CHECK_NO_THROW(refuseBinaryDataWithoutFlag(*fixture.db, kAbsent));
    }
    // Brand-new DB: nothing at all.
    {
        TempRocksDB fixture;
        BOOST_CHECK(!hasBinaryTableRegistration(*fixture.db));
        BOOST_CHECK_NO_THROW(refuseBinaryDataWithoutFlag(*fixture.db, kAbsent));
    }
}

BOOST_AUTO_TEST_CASE(LaneClassification)
{
    Features noL2;
    Features l2;
    l2.set(Features::Flag::feature_l2_ethereum_compat);

    // The L2 flag makes every lane hex-only.
    for (int version : {0, 1, 2, 3, 4})
    {
        BOOST_CHECK(isHexOnlyExecutorLane(l2, version));
    }
    // Without it: legacy (0), Eth engine (2) and OP (>= 3) are hex-only; only the baseline
    // v1 lane routes account tables through the node mode.
    BOOST_CHECK(isHexOnlyExecutorLane(noL2, 0));
    BOOST_CHECK(!isHexOnlyExecutorLane(noL2, 1));
    BOOST_CHECK(isHexOnlyExecutorLane(noL2, ledger::ETHEREUM_EXECUTOR_VERSION));
    BOOST_CHECK(isHexOnlyExecutorLane(noL2, ledger::OPSTACK_EXECUTOR_VERSION));
    BOOST_CHECK(isHexOnlyExecutorLane(noL2, ledger::OPSTACK_EXECUTOR_VERSION + 1));
}

BOOST_AUTO_TEST_SUITE_END()
