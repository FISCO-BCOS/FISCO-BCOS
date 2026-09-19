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
 * @brief Boot-time node-local account-table mode detection: the s_tables prefix scan,
 *        the marker file, and the lane forcing in resolveNodeAddressTableMode.
 */
#include "libinitializer/AddressTableModeDetection.h"
#include <bcos-framework/ledger/LedgerConfig.h>
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

std::string binaryTableName()
{
    std::string name("/apps/");
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

BOOST_AUTO_TEST_CASE(DetectsHexBinaryAndMixedLayouts)
{
    // Hex-only: an s_tables:/apps/<40hex> registration row.
    {
        TempRocksDB fixture;
        fixture.putRegistration(std::string(kHexTable));
        // Not account tables: /sys/ and a 40-hex auth table must not count.
        fixture.putRegistration("/sys/status");
        fixture.putRegistration(std::string(kHexTable) + "_accessAuth");
        auto layout = detectAccountTableLayout(*fixture.db, fixture.dir.string());
        BOOST_CHECK(layout.sawHexTables);
        BOOST_CHECK(!layout.sawBinaryTables);
        BOOST_CHECK(!layout.markerFile);
    }
    // Binary-only.
    {
        TempRocksDB fixture;
        fixture.putRegistration(binaryTableName());
        auto layout = detectAccountTableLayout(*fixture.db, fixture.dir.string());
        BOOST_CHECK(!layout.sawHexTables);
        BOOST_CHECK(layout.sawBinaryTables);
    }
    // Mixed (interrupted migration).
    {
        TempRocksDB fixture;
        fixture.putRegistration(std::string(kHexTable));
        fixture.putRegistration(binaryTableName());
        auto layout = detectAccountTableLayout(*fixture.db, fixture.dir.string());
        BOOST_CHECK(layout.sawHexTables);
        BOOST_CHECK(layout.sawBinaryTables);
    }
    // Marker file alone.
    {
        TempRocksDB fixture;
        std::ofstream marker(binaryAccountTablesMarkerPath(fixture.dir.string()));
        marker << "binary\n";
        marker.close();
        auto layout = detectAccountTableLayout(*fixture.db, fixture.dir.string());
        BOOST_CHECK(layout.markerFile);
        BOOST_CHECK(!layout.sawHexTables);
        BOOST_CHECK(!layout.sawBinaryTables);
    }
}

BOOST_AUTO_TEST_CASE(ResolvesModeFromLayout)
{
    using account::AddressTableMode;
    // No data (fresh chain): Binary by default; the normalized hash keeps the genesis root
    // encoding-identical.
    BOOST_CHECK(resolveNodeAddressTableMode({}, false) == AddressTableMode::Binary);
    BOOST_CHECK(resolveNodeAddressTableMode({.sawHexTables = true}, false) ==
                AddressTableMode::Hex);
    BOOST_CHECK(resolveNodeAddressTableMode({.sawBinaryTables = true}, false) ==
                AddressTableMode::Binary);
    BOOST_CHECK(resolveNodeAddressTableMode({.markerFile = true}, false) ==
                AddressTableMode::Binary);
    BOOST_CHECK(
        resolveNodeAddressTableMode({.sawHexTables = true, .sawBinaryTables = true}, false) ==
        AddressTableMode::BinaryWithHexFallback);
}

BOOST_AUTO_TEST_CASE(HexOnlyLaneForcing)
{
    using account::AddressTableMode;
    // A hex-only lane is pinned to Hex, even over hex data or no data.
    BOOST_CHECK(resolveNodeAddressTableMode({}, true) == AddressTableMode::Hex);
    BOOST_CHECK(resolveNodeAddressTableMode({.sawHexTables = true}, true) ==
                AddressTableMode::Hex);
    // Binary evidence on a hex-only lane is a loud boot failure with recovery instructions.
    BOOST_CHECK_THROW(resolveNodeAddressTableMode({.sawBinaryTables = true}, true),
        bcos::tool::InvalidConfig);
    BOOST_CHECK_THROW(
        resolveNodeAddressTableMode({.markerFile = true}, true), bcos::tool::InvalidConfig);
    BOOST_CHECK_THROW(
        resolveNodeAddressTableMode({.sawHexTables = true, .sawBinaryTables = true}, true),
        bcos::tool::InvalidConfig);
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
