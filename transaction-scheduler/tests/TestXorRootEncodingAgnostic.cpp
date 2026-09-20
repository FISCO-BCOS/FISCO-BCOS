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
 * @file TestXorRootEncodingAgnostic.cpp
 * @brief The XOR state root is a function of the LOGICAL state alone: the same logical
 *        account rows stored in an all-hex layout, an all-binary layout and a mixed
 *        layout (a node mid-migration between the hex and binary encodings) must fold to
 *        the same root.
 *        Entry::hash normalizes binary account table names (and binary s_tables keys)
 *        to the canonical hex form — this test pins that normalization end to end.
 */

#include "bcos-framework/ledger/AccountTableName.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-task/Wait.h"
#include "bcos-transaction-scheduler/BaselineSchedulerMPTHelpers.h"
#include <boost/test/unit_test.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using namespace bcos;
using namespace bcos::storage2;

using XorEncStorage = memory_storage::MemoryStorage<executor_v1::StateKey,
    executor_v1::StateValue, memory_storage::Attribute(memory_storage::ORDERED)>;

// One logical account: registration row in s_tables plus nonce/balance/slot rows.
// When @p binary is true the rows live in the "/s/<20 raw bytes>" table, otherwise in
// the legacy "/apps/<40 lowercase hex>" table.
task::Task<void> writeAccount(XorEncStorage& storage, std::string_view hexAddress,
    std::string_view nonce, std::string_view balance, bool binary)
{
    namespace account = ledger::account;
    std::string const hexTable = "/apps/" + std::string(hexAddress);
    std::string const table =
        binary ? account::hexToBinaryAccountTableName(hexTable) : hexTable;
    BOOST_REQUIRE(!table.empty());

    co_await storage2::writeOne(storage, executor_v1::StateKey{ledger::SYS_TABLES, table},
        storage::Entry{std::string_view{"value"}});
    co_await storage2::writeOne(
        storage, executor_v1::StateKey{table, "nonce"}, storage::Entry{nonce});
    co_await storage2::writeOne(
        storage, executor_v1::StateKey{table, "balance"}, storage::Entry{balance});

    std::string slotKey(32, '\0');
    slotKey[31] = 0x11;
    std::string slotValue(32, '\0');
    slotValue[31] = 0x42;
    co_await storage2::writeOne(
        storage, executor_v1::StateKey{table, slotKey}, storage::Entry{slotValue});
}

task::Task<h256> rootOf(XorEncStorage& storage, crypto::Hash const& hashImpl,
    ledger::Features const& features)
{
    co_return co_await scheduler_v1::xorStateRoot(storage,
        static_cast<uint32_t>(protocol::BlockVersion::V3_6_VERSION), hashImpl, features);
}
}  // namespace

BOOST_AUTO_TEST_SUITE(XorRootEncodingAgnosticSuite)

BOOST_AUTO_TEST_CASE(sameLogicalStateSameRootAcrossLayouts)
{
    task::syncWait([]() -> task::Task<void> {
        crypto::Keccak256 hashImpl;

        // Two accounts; address2 carries a 0x3a (':') byte so the binary table name
        // exercises the StateKey fixed-offset split rule.
        constexpr std::string_view address1 = "4200000000000000000000000000000000001234";
        constexpr std::string_view address2 = "3a00000000000000000000000000000000000007";

        XorEncStorage allHex;
        co_await writeAccount(allHex, address1, "5", "999", false);
        co_await writeAccount(allHex, address2, "7", "12345", false);

        XorEncStorage allBinary;
        co_await writeAccount(allBinary, address1, "5", "999", true);
        co_await writeAccount(allBinary, address2, "7", "12345", true);

        XorEncStorage mixed;
        co_await writeAccount(mixed, address1, "5", "999", false);  // unmigrated account
        co_await writeAccount(mixed, address2, "7", "12345", true);  // migrated account

        // v3.1 digest format (bugfix_statestorage_hash_v3_17 off).
        ledger::Features featuresOff;
        auto const rootHex = co_await rootOf(allHex, hashImpl, featuresOff);
        auto const rootBinary = co_await rootOf(allBinary, hashImpl, featuresOff);
        auto const rootMixed = co_await rootOf(mixed, hashImpl, featuresOff);
        BOOST_CHECK_NE(rootHex, h256{});
        BOOST_CHECK_EQUAL(rootHex, rootBinary);
        BOOST_CHECK_EQUAL(rootHex, rootMixed);

        // v3.17 length-prefixed digest format (bugfix_statestorage_hash_v3_17 on).
        ledger::Features featuresOn;
        featuresOn.set(ledger::Features::Flag::bugfix_statestorage_hash_v3_17);
        auto const rootHex17 = co_await rootOf(allHex, hashImpl, featuresOn);
        auto const rootBinary17 = co_await rootOf(allBinary, hashImpl, featuresOn);
        auto const rootMixed17 = co_await rootOf(mixed, hashImpl, featuresOn);
        BOOST_CHECK_EQUAL(rootHex17, rootBinary17);
        BOOST_CHECK_EQUAL(rootHex17, rootMixed17);

        // Sanity: the two digest formats differ (the v3.17 fix really engages).
        BOOST_CHECK_NE(rootHex, rootHex17);

        // A real logical difference must still move the root (the fold is not constant).
        XorEncStorage changed;
        co_await writeAccount(changed, address1, "6", "999", false);  // nonce bumped
        co_await writeAccount(changed, address2, "7", "12345", false);
        auto const rootChanged = co_await rootOf(changed, hashImpl, featuresOff);
        BOOST_CHECK_NE(rootHex, rootChanged);
    }());
}

BOOST_AUTO_TEST_SUITE_END()
