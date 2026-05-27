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
 * @file FIB155_AsyncCallbackLifetimeTest.cpp
 * @brief Regression test for FIB-155: SealingManager::notifyResetTxsFlag must
 *        not capture raw `this` because the asyncMarkTxs callback can outlive
 *        the owning SealingManager during shutdown/reconfiguration. With the
 *        previous raw-`this` capture, invoking the deferred callback after the
 *        SealingManager is destroyed produces a heap-use-after-free (visible
 *        under ASan). After the fix, the callback locks a weak_ptr and exits
 *        cleanly when the owner is gone.
 */

#include "bcos-crypto/bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/txpool/TxPoolInterface.h"
#include "bcos-sealer/SealerConfig.h"
#include "bcos-sealer/SealingManager.h"
#include "bcos-tars-protocol/protocol/BlockFactoryImpl.h"
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-utilities/Error.h>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <memory>
#include <utility>

namespace bcos::test
{
namespace
{
// A txpool mock that captures (defers) the asyncMarkTxs callback and lets the
// test invoke it explicitly. This simulates the realistic scenario where a
// network-driven async response fires after the SealingManager has already
// been destroyed.
struct DeferringTxPool : public txpool::TxPoolInterface
{
    using Callback = std::function<void(Error::Ptr)>;
    std::vector<Callback> deferredCallbacks;
    std::atomic<size_t> markCalls{0};

    void asyncMarkTxs(const bcos::crypto::HashList& /*_txsHash*/, bool /*_sealedFlag*/,
        bcos::protocol::BlockNumber /*_batchId*/, bcos::crypto::HashType const& /*_batchHash*/,
        std::function<void(Error::Ptr)> _onRecvResponse) override
    {
        ++markCalls;
        // intentionally defer — the callback will fire after the test drops
        // the owning SealingManager.
        deferredCallbacks.push_back(std::move(_onRecvResponse));
    }

    // unused interface members
    std::tuple<std::vector<bcos::protocol::TransactionMetaData::Ptr>,
        std::vector<bcos::protocol::TransactionMetaData::Ptr>>
    sealTxs(uint64_t /*_txsLimit*/) override
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

sealer::SealerConfig::Ptr makeSealerConfig(std::shared_ptr<DeferringTxPool> txpool)
{
    auto hashImpl = std::make_shared<crypto::Keccak256>();
    auto signatureImpl = std::make_shared<crypto::Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<crypto::CryptoSuite>(hashImpl, signatureImpl, nullptr);
    auto blockFactory = std::make_shared<bcostars::protocol::BlockFactoryImpl>(
        cryptoSuite, nullptr, nullptr, nullptr);
    return std::make_shared<sealer::SealerConfig>(blockFactory, txpool, nullptr);
}
}  // namespace

BOOST_AUTO_TEST_SUITE(FIB155_AsyncCallbackLifetime)

// Trigger: SealingManager::notifyResetTxsFlag posts an asyncMarkTxs callback
// that captures the owning instance. If the SealingManager goes away before
// the callback fires, dispatching the callback would dereference a dangling
// pointer (heap-use-after-free under ASan).
//
// Verify: after the fix, the callback locks a weak_ptr; if the owner is gone,
// the callback returns cleanly with no UAF and no further retries.
BOOST_AUTO_TEST_CASE(notifyResetTxsFlag_no_uaf_after_destroy)
{
    auto txpool = std::make_shared<DeferringTxPool>();
    auto config = makeSealerConfig(txpool);

    // Scope: create SealingManager, schedule the async callback, then destroy
    // it before the deferred callback runs.
    {
        auto sm = std::make_shared<sealer::SealingManager>(config);
        // Use the canonical entry — line 100 of SealingManager.cpp; we
        // directly drive the async callback with a single hash. Pass _retryTime
        // = 0 to allow up to 3 retries; on raw-`this` this would cause UAF
        // when the callback retries through the dangling pointer.
        bcos::crypto::HashList hashes{bcos::crypto::HashType(
            "0xaabbccddeeff00112233445566778899aabbccddeeff00112233445566778899")};
        sm->notifyResetTxsFlag(hashes, false, /*_retryTime=*/0);
    }
    // SealingManager destroyed here.

    BOOST_REQUIRE_EQUAL(txpool->deferredCallbacks.size(), 1U);
    BOOST_REQUIRE_EQUAL(txpool->markCalls.load(), 1U);

    // Now fire the deferred callback with an error — pre-fix, this would
    // dereference a dangling SealingManager and recurse into another
    // asyncMarkTxs (infinite UAF retry). Post-fix, the lock() returns null
    // and the callback returns cleanly, leaving markCalls at 1.
    auto error = BCOS_ERROR_PTR(-1, "simulated asyncMarkTxs failure");
    BOOST_CHECK_NO_THROW(txpool->deferredCallbacks.front()(std::move(error)));

    // After fix: no retry was scheduled because the weak_ptr lock failed.
    BOOST_CHECK_EQUAL(txpool->markCalls.load(), 1U);
    BOOST_CHECK_EQUAL(txpool->deferredCallbacks.size(), 1U);
}

// Sanity-check that the callback path still functions correctly while the
// SealingManager is alive (i.e. the weak_from_this lock succeeds and the
// retry logic still runs as expected).
BOOST_AUTO_TEST_CASE(notifyResetTxsFlag_retries_while_alive)
{
    auto txpool = std::make_shared<DeferringTxPool>();
    auto config = makeSealerConfig(txpool);
    auto sm = std::make_shared<sealer::SealingManager>(config);

    bcos::crypto::HashList hashes{bcos::crypto::HashType(
        "0x0011223344556677889900112233445566778899001122334455667788990011")};
    sm->notifyResetTxsFlag(hashes, false, /*_retryTime=*/0);

    BOOST_REQUIRE_EQUAL(txpool->deferredCallbacks.size(), 1U);

    // First callback fires with error: the live SealingManager should retry,
    // bumping deferredCallbacks to 2 entries.
    auto firstCb = txpool->deferredCallbacks.front();
    firstCb(BCOS_ERROR_PTR(-1, "first failure"));
    BOOST_CHECK_EQUAL(txpool->markCalls.load(), 2U);
    BOOST_REQUIRE_EQUAL(txpool->deferredCallbacks.size(), 2U);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
