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
 * @file FIB117_GenerateProposalRaceTest.cpp
 * @brief CertiK FIB-117 regression: generateProposal must evaluate its
 *        preconditions under x_pendingTxs's write lock so a concurrent drain
 *        cannot leave the lock body executing on a stale snapshot. After the
 *        option-C refactor the race is structurally impossible — the write
 *        lock is acquired before the predicate is evaluated — so these tests
 *        verify (1) the inside-lock check rejects a drained queue, and
 *        (2) concurrent mutators never coax an empty proposal out under
 *        load.
 */
#include "bcos-crypto/bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/txpool/TxPoolInterface.h"
#include "bcos-sealer/SealingManager.h"
#include "bcos-tars-protocol/protocol/BlockFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionMetaDataImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-tool/NodeTimeMaintenance.h"
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <thread>

using namespace bcos;
using namespace bcos::sealer;

namespace bcos::test
{
namespace
{
struct FIB117MockTxPool : public bcos::txpool::TxPoolInterface
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
        bcos::crypto::HashType const&, std::function<void(Error::Ptr)> _onRecvResponse) override
    {
        if (_onRecvResponse)
        {
            _onRecvResponse(nullptr);
        }
    }
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

struct FIB117Fixture
{
    FIB117Fixture()
    {
        boost::log::core::get()->set_logging_enabled(false);
        auto hashImpl = std::make_shared<crypto::Keccak256>();
        auto signatureImpl = std::make_shared<crypto::Secp256k1Crypto>();
        cryptoSuite = std::make_shared<crypto::CryptoSuite>(hashImpl, signatureImpl, nullptr);
        auto blockHeaderFactory =
            std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite);
        auto transactionFactory =
            std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite);
        auto receiptFactory =
            std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite);
        blockFactory = std::make_shared<bcostars::protocol::BlockFactoryImpl>(
            cryptoSuite, blockHeaderFactory, transactionFactory, receiptFactory);
        txpool = std::make_shared<FIB117MockTxPool>();
        nodeTimeMaintenance = std::make_shared<bcos::tool::NodeTimeMaintenance>();
        sealerConfig =
            std::make_shared<sealer::SealerConfig>(blockFactory, txpool, nodeTimeMaintenance);
    }
    ~FIB117Fixture() { boost::log::core::get()->set_logging_enabled(true); }

    crypto::CryptoSuite::Ptr cryptoSuite;
    protocol::BlockFactory::Ptr blockFactory;
    std::shared_ptr<FIB117MockTxPool> txpool;
    bcos::tool::NodeTimeMaintenance::Ptr nodeTimeMaintenance;
    sealer::SealerConfig::Ptr sealerConfig;
};

inline std::shared_ptr<sealer::SealingManager> makePrimedSealingManager(
    sealer::SealerConfig::Ptr config)
{
    auto mgr = std::make_shared<sealer::SealingManager>(std::move(config));
    // First call seeds m_endSealingNumber but, because the initial m_sealingNumber is -1,
    // the inner branch of resetSealingInfo (which actually advances m_sealingNumber) is
    // not taken. A second call with start=current_end+1 also leaves the inner branch
    // un-taken, so we use a "discontinuous" start to force m_sealingNumber to be primed.
    mgr->resetSealingInfo(/*start*/ 2, /*end*/ 1'000'000, /*maxTxsPerBlock*/ 1);
    mgr->resetLatestNumber(0);
    return mgr;
}

inline std::vector<bcos::protocol::TransactionMetaData::Ptr> makeMetaDataBatch(size_t count)
{
    std::vector<bcos::protocol::TransactionMetaData::Ptr> batch;
    batch.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        bcos::crypto::HashType h;
        // deterministic, non-zero hash; first two bytes differ per index
        h[0] = static_cast<bcos::byte>((i + 1) & 0xff);
        h[1] = static_cast<bcos::byte>(((i + 1) >> 8) & 0xff);
        auto md =
            std::make_shared<bcostars::protocol::TransactionMetaDataImpl>(h, std::string{"to"});
        batch.emplace_back(std::move(md));
    }
    return batch;
}
}  // namespace

BOOST_FIXTURE_TEST_SUITE(FIB117_GenerateProposalRace, FIB117Fixture)

// FIB-117 happy path: a freshly seeded queue and no concurrent mutator
// produces a block carrying the expected txs. Establishes the baseline so
// the negative-case asserts below are meaningful.
BOOST_AUTO_TEST_CASE(seeded_queue_produces_block)
{
    auto mgr = makePrimedSealingManager(sealerConfig);
    auto seed = makeMetaDataBatch(8);
    mgr->testOnlySeedPendingTxs(seed);
    auto [containsSysTxs, block] = mgr->generateProposal({});
    BOOST_REQUIRE(block);
    BOOST_CHECK_EQUAL(block->transactionsMetaDataSize(), 1);  // fixture caps at 1
}

// Core FIB-117 invariant: a drain that completes BEFORE generateProposal
// acquires its write lock must not result in an empty proposal. Pre-fix
// generateProposal trusted the outer unlocked predicate and would package
// an empty block; post-fix the inside-lock meetsProposalPreconditions()
// check returns false and the call returns {false, nullptr}.
//
// We don't need thread orchestration: a sequential
// seed → outer-predicate-passes → drain → call sequence reproduces the
// exact state generateProposal would observe if the race had occurred.
BOOST_AUTO_TEST_CASE(drain_before_generateProposal_returns_nullptr)
{
    auto mgr = makePrimedSealingManager(sealerConfig);
    auto seed = makeMetaDataBatch(5);
    mgr->testOnlySeedPendingTxs(seed);
    BOOST_REQUIRE(mgr->shouldGenerateProposal());  // outer check would greenlight

    // Simulated race winner: another path drained the queue between the
    // outer check and generateProposal's WriteGuard acquisition.
    mgr->testOnlyDrainPendingTxs();

    auto [containsSysTxs, block] = mgr->generateProposal({});
    BOOST_CHECK(block == nullptr);
}

// Stress / belt-and-suspenders: under a continuous drain mutator running
// on a separate thread, generateProposal must never emit an empty block.
// Probabilistic — the deterministic guarantee comes from the structure
// (write lock serialises predicate + action), this test just guards
// against regressions in that structure.
BOOST_AUTO_TEST_CASE(concurrent_drain_never_yields_empty_proposal)
{
    auto mgr = makePrimedSealingManager(sealerConfig);
    auto seed = makeMetaDataBatch(5);

    std::atomic<bool> stop{false};
    std::thread racer([&] {
        while (!stop.load(std::memory_order_relaxed))
        {
            mgr->testOnlyDrainPendingTxs();
            std::this_thread::yield();
        }
    });

    constexpr int kIterations = 1000;
    int spuriousCount = 0;
    int okCount = 0;
    for (int i = 0; i < kIterations; ++i)
    {
        mgr->testOnlySeedPendingTxs(seed);
        auto [containsSysTxs, block] = mgr->generateProposal({});
        if (block != nullptr)
        {
            ++okCount;
            if (block->transactionsMetaDataSize() == 0 && block->transactionsSize() == 0)
            {
                ++spuriousCount;
            }
        }
    }
    stop.store(true);
    racer.join();

    BOOST_TEST_MESSAGE("FIB117: blocks=" << okCount << " spurious(empty)=" << spuriousCount);
    BOOST_CHECK_EQUAL(spuriousCount, 0);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
