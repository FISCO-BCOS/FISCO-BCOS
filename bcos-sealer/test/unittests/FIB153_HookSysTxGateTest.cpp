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
 * @file FIB153_HookSysTxGateTest.cpp
 * @brief CertiK FIB-153 regression: when _handleBlockHook synthesises a
 *        system transaction into the block (e.g. VRF rotation), the sealer
 *        must advance m_waitUntil so the next proposal honours commit-order
 *        gating. The pre-hook branch in generateProposal only sets
 *        m_waitUntil when m_pendingSysTxs is non-empty, so hook-only sys-tx
 *        injections used to slip through silently.
 */
#include "bcos-crypto/bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/txpool/TxPoolInterface.h"
#include "bcos-sealer/Sealer.h"
#include "bcos-sealer/SealingManager.h"
#include "bcos-tars-protocol/protocol/BlockFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionMetaDataImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-tool/NodeTimeMaintenance.h"
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::sealer;

namespace bcos::test
{
namespace
{
struct FIB153MockTxPool : public bcos::txpool::TxPoolInterface
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

// Subclass that forces the inside-lock predicate to pass so the test can
// deterministically reach the hook block regardless of m_waitUntil /
// m_latestNumber state. shouldGenerateProposal is also overridden for
// completeness, though after FIB-117's option-C refactor generateProposal()
// only consults meetsProposalPreconditions() under the write lock.
class HookGateSealingManager : public sealer::SealingManager
{
public:
    using sealer::SealingManager::SealingManager;
    bool shouldGenerateProposal() override { return true; }

protected:
    bool meetsProposalPreconditions() const override { return true; }
};

struct FIB153Fixture
{
    FIB153Fixture()
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
        txpool = std::make_shared<FIB153MockTxPool>();
        nodeTimeMaintenance = std::make_shared<bcos::tool::NodeTimeMaintenance>();
        sealerConfig =
            std::make_shared<sealer::SealerConfig>(blockFactory, txpool, nodeTimeMaintenance);
    }
    ~FIB153Fixture() { boost::log::core::get()->set_logging_enabled(true); }

    crypto::CryptoSuite::Ptr cryptoSuite;
    protocol::BlockFactory::Ptr blockFactory;
    std::shared_ptr<FIB153MockTxPool> txpool;
    bcos::tool::NodeTimeMaintenance::Ptr nodeTimeMaintenance;
    sealer::SealerConfig::Ptr sealerConfig;
};

inline std::vector<bcos::protocol::TransactionMetaData::Ptr> makeMetaDataBatch(size_t count)
{
    std::vector<bcos::protocol::TransactionMetaData::Ptr> batch;
    batch.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        bcos::crypto::HashType h;
        h[0] = static_cast<bcos::byte>((i + 1) & 0xff);
        auto md =
            std::make_shared<bcostars::protocol::TransactionMetaDataImpl>(h, std::string{"to"});
        batch.emplace_back(std::move(md));
    }
    return batch;
}

inline std::shared_ptr<HookGateSealingManager> makeHookGateManager(sealer::SealerConfig::Ptr config)
{
    auto mgr = std::make_shared<HookGateSealingManager>(std::move(config));
    mgr->resetSealingInfo(/*start*/ 2, /*end*/ 1'000'000, /*maxTxsPerBlock*/ 1);
    // Seed the user pending queue so the locked predicate (added by FIB-117)
    // sees txsSize >= m_maxTxsPerBlock and lets execution proceed to the
    // hook gate we want to test. Without this seed the FIB-117 inner re-check
    // would short-circuit before the gate is reached.
    auto seed = makeMetaDataBatch(4);
    mgr->testOnlySeedPendingTxs(seed);
    return mgr;
}
}  // namespace

BOOST_FIXTURE_TEST_SUITE(FIB153_HookSysTxWaitUntil, FIB153Fixture)

// FIB-153 (original-finding scenario): when m_pendingSysTxs is empty and the
// hook itself appends a sys-tx to the block, the sealer must advance
// m_waitUntil so the next proposal cannot bypass commit-order gating. The
// pre-hook branch that sets m_waitUntil only fires for the
// m_pendingSysTxs-non-empty case; without this post-hook update the
// hook-injected sys-tx slips through silently — exactly the invariant
// violation the finding describes.
BOOST_AUTO_TEST_CASE(hook_added_sys_tx_advances_waitUntil)
{
    auto mgr = makeHookGateManager(sealerConfig);

    // Gate disengaged: m_waitUntil starts below m_latestNumber so the hook
    // is allowed to run. m_pendingSysTxs is empty (fixture only seeds
    // m_pendingTxs), matching the finding's scenario.
    mgr->setWaitUntilForTest(0);
    mgr->resetLatestNumber(50);
    BOOST_REQUIRE_EQUAL(mgr->getWaitUntilForTest(), 0);

    // Hook synthesises a sys-tx and appends it directly to the block — same
    // shape as VRFBasedSealer::generateTransactionForRotating.
    auto syntheticSysTx = makeMetaDataBatch(1).front();
    int hookCalls = 0;
    auto [containsSysTxs, block] =
        mgr->generateProposal([&](bcos::protocol::Block::Ptr _block) -> uint16_t {
            ++hookCalls;
            _block->appendTransactionMetaData(syntheticSysTx);
            return Sealer::SealBlockResult::SUCCESS;
        });

    BOOST_REQUIRE(block);
    BOOST_CHECK_EQUAL(hookCalls, 1);
    BOOST_CHECK(containsSysTxs);

    // resetSealingInfo(start=2,...) + resetLatestNumber(50) means
    // m_sealingNumber = std::max(2, 51) = 51 inside generateProposal, then
    // the block header is set to 51 before the trailing ++m_sealingNumber.
    BOOST_REQUIRE_EQUAL(block->blockHeader()->number(), 51);
    // Post-fix: m_waitUntil was advanced to the sealing number (51) inside
    // the hook SUCCESS branch. Pre-fix: m_waitUntil remains 0 and the next
    // proposal would seal without waiting for block 51 to commit.
    BOOST_CHECK_EQUAL(mgr->getWaitUntilForTest(), 51);
}

// Companion case: hook returns SUCCESS but appends nothing. m_pendingSysTxs
// is still empty, so neither the pre-hook branch nor the new post-hook
// update should fire — m_waitUntil must remain at its pre-call value.
BOOST_AUTO_TEST_CASE(hook_success_without_appended_tx_leaves_waitUntil_unchanged)
{
    auto mgr = makeHookGateManager(sealerConfig);
    mgr->setWaitUntilForTest(7);
    mgr->resetLatestNumber(50);

    int hookCalls = 0;
    auto [containsSysTxs, block] =
        mgr->generateProposal([&](bcos::protocol::Block::Ptr) -> uint16_t {
            ++hookCalls;
            return Sealer::SealBlockResult::SUCCESS;
        });

    BOOST_REQUIRE(block);
    BOOST_CHECK_EQUAL(hookCalls, 1);
    BOOST_CHECK(!containsSysTxs);
    BOOST_CHECK_EQUAL(mgr->getWaitUntilForTest(), 7);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
