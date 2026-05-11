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
 */

#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-task/Wait.h"
#include "bcos-transaction-scheduler/BaselineScheduler.h"
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::storage;
using namespace bcos::storage2;
using namespace bcos::executor_v1;

BOOST_AUTO_TEST_SUITE(FIB99_FIB105_StateRootTest)

// ---------------------------------------------------------------------------
// FIB-105: Verify calculateStateRoot uses the passed blockVersion, not a
// hardcoded V3_1_VERSION.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(calculateStateRootUsesPassedVersion)
{
    // Build a small storage with one modified entry
    using Storage = memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
    Storage storage;

    Entry entry;
    entry.importFields({"hello"});
    task::syncWait(
        storage2::writeOne(storage, StateKey{"test_table", "test_key"}, std::move(entry)));

    crypto::Keccak256 hashImpl;

    // Compute state root with V3_1_VERSION
    auto rootV31 = task::syncWait(scheduler_v1::calculateStateRoot(
        storage, static_cast<uint32_t>(protocol::BlockVersion::V3_1_VERSION), hashImpl));

    // Compute state root with V3_17_0_VERSION (new unambiguous hashing)
    auto rootV317 = task::syncWait(scheduler_v1::calculateStateRoot(
        storage, static_cast<uint32_t>(protocol::BlockVersion::V3_17_0_VERSION), hashImpl));

    // The two roots must differ because the hashing algorithm changed.
    // If calculateStateRoot still hardcoded V3_1, they would be identical.
    BOOST_CHECK_NE(rootV31, rootV317);

    // Each should be non-zero (the entry is MODIFIED)
    BOOST_CHECK_NE(rootV31, h256{});
    BOOST_CHECK_NE(rootV317, h256{});
}

// ---------------------------------------------------------------------------
// FIB-99 boundary collision: (table="a", key="bc") vs (table="ab", key="c")
// should produce different hashes under V3_17_0 but the same under V3_1.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(boundaryAmbiguityFixedInV317)
{
    crypto::Keccak256 hashImpl;

    Entry entryA;
    entryA.importFields({"d"});  // MODIFIED with data "d"

    Entry entryB;
    entryB.importFields({"d"});  // MODIFIED with data "d"

    constexpr auto v31 = static_cast<uint32_t>(protocol::BlockVersion::V3_1_VERSION);
    constexpr auto v317 = static_cast<uint32_t>(protocol::BlockVersion::V3_17_0_VERSION);

    // Under V3_1: hash("a" || "bc" || "d") == hash("ab" || "c" || "d") => collision
    auto hashA_v31 = entryA.hash("a", "bc", hashImpl, v31);
    auto hashB_v31 = entryB.hash("ab", "c", hashImpl, v31);
    BOOST_CHECK_EQUAL(hashA_v31, hashB_v31);  // Known collision in old scheme

    // Under V3_17_0: length-prefixed => no collision
    auto hashA_v317 = entryA.hash("a", "bc", hashImpl, v317);
    auto hashB_v317 = entryB.hash("ab", "c", hashImpl, v317);
    BOOST_CHECK_NE(hashA_v317, hashB_v317);  // Fixed!
}

// ---------------------------------------------------------------------------
// FIB-99 status collision: DELETED entry vs MODIFIED with empty data should
// produce different hashes under V3_17_0 but the same under V3_1.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(statusAmbiguityFixedInV317)
{
    crypto::Keccak256 hashImpl;

    Entry deletedEntry;
    deletedEntry.setStatus(Entry::DELETED);

    Entry emptyModified;
    emptyModified.importFields({""});  // MODIFIED with empty data

    constexpr auto v31 = static_cast<uint32_t>(protocol::BlockVersion::V3_1_VERSION);
    constexpr auto v317 = static_cast<uint32_t>(protocol::BlockVersion::V3_17_0_VERSION);

    // Under V3_1: hash(table || key) vs hash(table || key || "") => same
    auto hashDel_v31 = deletedEntry.hash("tbl", "k", hashImpl, v31);
    auto hashMod_v31 = emptyModified.hash("tbl", "k", hashImpl, v31);
    BOOST_CHECK_EQUAL(hashDel_v31, hashMod_v31);  // Known collision in old scheme

    // Under V3_17_0: status byte distinguishes them
    auto hashDel_v317 = deletedEntry.hash("tbl", "k", hashImpl, v317);
    auto hashMod_v317 = emptyModified.hash("tbl", "k", hashImpl, v317);
    BOOST_CHECK_NE(hashDel_v317, hashMod_v317);  // Fixed!
}

// ---------------------------------------------------------------------------
// Backward compatibility: V3_1 and V3_0 hashes remain unchanged.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(backwardCompatibilityPreserved)
{
    crypto::Keccak256 hashImpl;

    Entry modifiedEntry;
    modifiedEntry.importFields({"value123"});

    Entry deletedEntry;
    deletedEntry.setStatus(Entry::DELETED);

    constexpr auto v30 = static_cast<uint32_t>(protocol::BlockVersion::V3_0_VERSION);
    constexpr auto v31 = static_cast<uint32_t>(protocol::BlockVersion::V3_1_VERSION);
    constexpr auto v316 = static_cast<uint32_t>(protocol::BlockVersion::V3_16_4_VERSION);

    // V3_0 hashing: MODIFIED => hash(data), DELETED => 0x1
    auto hashMod_v30 = modifiedEntry.hash("t", "k", hashImpl, v30);
    auto hashDel_v30 = deletedEntry.hash("t", "k", hashImpl, v30);
    BOOST_CHECK_NE(hashMod_v30, h256{});
    BOOST_CHECK_EQUAL(hashDel_v30, crypto::HashType(0x1));

    // V3_1 hashing: uses hasher with table+key+data, no length prefix
    auto hashMod_v31 = modifiedEntry.hash("t", "k", hashImpl, v31);
    auto hashDel_v31 = deletedEntry.hash("t", "k", hashImpl, v31);
    BOOST_CHECK_NE(hashMod_v31, h256{});
    BOOST_CHECK_NE(hashDel_v31, h256{});

    // V3_16_4 should still use V3_1 logic (< V3_17_0)
    auto hashMod_v316 = modifiedEntry.hash("t", "k", hashImpl, v316);
    auto hashDel_v316 = deletedEntry.hash("t", "k", hashImpl, v316);
    BOOST_CHECK_EQUAL(hashMod_v31, hashMod_v316);
    BOOST_CHECK_EQUAL(hashDel_v31, hashDel_v316);
}

BOOST_AUTO_TEST_SUITE_END()
