/**
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
 * @brief FIB-154: TxPool pre-store enqueue must dedup by block hash, enforce a hard
 *        cap, and clean up on success AND exception.
 * @file FIB154_PreStoreDedupBacklogTest.cpp
 */
#include "TxPoolFixture.h"
#include <boost/test/unit_test.hpp>
#include <cstring>

namespace bcos::test
{
namespace
{

inline bcos::crypto::HashType makeBlockHashFib154(int64_t n)
{
    bcos::crypto::HashType h;
    std::memset(h.data(), 0, h.size());
    std::memcpy(h.data(), &n, sizeof(n));
    return h;
}

}  // namespace

BOOST_FIXTURE_TEST_SUITE(FIB154PreStoreDedupBacklogTest, TxPoolFixture)

// Scenario 1: Enqueueing the same block hash twice must not increment the count.
BOOST_AUTO_TEST_CASE(duplicate_block_hash_skipped)
{
    auto h = makeBlockHashFib154(1);
    m_txpool->testOnlyEnqueuePreStore(h);
    auto count1 = m_txpool->testOnlyPreStoreCount();
    BOOST_CHECK_EQUAL(count1, 1u);

    // Enqueue duplicate — should be silently ignored
    m_txpool->testOnlyEnqueuePreStore(h);
    BOOST_CHECK_EQUAL(m_txpool->testOnlyPreStoreCount(), count1);

    // Cleanup: remove the entry so the test is self-contained
    m_txpool->testOnlyCleanupPreStore(h);
    BOOST_CHECK_EQUAL(m_txpool->testOnlyPreStoreCount(), 0u);
}

// Scenario 2: Once the cap is reached, additional enqueues must be dropped.
BOOST_AUTO_TEST_CASE(backlog_cap_drops_excess)
{
    constexpr std::size_t cap = 32;  // matches c_maxPreStoreInFlight
    for (std::size_t i = 0; i < cap; ++i)
    {
        m_txpool->testOnlyEnqueuePreStore(makeBlockHashFib154(static_cast<int64_t>(i + 100)));
    }
    // All cap entries should be accepted
    BOOST_CHECK_EQUAL(m_txpool->testOnlyPreStoreCount(), cap);

    // One more entry beyond cap — must be dropped
    m_txpool->testOnlyEnqueuePreStore(makeBlockHashFib154(9999));
    BOOST_CHECK_LE(m_txpool->testOnlyPreStoreCount(), cap);

    // Cleanup
    for (std::size_t i = 0; i < cap; ++i)
    {
        m_txpool->testOnlyCleanupPreStore(makeBlockHashFib154(static_cast<int64_t>(i + 100)));
    }
    BOOST_CHECK_EQUAL(m_txpool->testOnlyPreStoreCount(), 0u);
}

// Scenario 3: Count must be decremented after successful processing.
BOOST_AUTO_TEST_CASE(cleanup_on_success)
{
    auto h = makeBlockHashFib154(99);
    m_txpool->testOnlyEnqueuePreStoreSyncSuccess(h);
    BOOST_CHECK_EQUAL(m_txpool->testOnlyPreStoreCount(), 0u);
}

// Scenario 4: Count must be decremented even when an exception is thrown.
BOOST_AUTO_TEST_CASE(cleanup_on_exception)
{
    auto h = makeBlockHashFib154(100);
    m_txpool->testOnlyEnqueuePreStoreSyncThrow(h);
    BOOST_CHECK_EQUAL(m_txpool->testOnlyPreStoreCount(), 0u);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
