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
 * @file FIB161_SystemTxsSizeShrinkTest.cpp
 * @brief Regression test for FIB-161: when the block-hook injects a system
 *        transaction, SealingManager::generateProposal must shrink
 *        systemTxsSize alongside txsSize so that the resulting block never
 *        exceeds m_maxTxsPerBlock. The pre-fix code captured systemTxsSize
 *        as a const before the hook ran and only decremented txsSize,
 *        causing the second for-loop to over-append.
 */

#include "bcos-crypto/bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/txpool/TxPoolInterface.h"
#include "bcos-sealer/Sealer.h"
#include "bcos-sealer/SealerConfig.h"
#include "bcos-sealer/SealingManager.h"
#include "bcos-tars-protocol/protocol/BlockFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionMetaDataImpl.h"
#include "bcos-tool/NodeTimeMaintenance.h"
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-utilities/Error.h>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <utility>

namespace bcos::test
{
namespace
{
// Stub txpool: every callback path is a no-op. FIB-161 exercises only
// generateProposal(), which doesn't touch the txpool directly.
struct StubTxPool : public txpool::TxPoolInterface
{
    std::tuple<std::vector<bcos::protocol::TransactionMetaData::Ptr>,
        std::vector<bcos::protocol::TransactionMetaData::Ptr>>
    sealTxs(uint64_t /*_txsLimit*/) override
    {
        return {};
    }
    void tryToSyncTxsFromPeers() override {}
    void start() override {}
    void stop() override {}
    void asyncMarkTxs(const bcos::crypto::HashList&, bool, bcos::protocol::BlockNumber,
        bcos::crypto::HashType const&, std::function<void(Error::Ptr)>) override
    {}
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

// Friend fixture: gives the test direct write access to SealingManager's
// private pending queues / maxTxsPerBlock / sealing window so we can stage
// precise pre-state without exercising the fetch pipeline.
struct FIB161Fixture
{
    FIB161Fixture()
    {
        auto hashImpl = std::make_shared<crypto::Keccak256>();
        auto signatureImpl = std::make_shared<crypto::Secp256k1Crypto>();
        auto cryptoSuite = std::make_shared<crypto::CryptoSuite>(hashImpl, signatureImpl, nullptr);
        auto blockHeaderFactory =
            std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite);
        blockFactory = std::make_shared<bcostars::protocol::BlockFactoryImpl>(
            cryptoSuite, blockHeaderFactory, nullptr, nullptr);
        txpool = std::make_shared<StubTxPool>();
        auto timeMaintenance = std::make_shared<bcos::tool::NodeTimeMaintenance>();
        config = std::make_shared<sealer::SealerConfig>(blockFactory, txpool, timeMaintenance);
        sm = std::make_shared<sealer::SealingManager>(config);
    }

    // Stage one tx per slot, using a sequence number to give each a unique hash.
    bcos::protocol::TransactionMetaData::Ptr makeMeta(uint8_t seed) const
    {
        bcos::crypto::HashType h;
        // distinct hashes for distinct seeds
        h.data()[0] = seed;
        return blockFactory->createTransactionMetaData(h, std::string{"to"});
    }

    void stagePending(size_t normalCount, size_t sysCount, size_t maxTxsPerBlock)
    {
        // Direct friend writes — no locking needed: nothing else touches this SM yet.
        for (size_t i = 0; i < normalCount; ++i)
        {
            sm->m_pendingTxs.push_back(makeMeta(static_cast<uint8_t>(0x10 + i)));
        }
        for (size_t i = 0; i < sysCount; ++i)
        {
            sm->m_pendingSysTxs.push_back(makeMeta(static_cast<uint8_t>(0xA0 + i)));
        }
        // Force shouldGenerateProposal() to be true:
        //   m_startSealingNumber <= m_sealingNumber <= m_endSealingNumber
        //   m_latestNumber >= m_waitUntil
        sm->m_startSealingNumber = 1;
        sm->m_endSealingNumber = 1000;
        sm->m_sealingNumber = 1;
        sm->m_latestNumber = 1;
        sm->m_waitUntil = 0;
        sm->m_maxTxsPerBlock = maxTxsPerBlock;
        // Force reachMinSealTimeCondition() to be true regardless of clock.
        sm->m_lastSealTime = 0;
    }

    size_t remainingSys() const { return sm->m_pendingSysTxs.size(); }
    size_t remainingNormal() const { return sm->m_pendingTxs.size(); }

    std::shared_ptr<bcostars::protocol::BlockFactoryImpl> blockFactory;
    std::shared_ptr<StubTxPool> txpool;
    sealer::SealerConfig::Ptr config;
    sealer::SealingManager::Ptr sm;
};

BOOST_FIXTURE_TEST_SUITE(FIB161SystemTxsSizeShrinkTest, FIB161Fixture)

// T1 — pure bug trigger: max=5 sys=5 normal=0, hook adds 1 sys tx.
// Pre-fix: block size = 6 (5 sys-tx-queue + 1 hook-injected).
// Post-fix: block size = 5.
BOOST_AUTO_TEST_CASE(t1_hook_injects_when_sys_queue_at_max)
{
    stagePending(/*normal=*/0, /*sys=*/5, /*max=*/5);
    auto hook = [bf = blockFactory](bcos::protocol::Block::Ptr block) -> uint16_t {
        bcos::crypto::HashType h;
        h.data()[0] = 0xFF;
        block->appendTransactionMetaData(bf->createTransactionMetaData(h, std::string{"hook"}));
        return sealer::Sealer::SealBlockResult::SUCCESS;
    };
    auto [emitted, block] = sm->generateProposal(hook);
    BOOST_REQUIRE(block);
    BOOST_CHECK_EQUAL(block->transactionsMetaDataSize(), 5U);
}

// T2 — mixed pre-state: sysQueue=3 + normal=5, max=5, hook adds 1.
// Pre-fix would over-append (1 hook + 3 sys + 2 normal = 6). Post-fix: 5.
BOOST_AUTO_TEST_CASE(t2_hook_with_mixed_pending)
{
    stagePending(/*normal=*/5, /*sys=*/3, /*max=*/5);
    auto hook = [bf = blockFactory](bcos::protocol::Block::Ptr block) -> uint16_t {
        bcos::crypto::HashType h;
        h.data()[0] = 0xFE;
        block->appendTransactionMetaData(bf->createTransactionMetaData(h, std::string{"hook"}));
        return sealer::Sealer::SealBlockResult::SUCCESS;
    };
    auto [emitted, block] = sm->generateProposal(hook);
    BOOST_REQUIRE(block);
    BOOST_CHECK_EQUAL(block->transactionsMetaDataSize(), 5U);
}

// T3 — max equals the sys cap (10): hook injects 1 while sysQueue is at the
// cap. Pre-fix: txsSize=10, systemTxsSize=10, hook decrements txsSize to 9
// but the for-loop still pulls 10 sys → block of size 11.
// Post-fix: systemTxsSize is collapsed to 9 → block of size 10.
BOOST_AUTO_TEST_CASE(t3_max_equals_sys_cap_with_hook)
{
    stagePending(/*normal=*/0, /*sys=*/10, /*max=*/10);
    auto hook = [bf = blockFactory](bcos::protocol::Block::Ptr block) -> uint16_t {
        bcos::crypto::HashType h;
        h.data()[0] = 0xFD;
        block->appendTransactionMetaData(bf->createTransactionMetaData(h, std::string{"hook"}));
        return sealer::Sealer::SealBlockResult::SUCCESS;
    };
    auto [emitted, block] = sm->generateProposal(hook);
    BOOST_REQUIRE(block);
    BOOST_CHECK_EQUAL(block->transactionsMetaDataSize(), 10U);
}

// T4 — no hook installed: sysQueue=5, max=5, the loops pull exactly 5.
BOOST_AUTO_TEST_CASE(t4_no_hook)
{
    stagePending(/*normal=*/0, /*sys=*/5, /*max=*/5);
    auto [emitted, block] = sm->generateProposal(nullptr);
    BOOST_REQUIRE(block);
    BOOST_CHECK_EQUAL(block->transactionsMetaDataSize(), 5U);
}

// T5 — hook short-circuits with WAIT_FOR_LATEST_BLOCK; emit must be false
// and no transactions pulled from the queues.
BOOST_AUTO_TEST_CASE(t5_hook_wait_for_latest_block)
{
    stagePending(/*normal=*/0, /*sys=*/5, /*max=*/5);
    auto hook = [](bcos::protocol::Block::Ptr) -> uint16_t {
        return sealer::Sealer::SealBlockResult::WAIT_FOR_LATEST_BLOCK;
    };
    auto [emitted, block] = sm->generateProposal(hook);
    BOOST_CHECK_EQUAL(emitted, false);
    BOOST_CHECK(!block);
    // No tx should have been popped from either queue.
    BOOST_CHECK_EQUAL(remainingSys(), 5U);
}

// T6 — hook injects, but sysQueue is empty: only normal txs feed the block.
// max=5, normal=5, hook adds 1 → 5 total.
BOOST_AUTO_TEST_CASE(t6_hook_with_normal_only)
{
    stagePending(/*normal=*/5, /*sys=*/0, /*max=*/5);
    auto hook = [bf = blockFactory](bcos::protocol::Block::Ptr block) -> uint16_t {
        bcos::crypto::HashType h;
        h.data()[0] = 0xFC;
        block->appendTransactionMetaData(bf->createTransactionMetaData(h, std::string{"hook"}));
        return sealer::Sealer::SealBlockResult::SUCCESS;
    };
    auto [emitted, block] = sm->generateProposal(hook);
    BOOST_REQUIRE(block);
    BOOST_CHECK_EQUAL(block->transactionsMetaDataSize(), 5U);
}

// T7 — large max, mixed pending, no hook. 10 sys (cap) + 90 normal = 100.
BOOST_AUTO_TEST_CASE(t7_large_max_no_hook_caps_sys)
{
    stagePending(/*normal=*/200, /*sys=*/20, /*max=*/100);
    auto [emitted, block] = sm->generateProposal(nullptr);
    BOOST_REQUIRE(block);
    BOOST_CHECK_EQUAL(block->transactionsMetaDataSize(), 100U);
    // Sanity-check: 10 of the 20 sys txs were consumed (the cap), and 90 of
    // the 200 normal txs were consumed.
    BOOST_CHECK_EQUAL(remainingSys(), 10U);
    BOOST_CHECK_EQUAL(remainingNormal(), 110U);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
