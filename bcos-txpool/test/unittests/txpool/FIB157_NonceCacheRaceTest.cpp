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
 * @file FIB157_NonceCacheRaceTest.cpp
 * @brief Regression tests for FIB-157: Web3NonceChecker::updateNonceCache() must not
 *        delete a newer m_maxNonces entry that was published by a concurrent
 *        insertMemoryNonce(). The fix replaces the read-then-remove pair on m_maxNonces
 *        with an atomic predicate-guarded remove (storage2::removeOneIf) so the predicate
 *        evaluation and the remove happen under the same bucket lock.
 */

#include "bcos-utilities/Common.h"
#include "test/unittests/txpool/TxPoolFixture.h"
#include <bcos-framework/storage2/Storage.h>
#include <bcos-task/Wait.h>
#include <bcos-txpool/txpool/validator/Web3NonceChecker.h>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <set>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace bcos;
using namespace bcos::txpool;
using namespace bcos::protocol;
using namespace bcos::crypto;

namespace bcos::test
{
class FIB157Fixture : public TxPoolFixture
{
public:
    FIB157Fixture() : TxPoolFixture(), checker(m_ledger) {}
    Web3NonceChecker checker;
};

BOOST_FIXTURE_TEST_SUITE(FIB157_NonceCacheRace, FIB157Fixture)

// Reference for the bug:
// updateNonceCache() observes m_maxNonces == 6 (just bumped by a concurrent
// insertMemoryNonce()) and proceeds to remove it because its locally observed maxNonce ==
// 5 was already <= the cached value at read-time. With the FIB-157 fix the predicate
// (maxNonce >= existing) is evaluated under the same bucket lock as the remove, so the
// stale 5 cannot delete a freshly written 6.
BOOST_AUTO_TEST_CASE(updateNonceCacheDoesNotDeleteNewerEntry)
{
    // Run many iterations to expose the race; even one stale-delete invalidates the test.
    constexpr int iterations = 1000;
    int regressions = 0;

    for (int it = 0; it < iterations; ++it)
    {
        auto sender = Address::generateRandomFixedBytes().toRawString();
        auto senderHex = toHex(sender);

        // Seed: insert nonce 5 so m_maxNonces[sender] == 6 (fix stores nonce+1).
        BOOST_REQUIRE(task::syncWait(checker.insertMemoryNonce(sender, "5")));

        // Bumper concurrently raises m_maxNonces well past the updater's locally observed
        // ledger max. Updater calls updateNonceCache with ledger maxNonce=5 (committed +1
        // == 6); the predicate (6 >= existing) must reject the remove once the bumper has
        // published a value > 6.
        std::atomic<bool> startGate{false};
        std::thread bumper([&] {
            while (!startGate.load(std::memory_order_acquire))
            {
            }
            for (int n = 6; n < 50; ++n)
            {
                task::syncWait(checker.insertMemoryNonce(sender, std::to_string(n)));
            }
        });

        std::thread updater([&] {
            std::unordered_map<std::string, std::set<u256>> commitMap;
            commitMap[sender] = {u256(5)};
            while (!startGate.load(std::memory_order_acquire))
            {
            }
            task::syncWait(checker.updateNonceCache(::ranges::views::all(commitMap)));
        });

        startGate.store(true, std::memory_order_release);
        bumper.join();
        updater.join();

        // Final state: bumper inserted up to nonce 49, so m_maxNonces should hold 50.
        // If the buggy code wins the race, m_maxNonces is empty and getPendingNonce falls
        // through to the ledger value (which is 0 / nullopt for this fresh sender) — a
        // regression.
        auto pending = task::syncWait(checker.getPendingNonce(senderHex));
        if (!pending.has_value() || pending.value() < u256(7))
        {
            ++regressions;
        }
    }

    BOOST_CHECK_EQUAL(regressions, 0);
}

// Direct unit test for the new storage2::removeOneIf primitive: the predicate must see
// the latest value, not a stale one, and the remove must not happen if the predicate
// rejects it.
BOOST_AUTO_TEST_CASE(removeOneIfRejectsWhenPredicateFalse)
{
    using namespace bcos::storage2::memory_storage;
    bcos::storage2::memory_storage::MemoryStorage<std::string, u256, Attribute(CONCURRENT | LRU)>
        storage(4, 1024);

    task::syncWait([&]() -> task::Task<void> {
        co_await bcos::storage2::writeOne(storage, std::string("k"), u256(10));

        // Predicate false -> no removal, returns false.
        bool removed = co_await bcos::storage2::removeOneIf(
            storage, std::string("k"), [](u256 const& existing) { return existing < 5; });
        BOOST_CHECK(!removed);
        auto val = co_await bcos::storage2::readOne(storage, std::string("k"));
        BOOST_REQUIRE(val.has_value());
        BOOST_CHECK_EQUAL(*val, u256(10));

        // Predicate true -> removal performed, returns true.
        removed = co_await bcos::storage2::removeOneIf(
            storage, std::string("k"), [](u256 const& existing) { return existing >= 10; });
        BOOST_CHECK(removed);
        auto val2 = co_await bcos::storage2::readOne(storage, std::string("k"));
        BOOST_CHECK(!val2.has_value());

        // Removing an absent key is a no-op and returns false.
        removed = co_await bcos::storage2::removeOneIf(
            storage, std::string("missing"), [](u256 const&) { return true; });
        BOOST_CHECK(!removed);
        co_return;
    }());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
