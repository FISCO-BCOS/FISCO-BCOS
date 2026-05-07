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
 * @file FIB152_ExecuteWorkerExceptionTest.cpp
 * @brief Regression test for FIB-152: Sealer::executeWorker must contain
 *        exceptions raised by generateProposal / hookWhenSealBlock /
 *        submitProposal so the worker loop survives a single failing
 *        iteration without aborting the worker thread.
 */
#include "bcos-crypto/bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/protocol/Block.h"
#include "bcos-framework/txpool/TxPoolInterface.h"
#include "bcos-sealer/Sealer.h"
#include "bcos-sealer/SealerConfig.h"
#include "bcos-sealer/SealingManager.h"
#include "bcos-tars-protocol/protocol/BlockFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h"
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-tool/NodeTimeMaintenance.h>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <memory>
#include <stdexcept>

namespace bcos::test
{
namespace
{

struct StubTxPoolForFIB152 : public bcos::txpool::TxPoolInterface
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
        bcos::bytesConstRef, std::function<void(Error::Ptr _error)>) override
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

// SealingManager whose pendingTxsSize() reports >= maxTxsPerBlock so that
// shouldGenerateProposal() returns true; tracks resetSealing() invocations
// to verify the FIB-152 catch path.
struct ReadyForProposalSealingManager : public bcos::sealer::SealingManager
{
    std::atomic<int> resetSealingCalls{0};

    explicit ReadyForProposalSealingManager(bcos::sealer::SealerConfig::Ptr cfg)
      : bcos::sealer::SealingManager(std::move(cfg))
    {}

    FetchResult fetchTransactions() override { return FetchResult::SUCCESS; }
    size_t pendingTxsSize() override { return 1024; }
    int64_t latestNumber() const override { return 0; }

    void resetSealing() override
    {
        ++resetSealingCalls;
        bcos::sealer::SealingManager::resetSealing();
    }
};

// Sealer subclass whose hookWhenSealBlock throws. The hook runs inside
// SealingManager::generateProposal, so the throw escapes back into
// Sealer::executeWorker and exercises the FIB-152 try/catch boundary.
struct ThrowingHookSealer : public bcos::sealer::Sealer
{
    std::atomic<int> hookInvocations{0};

    explicit ThrowingHookSealer(bcos::sealer::SealerConfig::Ptr cfg)
      : bcos::sealer::Sealer(std::move(cfg))
    {}

    uint16_t hookWhenSealBlock(bcos::protocol::Block::Ptr /*_block*/) override
    {
        ++hookInvocations;
        throw std::runtime_error("FIB-152 simulated hook failure");
    }
};

}  // namespace

BOOST_AUTO_TEST_SUITE(FIB152_ExecuteWorkerException)

BOOST_AUTO_TEST_CASE(executeWorker_swallows_hook_exception_and_resets_sealing)
{
    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    auto signatureImpl = std::make_shared<bcos::crypto::Secp256k1Crypto>();
    auto cryptoSuite =
        std::make_shared<bcos::crypto::CryptoSuite>(hashImpl, signatureImpl, nullptr);
    auto blockHeaderFactory =
        std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite);
    auto blockFactory = std::make_shared<bcostars::protocol::BlockFactoryImpl>(
        cryptoSuite, blockHeaderFactory, nullptr, nullptr);
    auto txpool = std::make_shared<StubTxPoolForFIB152>();
    auto nodeTime = std::make_shared<bcos::tool::NodeTimeMaintenance>();
    auto cfg = std::make_shared<bcos::sealer::SealerConfig>(blockFactory, txpool, nodeTime);

    auto mgr = std::make_shared<ReadyForProposalSealingManager>(cfg);
    // Establish a valid sealing window: [2, 1000] with maxTxsPerBlock=1, so
    // shouldGenerateProposal() returns true (pendingTxsSize() >= maxTxsPerBlock).
    // start=2 (not endSealingNumber+1=1) triggers the non-continuous branch
    // which sets m_sealingNumber := startSealingNumber.
    mgr->resetSealingInfo(2, 1000, 1);

    auto sealer = std::make_shared<ThrowingHookSealer>(cfg);
    sealer->setSealingManager(mgr);
    sealer->setFetchTimeout(60);  // do not trigger the syncTxs branch

    // Without FIB-152, the throw inside hookWhenSealBlock would propagate out
    // of executeWorker and abort the worker thread. With the fix, executeWorker
    // contains the exception, calls resetSealing(), and returns normally.
    BOOST_CHECK_NO_THROW(sealer->executeWorker());
    BOOST_CHECK_GE(sealer->hookInvocations.load(), 1);
    BOOST_CHECK_GE(mgr->resetSealingCalls.load(), 1);

    // Second iteration must still execute (worker loop did not die).
    BOOST_CHECK_NO_THROW(sealer->executeWorker());
}

BOOST_AUTO_TEST_CASE(executeWorker_normal_path_does_not_throw)
{
    // Smoke check on the no-proposal path: with no pending txs and a
    // SealingManager that returns NO_TRANSACTION, executeWorker must not throw
    // (the try/catch wrapper does not change the happy-path behavior).
    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    auto signatureImpl = std::make_shared<bcos::crypto::Secp256k1Crypto>();
    auto cryptoSuite =
        std::make_shared<bcos::crypto::CryptoSuite>(hashImpl, signatureImpl, nullptr);
    auto blockFactory = std::make_shared<bcostars::protocol::BlockFactoryImpl>(
        cryptoSuite, nullptr, nullptr, nullptr);
    auto txpool = std::make_shared<StubTxPoolForFIB152>();
    auto cfg = std::make_shared<bcos::sealer::SealerConfig>(blockFactory, txpool, nullptr);

    auto sealer = std::make_shared<bcos::sealer::Sealer>(cfg);
    sealer->setSealingManager(std::make_shared<bcos::sealer::SealingManager>(cfg));
    sealer->setFetchTimeout(60);

    BOOST_CHECK_NO_THROW(sealer->executeWorker());
    BOOST_CHECK_NO_THROW(sealer->executeWorker());
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
