/*
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief State rows whose ROW KEY is arbitrary binary — the shape every hash-, digest- or
 *        position-keyed row in the default ColumnFamily has. Pins the two facts such rows
 *        depend on: StateKeyResolver splits a physical key at its FIRST colon (so a table name
 *        must carry one and the row key may contain any byte, 0x3A included), and
 *        RocksDBStorage2 exposes the DB it writes through. The MPT node row is the in-tree
 *        example; its own layout is proven in bcos-ledger (MPTNodeRowLayoutTest.cpp), which is
 *        where the table names are defined.
 * @file TestStateKeyBinaryRows.cpp
 * @author: kyonRay
 * @date: 2026-05-12
 */
#include <bcos-storage/RocksDBStorage2.h>
#include <bcos-storage/StateKVResolver.h>
#include <bcos-utilities/FixedBytes.h>
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <random>
#include <string_view>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::storage2::rocksdb;

namespace
{
/// The physical bytes StateKeyResolver::encode emits for a StateKey — the single authority
/// every on-disk key goes through (RocksDBStorage2's write path).
std::string resolverPhysicalKey(executor_v1::StateKey const& stateKey)
{
    std::string out;
    StateKeyResolver::encode(stateKey, [&](bcos::bytesConstRef view) {
        out.append(reinterpret_cast<char const*>(view.data()), view.size());
    });
    return out;
}
}  // namespace

/// A table whose name carries no ':' and a row key that deliberately does: exactly the shape a
/// digest- or position-keyed state row has. Spelled locally — this file must not depend on any
/// module that defines real table names.
constexpr std::string_view kBinaryRowTable = "/binrow/x";

BOOST_AUTO_TEST_SUITE(StateKeyBinaryRowsSuite)

BOOST_AUTO_TEST_CASE(ColonFreeLayoutIsNotAStateKey)
{
    // A colon-free layout (table name + row key, no ':') cannot even be decoded as a StateKey
    // when the row key contains no 0x3A byte — the reason every table name is followed by ':'.
    // (A row key WITH a 0x3A would decode, but to a corrupted table/key split.)
    std::string legacy(37, '\0');
    legacy.replace(0, kBinaryRowTable.size(), kBinaryRowTable);
    BOOST_CHECK_THROW(
        StateKeyResolver::decode(std::string_view(legacy)), executor_v1::NoTableSpliterError);
}

BOOST_AUTO_TEST_CASE(RocksDBAccessorExposed)
{
    std::string path = "./mptkey_test_" + std::to_string(std::random_device{}());
    ::rocksdb::DB* rawPtr = nullptr;

    {
        ::rocksdb::Options options;
        options.create_if_missing = true;
        ::rocksdb::Status s = ::rocksdb::DB::Open(options, path, &rawPtr);
        BOOST_REQUIRE(s.ok());
        BOOST_REQUIRE(rawPtr != nullptr);
    }

    // Inner scope: keep dbOwner + storage lifetime strictly shorter than the on-disk path.
    // Background RocksDB threads (compaction, flush) may still touch lock/SST files while
    // the DB is open, so the directory must outlive both objects' destructors — otherwise
    // remove_all races the close path and can spuriously fail on lock-tracking filesystems.
    {
        std::unique_ptr<::rocksdb::DB> dbOwner(rawPtr);

        RocksDBStorage2<executor_v1::StateKey, storage::Entry, StateKeyResolver, StateValueResolver>
            storage(*dbOwner, StateKeyResolver{}, StateValueResolver{});

        // rocksDB() must reference the same underlying DB instance
        BOOST_CHECK_EQUAL(&storage.rocksDB(), rawPtr);

        // Demonstrate writing and reading a node row's physical key via rocksDB()
        std::string mptKey = resolverPhysicalKey(
            pathNodeStateKey(PathKey{.scope = TrieScope::storage(h256::generateRandomFixedBytes()),
                .position = bytes{0x01, 0x02}}));
        std::string mptValue = "mpt_node_data";

        ::rocksdb::WriteOptions wo;
        auto putStatus =
            storage.rocksDB().Put(wo, storage.rocksDB().DefaultColumnFamily(), mptKey, mptValue);
        BOOST_REQUIRE(putStatus.ok());

        std::string readback;
        ::rocksdb::ReadOptions ro;
        auto getStatus =
            storage.rocksDB().Get(ro, storage.rocksDB().DefaultColumnFamily(), mptKey, &readback);
        BOOST_REQUIRE(getStatus.ok());
        BOOST_CHECK_EQUAL(readback, mptValue);
    }  // storage destructor, then dbOwner destructor (LIFO) — DB fully closed here

    boost::filesystem::remove_all(path);
}

BOOST_AUTO_TEST_SUITE_END()
