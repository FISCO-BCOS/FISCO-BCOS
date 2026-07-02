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
 * @file MPTReadViewTest.cpp
 * @brief Unit tests for MPTReadView account lookups (spec §5.6, §7.1)
 */

#include "TestHelpers.h"
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-ledger/mpt/Account.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-ledger/mpt/MPTReadView.h>
#include <bcos-ledger/mpt/Trie.h>
#include <bcos-task/Task.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/test/unit_test.hpp>
#include <optional>

namespace bcos::ledger::mpt::test
{

BOOST_AUTO_TEST_SUITE(MPTReadViewSuite)

// An empty root has no accounts: hasAccount is false and no node fetch ever happens (so an empty
// storage is fine).
BOOST_AUTO_TEST_CASE(HasAccountFalseOnEmptyRoot)
{
    bcos::storage2::memory_storage::MemoryStorage<bcos::h256, bcos::bytes> storage;
    MPTReadView view(storage, emptyRootHash());

    bcos::Address const a = makeAddress(0xab);
    BOOST_CHECK(!bcos::task::syncWait(view.hasAccount(a)));
    BOOST_CHECK(!bcos::task::syncWait(view.readAccount(a)).has_value());
}

// An account committed through HashBuilder into the SAME storage reads back through MPTReadView.
// The decoded fields must match what was encoded.
BOOST_AUTO_TEST_CASE(HasAccountTrueAfterCommit)
{
    bcos::storage2::memory_storage::MemoryStorage<bcos::h256, bcos::bytes> storage;
    bcos::Address const a = makeAddress(0xab);

    HashBuilder hb(storage, emptyRootHash());
    Account acc;
    acc.nonce = 1;
    acc.balance = 100;
    auto const kh = accountKeyHash(a);
    bcos::task::syncWait(hb.put(kh, acc.encode()));
    auto const root = bcos::task::syncWait(hb.commit());

    MPTReadView view(storage, root);

    BOOST_CHECK(bcos::task::syncWait(view.hasAccount(a)));
    auto got = bcos::task::syncWait(view.readAccount(a));
    BOOST_REQUIRE(got.has_value());
    BOOST_CHECK_EQUAL(got->nonce, 1);
    BOOST_CHECK_EQUAL(got->balance, 100);

    // A different, never-committed address is absent.
    bcos::Address const b = makeAddress(0xcd);
    BOOST_CHECK(!bcos::task::syncWait(view.hasAccount(b)));
}

// The read view is storage-agnostic: build into storageA, drain its nodes into a FRESH storageB
// (writeOne each), then read the account back through a view over storageB. Since MPTReadView only
// depends on the storage2::ReadableStorage concept, the nodes need not live in the storage they
// were built in — a fresh process can serve reads straight from its own node store.
BOOST_AUTO_TEST_CASE(ReadThroughSeparateStorage)
{
    bcos::Address const a = makeAddress(0xab);

    // Build the trie into storageA and capture every node it produced.
    bcos::storage2::memory_storage::MemoryStorage<bcos::h256, bcos::bytes> storageA;
    HashBuilder hb(storageA, emptyRootHash());
    Account acc;
    acc.nonce = 7;
    acc.balance = 4242;
    auto const kh = accountKeyHash(a);
    bcos::task::syncWait(hb.put(kh, acc.encode()));
    auto const root = bcos::task::syncWait(hb.commit());

    auto const nodes = hb.drainNewNodes();
    BOOST_REQUIRE(!nodes.empty());

    // Load the drained nodes into a fresh, independent storage.
    bcos::storage2::memory_storage::MemoryStorage<bcos::h256, bcos::bytes> storageB;
    for (auto const& [hash, raw] : nodes)
    {
        bcos::task::syncWait(bcos::storage2::writeOne(storageB, hash, raw));
    }

    // Reads over storageB resolve the same account.
    MPTReadView view(storageB, root);
    auto got = bcos::task::syncWait(view.readAccount(a));
    BOOST_REQUIRE(got.has_value());
    BOOST_CHECK_EQUAL(got->nonce, 7);
    BOOST_CHECK_EQUAL(got->balance, 4242);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
