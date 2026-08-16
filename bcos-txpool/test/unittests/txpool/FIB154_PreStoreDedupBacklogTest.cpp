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
 * @brief FIB-154: TxPool pre-store gate must dedup by block hash, enforce the
 *        runtime cap, honor the enable flag, and clean up on every exit path.
 *        Tests drive the production gate methods via an InspectableTxPool
 *        subclass that re-exports them — no parallel re-implementation.
 * @file FIB154_PreStoreDedupBacklogTest.cpp
 */
#include "TxPoolFixture.h"
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

// Subclass that re-exports the protected gate so UTs call the same functions
// the production lambda calls (FIB-132 InspectablePBFTEngine pattern).
class InspectableTxPool : public bcos::txpool::TxPool
{
public:
    using Ptr = std::shared_ptr<InspectableTxPool>;
    using bcos::txpool::TxPool::TxPool;
    using bcos::txpool::TxPool::PreStoreAdmission;
    using bcos::txpool::TxPool::tryAcquirePreStoreSlot;
    using bcos::txpool::TxPool::releasePreStoreSlot;
    using bcos::txpool::TxPool::m_preStoreInFlight;
    using bcos::txpool::TxPool::x_preStoreInFlight;
};

inline std::size_t inflightSize(InspectableTxPool& p)
{
    std::lock_guard lk(p.x_preStoreInFlight);
    return p.m_preStoreInFlight.size();
}

inline InspectableTxPool::Ptr makeInspectable(bcos::txpool::TxPool::Ptr const& base,
    bcos::IOServicePool::Ptr ioServicePool)
{
    // Reuse the fixture's already-constructed components. The new instance is
    // never start()ed, so its destructor's stop() takes the m_running==false
    // fast path and does not double-stop the fixture's storage/sync.
    return std::make_shared<InspectableTxPool>(
        base->txpoolConfig(), base->txpoolStorage(), base->transactionSync(), 1, ioServicePool);
}

}  // namespace

BOOST_FIXTURE_TEST_SUITE(FIB154PreStoreDedupBacklogTest, TxPoolFixture)

// 1. Duplicate hash: second acquire returns DuplicateSkipped and does not grow set.
BOOST_AUTO_TEST_CASE(duplicate_block_hash_skipped)
{
    auto pool = makeInspectable(m_txpool, ioServicePool);
    auto h = makeBlockHashFib154(1);

    BOOST_CHECK(pool->tryAcquirePreStoreSlot(h) ==
                InspectableTxPool::PreStoreAdmission::Accepted);
    BOOST_CHECK_EQUAL(inflightSize(*pool), 1u);

    BOOST_CHECK(pool->tryAcquirePreStoreSlot(h) ==
                InspectableTxPool::PreStoreAdmission::DuplicateSkipped);
    BOOST_CHECK_EQUAL(inflightSize(*pool), 1u);

    pool->releasePreStoreSlot(h);
    BOOST_CHECK_EQUAL(inflightSize(*pool), 0u);
}

// 2. Cap is honored exactly. Setting cap to a small number avoids needing 1024
//    distinct hashes per test.
BOOST_AUTO_TEST_CASE(backlog_cap_drops_excess)
{
    auto pool = makeInspectable(m_txpool, ioServicePool);
    constexpr std::size_t cap = 4;
    pool->setPreStoreMaxInflight(cap);

    for (std::size_t i = 0; i < cap; ++i)
    {
        BOOST_CHECK(pool->tryAcquirePreStoreSlot(
                        makeBlockHashFib154(static_cast<int64_t>(i + 100))) ==
                    InspectableTxPool::PreStoreAdmission::Accepted);
    }
    BOOST_CHECK_EQUAL(inflightSize(*pool), cap);

    // One more — must be dropped, count stays exactly at cap.
    BOOST_CHECK(pool->tryAcquirePreStoreSlot(makeBlockHashFib154(9999)) ==
                InspectableTxPool::PreStoreAdmission::CapDropped);
    BOOST_CHECK_EQUAL(inflightSize(*pool), cap);

    // Release one — a fresh hash can now be admitted.
    pool->releasePreStoreSlot(makeBlockHashFib154(100));
    BOOST_CHECK_EQUAL(inflightSize(*pool), cap - 1);
    BOOST_CHECK(pool->tryAcquirePreStoreSlot(makeBlockHashFib154(9999)) ==
                InspectableTxPool::PreStoreAdmission::Accepted);
    BOOST_CHECK_EQUAL(inflightSize(*pool), cap);

    // Cleanup
    for (std::size_t i = 1; i < cap; ++i)
    {
        pool->releasePreStoreSlot(makeBlockHashFib154(static_cast<int64_t>(i + 100)));
    }
    pool->releasePreStoreSlot(makeBlockHashFib154(9999));
    BOOST_CHECK_EQUAL(inflightSize(*pool), 0u);
}

// 3. Disabled mode: gate returns Disabled, never inserts into the in-flight set.
BOOST_AUTO_TEST_CASE(backpressure_disabled_bypasses_gate)
{
    auto pool = makeInspectable(m_txpool, ioServicePool);
    pool->setPreStoreBackpressureEnabled(false);

    for (int i = 0; i < 8; ++i)
    {
        BOOST_CHECK(pool->tryAcquirePreStoreSlot(makeBlockHashFib154(i)) ==
                    InspectableTxPool::PreStoreAdmission::Disabled);
    }
    BOOST_CHECK_EQUAL(inflightSize(*pool), 0u);

    // Flipping back on restores admission control.
    pool->setPreStoreBackpressureEnabled(true);
    auto h = makeBlockHashFib154(123);
    BOOST_CHECK(pool->tryAcquirePreStoreSlot(h) ==
                InspectableTxPool::PreStoreAdmission::Accepted);
    BOOST_CHECK_EQUAL(inflightSize(*pool), 1u);
    pool->releasePreStoreSlot(h);
}

// 4. Cleanup on success path: release brings set to 0.
BOOST_AUTO_TEST_CASE(cleanup_on_success)
{
    auto pool = makeInspectable(m_txpool, ioServicePool);
    auto h = makeBlockHashFib154(7);

    BOOST_CHECK(pool->tryAcquirePreStoreSlot(h) ==
                InspectableTxPool::PreStoreAdmission::Accepted);
    pool->releasePreStoreSlot(h);
    BOOST_CHECK_EQUAL(inflightSize(*pool), 0u);
}

// 5. Cleanup on exception path: release from a catch block also brings to 0.
//    Mirrors the lambda's catch-then-release contract in production code.
BOOST_AUTO_TEST_CASE(cleanup_on_exception)
{
    auto pool = makeInspectable(m_txpool, ioServicePool);
    auto h = makeBlockHashFib154(8);

    BOOST_CHECK(pool->tryAcquirePreStoreSlot(h) ==
                InspectableTxPool::PreStoreAdmission::Accepted);
    try
    {
        throw std::runtime_error("simulated storeVerifiedBlock failure");
    }
    catch (std::exception const&)
    {
        pool->releasePreStoreSlot(h);
    }
    BOOST_CHECK_EQUAL(inflightSize(*pool), 0u);
}

// 6. setPreStoreMaxInflight(0) must be rejected — would otherwise block all work.
BOOST_AUTO_TEST_CASE(setPreStoreMaxInflight_rejects_zero)
{
    auto pool = makeInspectable(m_txpool, ioServicePool);
    pool->setPreStoreMaxInflight(7);
    pool->setPreStoreMaxInflight(0);  // must be ignored

    // Fill to old cap (7), then verify 8th drops — proves cap is still 7, not 0.
    for (int i = 0; i < 7; ++i)
    {
        BOOST_CHECK(pool->tryAcquirePreStoreSlot(makeBlockHashFib154(i + 1000)) ==
                    InspectableTxPool::PreStoreAdmission::Accepted);
    }
    BOOST_CHECK(pool->tryAcquirePreStoreSlot(makeBlockHashFib154(9000)) ==
                InspectableTxPool::PreStoreAdmission::CapDropped);
    for (int i = 0; i < 7; ++i)
    {
        pool->releasePreStoreSlot(makeBlockHashFib154(i + 1000));
    }
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
