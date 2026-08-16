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
#include <bcos-utilities/IOServicePool.h>
#include <bcos-tool/NodeTimeMaintenance.h>
#include <boost/asio/io_context.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <memory>
#include <stdexcept>
#include <thread>

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

// SealingManager that reports ready-to-seal so shouldGenerateProposal() returns
// true; tracks resetSealing() invocations to verify the FIB-152 catch path.
// Note: meetsProposalPreconditions() reads the m_pendingTxs/m_pendingSysTxs
// deques directly (not the virtual pendingTxsSize()), so the test must seed real
// metadata via testOnlySeedPendingTxs() — overriding pendingTxsSize() no longer
// influences the proposal-ready predicate after the FIB-117 refactor.
struct ReadyForProposalSealingManager : public bcos::sealer::SealingManager
{
    std::atomic<int> resetSealingCalls{0};

    explicit ReadyForProposalSealingManager(bcos::sealer::SealerConfig::Ptr cfg)
      : bcos::sealer::SealingManager(std::move(cfg))
    {}

    FetchResult fetchTransactions() override { return FetchResult::SUCCESS; }

    void resetSealing() override
    {
        ++resetSealingCalls;
        bcos::sealer::SealingManager::resetSealing();
    }
};

// SealingManager whose fetchTransactions() throws. Used to exercise the
// FIB-152 catch arm for the txpool-fetch path (the audit explicitly named
// transaction fetching as one of the unprotected fallible operations).
struct ThrowingFetchSealingManager : public bcos::sealer::SealingManager
{
    std::atomic<int> fetchInvocations{0};
    std::atomic<int> resetSealingCalls{0};

    explicit ThrowingFetchSealingManager(bcos::sealer::SealerConfig::Ptr cfg)
      : bcos::sealer::SealingManager(std::move(cfg))
    {}

    FetchResult fetchTransactions() override
    {
        ++fetchInvocations;
        throw std::runtime_error("FIB-152 simulated fetchTransactions failure");
    }

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

    explicit ThrowingHookSealer(bcos::sealer::SealerConfig::Ptr cfg,
        boost::asio::io_context& io)
      : bcos::sealer::Sealer(std::move(cfg), io)
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
    auto ioServicePool = std::make_shared<IOServicePool>(1, "fib152");
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
    // Establish a valid sealing window: [2, 1000] with maxTxsPerBlock=1.
    // start=2 (not endSealingNumber+1=1) triggers the non-continuous branch
    // which sets m_sealingNumber := startSealingNumber.
    mgr->resetSealingInfo(2, 1000, 1);
    // meetsProposalPreconditions() reads the pending-txs deque directly, so seed
    // one real metadata entry (>= maxTxsPerBlock=1, which skips the minSealTime
    // gate) to make the proposal-ready predicate true and drive execution into
    // the throwing hook.
    auto seedHash = hashImpl->hash(bcos::bytesConstRef("FIB-152-seed-tx"));
    mgr->testOnlySeedPendingTxs(
        {blockFactory->createTransactionMetaData(seedHash, seedHash.abridged())});

    auto sealer = std::make_shared<ThrowingHookSealer>(cfg, *ioServicePool->getIOService());
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

BOOST_AUTO_TEST_CASE(executeWorker_swallows_fetch_exception_and_resets_sealing)
{
    // Audit explicitly listed transaction fetching as an unprotected fallible
    // operation in executeWorker(). With the FIB-152 try block extended to
    // wrap the fetch path, a throw from fetchTransactions() must be contained
    // and the worker must remain ready for the next iteration.
    boost::asio::io_context ioContext;
    auto work = boost::asio::make_work_guard(ioContext);
    std::thread ioThread([&]() { ioContext.run(); });

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

    auto mgr = std::make_shared<ThrowingFetchSealingManager>(cfg);
    auto sealer = std::make_shared<bcos::sealer::Sealer>(cfg, ioContext);
    sealer->setSealingManager(mgr);
    sealer->setFetchTimeout(60);

    BOOST_CHECK_NO_THROW(sealer->executeWorker());
    BOOST_CHECK_GE(mgr->fetchInvocations.load(), 1);
    BOOST_CHECK_GE(mgr->resetSealingCalls.load(), 1);

    // Worker must survive a second iteration even though fetch keeps throwing.
    BOOST_CHECK_NO_THROW(sealer->executeWorker());
    BOOST_CHECK_GE(mgr->fetchInvocations.load(), 2);

    // Manually reset sealer while io_context is alive
    sealer.reset();
    // Stop io_context after sealer is destroyed
    work.reset();
    ioContext.stop();
    ioThread.join();
}

BOOST_AUTO_TEST_CASE(executeWorker_normal_path_does_not_throw)
{
    // Smoke check on the no-proposal path: with no pending txs and a
    // SealingManager that returns NO_TRANSACTION, executeWorker must not throw
    // (the try/catch wrapper does not change the happy-path behavior).
    boost::asio::io_context ioContext;
    auto work = boost::asio::make_work_guard(ioContext);
    std::thread ioThread([&]() { ioContext.run(); });

    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    auto signatureImpl = std::make_shared<bcos::crypto::Secp256k1Crypto>();
    auto cryptoSuite =
        std::make_shared<bcos::crypto::CryptoSuite>(hashImpl, signatureImpl, nullptr);
    auto blockFactory = std::make_shared<bcostars::protocol::BlockFactoryImpl>(
        cryptoSuite, nullptr, nullptr, nullptr);
    auto txpool = std::make_shared<StubTxPoolForFIB152>();
    auto cfg = std::make_shared<bcos::sealer::SealerConfig>(blockFactory, txpool, nullptr);

    auto sealer = std::make_shared<bcos::sealer::Sealer>(cfg, ioContext);
    sealer->setSealingManager(std::make_shared<bcos::sealer::SealingManager>(cfg));
    sealer->setFetchTimeout(60);

    BOOST_CHECK_NO_THROW(sealer->executeWorker());
    BOOST_CHECK_NO_THROW(sealer->executeWorker());

    // Manually reset sealer while io_context is alive to allow clean
    // timer cancellation, then stop the io_context.
    sealer.reset();
    work.reset();
    ioContext.stop();
    ioThread.join();
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
