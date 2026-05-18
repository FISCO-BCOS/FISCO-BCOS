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
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-task/Wait.h"
#include "bcos-transaction-scheduler/BaselineScheduler.h"
#include <boost/test/unit_test.hpp>
#include <optional>

using namespace bcos;
using namespace bcos::storage;
using namespace bcos::storage2;
using namespace bcos::executor_v1;

BOOST_AUTO_TEST_SUITE(FIB99_FIB105_StateRootTest)

namespace
{
std::optional<ledger::Features> featuresWithFix()
{
    ledger::Features features;
    features.set(ledger::Features::Flag::bugfix_statestorage_hash_v3_17);
    return features;
}
}  // namespace

// ---------------------------------------------------------------------------
// FIB-105 / FIB-99: calculateStateRoot must select the V3_17 hashing path
// based on the bugfix flag set in Features, not the block version.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(calculateStateRootRespectsBugfixFlag)
{
    using Storage = memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
    Storage storage;

    Entry entry;
    entry.importFields({"hello"});
    task::syncWait(
        storage2::writeOne(storage, StateKey{"test_table", "test_key"}, std::move(entry)));

    crypto::Keccak256 hashImpl;
    constexpr auto v31 = static_cast<uint32_t>(protocol::BlockVersion::V3_1_VERSION);

    ledger::Features noFix;
    ledger::Features fix;
    fix.set(ledger::Features::Flag::bugfix_statestorage_hash_v3_17);

    auto rootLegacy =
        task::syncWait(scheduler_v1::calculateStateRoot(storage, v31, hashImpl, noFix));
    auto rootFixed = task::syncWait(scheduler_v1::calculateStateRoot(storage, v31, hashImpl, fix));

    // The flag must change the hashing scheme even when blockVersion is identical.
    BOOST_CHECK_NE(rootLegacy, rootFixed);
    BOOST_CHECK_NE(rootLegacy, h256{});
    BOOST_CHECK_NE(rootFixed, h256{});
}

// ---------------------------------------------------------------------------
// FIB-99 boundary collision: (table="a", key="bc") vs (table="ab", key="c")
// collide under the legacy path, are distinct once the bugfix flag is set.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(boundaryAmbiguityFixedByFlag)
{
    crypto::Keccak256 hashImpl;
    constexpr auto v31 = static_cast<uint32_t>(protocol::BlockVersion::V3_1_VERSION);

    Entry entryA;
    entryA.importFields({"d"});
    Entry entryB;
    entryB.importFields({"d"});

    const std::optional<ledger::Features> noFix{ledger::Features{}};
    const auto fix = featuresWithFix();

    // Legacy: hash("a" || "bc" || "d") == hash("ab" || "c" || "d") => collision.
    BOOST_CHECK_EQUAL(
        entryA.hash("a", "bc", hashImpl, v31, noFix), entryB.hash("ab", "c", hashImpl, v31, noFix));

    // With bugfix flag: length-prefixed => no collision.
    BOOST_CHECK_NE(
        entryA.hash("a", "bc", hashImpl, v31, fix), entryB.hash("ab", "c", hashImpl, v31, fix));
}

// ---------------------------------------------------------------------------
// FIB-99 status collision: DELETED vs MODIFIED-with-empty-data must be
// distinguishable once the bugfix flag is set.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(statusAmbiguityFixedByFlag)
{
    crypto::Keccak256 hashImpl;
    constexpr auto v31 = static_cast<uint32_t>(protocol::BlockVersion::V3_1_VERSION);

    Entry deletedEntry;
    deletedEntry.setStatus(Entry::DELETED);
    Entry emptyModified;
    emptyModified.importFields({""});

    const std::optional<ledger::Features> noFix{ledger::Features{}};
    const auto fix = featuresWithFix();

    BOOST_CHECK_EQUAL(deletedEntry.hash("tbl", "k", hashImpl, v31, noFix),
        emptyModified.hash("tbl", "k", hashImpl, v31, noFix));

    BOOST_CHECK_NE(deletedEntry.hash("tbl", "k", hashImpl, v31, fix),
        emptyModified.hash("tbl", "k", hashImpl, v31, fix));
}

// ---------------------------------------------------------------------------
// Backward compatibility: the 4-arg overload (defaults to no Features) and the
// 5-arg overload with std::nullopt or an empty-flags Features object must all
// produce identical hashes. The new V3_17 fixed format is gated only by the
// flag, never by blockVersion.
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
    constexpr auto v317 = static_cast<uint32_t>(protocol::BlockVersion::V3_17_0_VERSION);

    const std::optional<ledger::Features> nullopt;  // empty optional
    const std::optional<ledger::Features> noFix{ledger::Features{}};

    // 4-arg overload (defaults to nullopt) and 5-arg with nullopt agree.
    BOOST_CHECK_EQUAL(modifiedEntry.hash("t", "k", hashImpl, v31),
        modifiedEntry.hash("t", "k", hashImpl, v31, nullopt));

    // An empty Features (flag unset) is also equivalent to nullopt for hashing.
    BOOST_CHECK_EQUAL(modifiedEntry.hash("t", "k", hashImpl, v31, nullopt),
        modifiedEntry.hash("t", "k", hashImpl, v31, noFix));

    // V3_0 path: MODIFIED hashes raw data, DELETED returns 0x1.
    BOOST_CHECK_NE(modifiedEntry.hash("t", "k", hashImpl, v30, noFix), h256{});
    BOOST_CHECK_EQUAL(deletedEntry.hash("t", "k", hashImpl, v30, noFix), crypto::HashType(0x1));

    // V3_1+ path is identical regardless of how high blockVersion goes when flag is off.
    auto modOff_v31 = modifiedEntry.hash("t", "k", hashImpl, v31, noFix);
    auto modOff_v316 = modifiedEntry.hash("t", "k", hashImpl, v316, noFix);
    auto modOff_v317 = modifiedEntry.hash("t", "k", hashImpl, v317, noFix);
    BOOST_CHECK_EQUAL(modOff_v31, modOff_v316);
    BOOST_CHECK_EQUAL(modOff_v31, modOff_v317);

    // Flag ON: produces a different (length-prefixed, status-aware) hash.
    const auto fix = featuresWithFix();
    auto modOn_v31 = modifiedEntry.hash("t", "k", hashImpl, v31, fix);
    BOOST_CHECK_NE(modOff_v31, modOn_v31);

    // Flag ON: result is independent of blockVersion (only the flag matters).
    auto modOn_v317 = modifiedEntry.hash("t", "k", hashImpl, v317, fix);
    BOOST_CHECK_EQUAL(modOn_v31, modOn_v317);
}

// ---------------------------------------------------------------------------
// Features integration: the bugfix flag is wired into the upgrade roadmap so
// nodes upgrading to V3_17_0 automatically activate it.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(bugfixFlagActivatedAtV3_17_0)
{
    ledger::Features features;
    BOOST_CHECK(!features.get(ledger::Features::Flag::bugfix_statestorage_hash_v3_17));

    features.setUpgradeFeatures(
        protocol::BlockVersion::V3_16_5_VERSION, protocol::BlockVersion::V3_17_0_VERSION);
    BOOST_CHECK(features.get(ledger::Features::Flag::bugfix_statestorage_hash_v3_17));
}

// ---------------------------------------------------------------------------
// FIB-99 cross-platform stability: pin the V3_17 preimage byte order so that
// heterogeneous (little-endian and big-endian) nodes compute byte-identical
// state roots.  The relational tests above (boundary / status collision) only
// prove that length prefixes exist; they cannot detect a regression where the
// prefixes are written in native order.  This test pins the canonical digest
// for a known preimage, computed independently using keccak256 over:
//   u32be(tableLen) || table || u32be(keyLen) || key || i8(status) || [data]
// Any change to length-prefix endianness, field ordering, or status byte width
// will flip these digests.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(crossPlatformHashStable_FIB99_V3_17_BE)
{
    crypto::Keccak256 hashImpl;
    constexpr auto v31 = static_cast<uint32_t>(protocol::BlockVersion::V3_1_VERSION);
    const auto fix = featuresWithFix();

    // MODIFIED entry: preimage = 00 00 00 03 'tbl' 00 00 00 03 'key' 03 'value'
    Entry modified;
    modified.importFields({"value"});
    BOOST_CHECK_EQUAL(modified.hash("tbl", "key", hashImpl, v31, fix),
        crypto::HashType("0x8079da3138e857fbdb6c43b1209696220bb398fb4ceb6fdf136bf010b3c5fbad"));

    // DELETED entry: preimage = 00 00 00 03 'tbl' 00 00 00 03 'key' 01  (no data)
    Entry deleted;
    deleted.setStatus(Entry::DELETED);
    BOOST_CHECK_EQUAL(deleted.hash("tbl", "key", hashImpl, v31, fix),
        crypto::HashType("0x785a147df3f07ff4daf914a8b0f39cd1d67c9ab7da043734f873c4f581cea221"));
}

BOOST_AUTO_TEST_SUITE_END()
