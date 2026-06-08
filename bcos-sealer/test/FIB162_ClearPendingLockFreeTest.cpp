/*
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
 * @file FIB162_ClearPendingLockFreeTest.cpp
 * @brief Regression test for FIB-162: SealingManager::clearPendingTxs() must
 *        not hold x_pendingTxs across the synchronous-inline asyncMarkTxs
 *        callback path, and notifyResetTxsFlag must skip retry on the
 *        deterministic TransactionsMissing error.
 *
 *        Test architecture: drives the production SealingManager through its
 *        public API — testOnlySeedPendingTxs / testOnlySeedSysPendingTxs to
 *        stage, resetSealing() to invoke clearPendingTxs(), and
 *        testOnlyPendingWriteLockFree() (probed from a separate thread) to
 *        observe lock state. The InlineTxPool stub mirrors the real
 *        synchronous-inline callback contract of TxPool::asyncMarkTxs.
 */

#include "bcos-crypto/bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/protocol/CommonError.h"
#include "bcos-framework/txpool/TxPoolInterface.h"
#include "bcos-sealer/SealerConfig.h"
#include "bcos-sealer/SealingManager.h"
#include "bcos-tars-protocol/protocol/BlockFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h"
#include "bcos-tool/NodeTimeMaintenance.h"
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Error.h>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <memory>
#include <thread>
#include <utility>

namespace bcos::test
{
namespace
{
// Synchronous-inline txpool stub. The real asyncMarkTxs invokes its
// callback inline on the caller's stack (despite the "async" name). The
// stub mirrors that behaviour and lets the test:
//   - count how many times asyncMarkTxs was invoked (retry count)
//   - script per-call error outcomes (OK / TransactionsMissing / transient)
//   - run a probe function from inside asyncMarkTxs, before the callback,
//     to observe lock state at the moment the bug would manifest
struct InlineTxPool : public txpool::TxPoolInterface
{
    enum class Mode : uint8_t
    {
        OK,
        AlwaysTransient,
        AlwaysTransactionsMissing,
        Throws,
    };

    Mode mode = Mode::OK;
    std::atomic<size_t> markCalls{0};
    std::function<void()> probe;
    bcos::crypto::HashList lastHashes;

    void asyncMarkTxs(const bcos::crypto::HashList& _txsHash, bool /*_sealedFlag*/,
        bcos::protocol::BlockNumber /*_batchId*/, bcos::crypto::HashType const& /*_batchHash*/,
        std::function<void(Error::Ptr)> _onRecvResponse) override
    {
        ++markCalls;
        lastHashes = _txsHash;
        if (probe)
        {
            probe();
        }
        if (mode == Mode::Throws)
        {
            throw std::runtime_error("simulated asyncMarkTxs throw");
        }
        Error::Ptr err;
        if (mode == Mode::AlwaysTransient)
        {
            err = BCOS_ERROR_PTR(-1, "transient");
        }
        else if (mode == Mode::AlwaysTransactionsMissing)
        {
            err = BCOS_ERROR_PTR(bcos::protocol::CommonError::TransactionsMissing, "missing");
        }
        _onRecvResponse(std::move(err));
    }

    // ── unused interface members ───────────────────────────────────────
    std::tuple<std::vector<bcos::protocol::TransactionMetaData::Ptr>,
        std::vector<bcos::protocol::TransactionMetaData::Ptr>>
    sealTxs(uint64_t) override
    {
        return {};
    }
    void tryToSyncTxsFromPeers() override {}
    void start() override {}
    void stop() override {}
    void asyncVerifyBlock(bcos::crypto::PublicPtr, bcos::protocol::Block::ConstPtr,
        std::function<void(Error::Ptr, bool)>) override
    {}
    void asyncFillBlock(bcos::crypto::HashListPtr,
        std::function<void(Error::Ptr, bcos::protocol::ConstTransactionsPtr)>) override
    {}
    void asyncNotifyBlockResult(bcos::protocol::BlockNumber,
        bcos::protocol::TransactionSubmitResultsPtr, std::function<void(Error::Ptr)>) override
    {}
    void asyncNotifyTxsSyncMessage(bcos::Error::Ptr, std::string const&, bcos::crypto::NodeIDPtr,
        bytesConstRef, std::function<void(Error::Ptr)>) override
    {}
    void notifyConsensusNodeList(
        bcos::consensus::ConsensusNodeList const&, std::function<void(Error::Ptr)>) override
    {}
    void notifyObserverNodeList(
        bcos::consensus::ConsensusNodeList const&, std::function<void(Error::Ptr)>) override
    {}
    void asyncGetPendingTransactionSize(std::function<void(Error::Ptr, uint64_t)>) override {}
    void asyncResetTxPool(std::function<void(Error::Ptr)>) override {}
    void notifyConnectedNodes(
        bcos::crypto::NodeIDSet const&, std::function<void(Error::Ptr)>) override
    {}
};
}  // namespace

struct FIB162Fixture
{
    FIB162Fixture()
    {
        auto hashImpl = std::make_shared<crypto::Keccak256>();
        auto signatureImpl = std::make_shared<crypto::Secp256k1Crypto>();
        auto cryptoSuite = std::make_shared<crypto::CryptoSuite>(hashImpl, signatureImpl, nullptr);
        auto blockHeaderFactory =
            std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite);
        blockFactory = std::make_shared<bcostars::protocol::BlockFactoryImpl>(
            cryptoSuite, blockHeaderFactory, nullptr, nullptr);
        txpool = std::make_shared<InlineTxPool>();
        auto timeMaintenance = std::make_shared<bcos::tool::NodeTimeMaintenance>();
        config = std::make_shared<sealer::SealerConfig>(blockFactory, txpool, timeMaintenance);
        sm = std::make_shared<sealer::SealingManager>(config);
    }

    bcos::protocol::TransactionMetaData::Ptr makeMeta(uint8_t seed) const
    {
        bcos::crypto::HashType h;
        h.data()[0] = seed;
        return blockFactory->createTransactionMetaData(h, std::string{"to"});
    }

    void stagePending(size_t normalCount, size_t sysCount)
    {
        std::vector<bcos::protocol::TransactionMetaData::Ptr> normal;
        normal.reserve(normalCount);
        for (size_t i = 0; i < normalCount; ++i)
        {
            normal.push_back(makeMeta(static_cast<uint8_t>(0x10 + i)));
        }
        sm->testOnlySeedPendingTxs(normal);

        std::vector<bcos::protocol::TransactionMetaData::Ptr> sys;
        sys.reserve(sysCount);
        for (size_t i = 0; i < sysCount; ++i)
        {
            sys.push_back(makeMeta(static_cast<uint8_t>(0xA0 + i)));
        }
        sm->testOnlySeedSysPendingTxs(sys);
    }

    // Probe x_pendingTxs from a SEPARATE thread. boost::shared_mutex's
    // same-thread try_lock is not well-defined, and the pre-fix code path
    // holds the lock on the thread that runs the txpool callback — so a
    // same-thread probe there would be UB. A separate thread gives a clean
    // "is the lock free right now?" answer for both pre- and post-fix code.
    bool canAcquireWriteLockNow() const
    {
        std::atomic<bool> gotLock{false};
        std::thread t([this, &gotLock]() { gotLock.store(sm->testOnlyPendingWriteLockFree()); });
        t.join();
        return gotLock.load();
    }

    size_t pendingTotal() const { return sm->testOnlyPendingTxsSize(); }

    // resetSealing() is the public entry point that drives clearPendingTxs().
    void invokeClear() { sm->resetSealing(); }

    std::shared_ptr<bcostars::protocol::BlockFactoryImpl> blockFactory;
    std::shared_ptr<InlineTxPool> txpool;
    sealer::SealerConfig::Ptr config;
    std::shared_ptr<sealer::SealingManager> sm;
};

BOOST_FIXTURE_TEST_SUITE(FIB162ClearPendingLockFreeTest, FIB162Fixture)

// T1 — empty pending: asyncMarkTxs must NOT be called.
BOOST_AUTO_TEST_CASE(t1_empty_pending_no_mark)
{
    invokeClear();
    BOOST_CHECK_EQUAL(txpool->markCalls.load(), 0U);
}

// T2 — snapshot correctness: 3 sys + 2 normal → 5 hashes, queues cleared.
BOOST_AUTO_TEST_CASE(t2_snapshot_then_clear)
{
    stagePending(/*normal=*/2, /*sys=*/3);
    BOOST_REQUIRE_EQUAL(pendingTotal(), 5U);
    invokeClear();
    BOOST_CHECK_EQUAL(txpool->markCalls.load(), 1U);
    BOOST_CHECK_EQUAL(txpool->lastHashes.size(), 5U);
    BOOST_CHECK_EQUAL(pendingTotal(), 0U);
}

// T3 — lock released before asyncMarkTxs runs. The probe captures whether
// a writer can acquire x_pendingTxs at the moment of the callback FROM A
// DIFFERENT THREAD. Pre-fix: caller holds UpgradableGuard → cross-thread
// writer try_lock returns false. Post-fix: lock released before notify →
// try_lock returns true.
BOOST_AUTO_TEST_CASE(t3_lock_released_before_async_call)
{
    stagePending(/*normal=*/1, /*sys=*/1);
    bool seenLockFree = false;
    txpool->probe = [this, &seenLockFree]() { seenLockFree = canAcquireWriteLockNow(); };
    invokeClear();
    BOOST_CHECK_MESSAGE(seenLockFree,
        "x_pendingTxs must be released before notifyResetTxsFlag invokes asyncMarkTxs");
}

// T4 — TransactionsMissing must not be retried.
BOOST_AUTO_TEST_CASE(t4_transactions_missing_skips_retry)
{
    stagePending(/*normal=*/1, /*sys=*/0);
    txpool->mode = InlineTxPool::Mode::AlwaysTransactionsMissing;
    invokeClear();
    BOOST_CHECK_EQUAL(txpool->markCalls.load(), 1U);
}

// T5 — transient errors retry up to 3 times (4 total calls).
BOOST_AUTO_TEST_CASE(t5_transient_error_retries_capped_at_three)
{
    stagePending(/*normal=*/1, /*sys=*/0);
    txpool->mode = InlineTxPool::Mode::AlwaysTransient;
    invokeClear();
    BOOST_CHECK_EQUAL(txpool->markCalls.load(), 4U);
}

// T6 — same retry semantics from a direct notifyResetTxsFlag call too.
BOOST_AUTO_TEST_CASE(t6_notify_reset_direct_retry_cap)
{
    txpool->mode = InlineTxPool::Mode::AlwaysTransient;
    bcos::crypto::HashList hashes{bcos::crypto::HashType{}};
    sm->notifyResetTxsFlag(hashes, false);
    BOOST_CHECK_EQUAL(txpool->markCalls.load(), 4U);
}

// T7 — asyncMarkTxs throws synchronously: outer try/catch in clearPendingTxs
// swallows the exception, queues must still be cleared.
BOOST_AUTO_TEST_CASE(t7_async_throws_still_clears)
{
    stagePending(/*normal=*/2, /*sys=*/0);
    txpool->mode = InlineTxPool::Mode::Throws;
    BOOST_CHECK_NO_THROW(invokeClear());
    BOOST_CHECK_EQUAL(pendingTotal(), 0U);
}

// T8 — during the (transient-mode) retry chain, the lock must remain free
// for every callback invocation. Same probe as T3, but exercised through
// 4 callbacks (1 initial + 3 retries) — verifies the fix holds across the
// full recursive retry chain.
BOOST_AUTO_TEST_CASE(t8_lock_free_for_each_retry_callback)
{
    stagePending(/*normal=*/3, /*sys=*/2);
    txpool->mode = InlineTxPool::Mode::AlwaysTransient;

    std::atomic<size_t> probeCount{0};
    std::atomic<size_t> probeSeenFree{0};
    txpool->probe = [this, &probeCount, &probeSeenFree]() {
        ++probeCount;
        if (canAcquireWriteLockNow())
        {
            ++probeSeenFree;
        }
    };

    invokeClear();
    BOOST_CHECK_EQUAL(probeCount.load(), 4U);
    BOOST_CHECK_EQUAL(probeSeenFree.load(), 4U);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
