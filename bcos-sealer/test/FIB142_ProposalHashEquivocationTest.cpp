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
 * @file FIB142_ProposalHashEquivocationTest.cpp
 * @brief Regression test for FIB-142 (sender side):
 *        Sealer::submitProposal must bind transaction commitment (txsRoot)
 *        into the proposal hash before signing, otherwise a byzantine leader
 *        can equivocate under the same proposal hash.
 */
#include "bcos-crypto/bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/consensus/ConsensusInterface.h"
#include "bcos-framework/protocol/Block.h"
#include "bcos-framework/txpool/TxPoolInterface.h"
#include "bcos-sealer/Sealer.h"
#include "bcos-sealer/SealerConfig.h"
#include "bcos-sealer/SealingManager.h"
#include "bcos-tars-protocol/protocol/BlockFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h"
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <string>
#include <vector>

namespace bcos::test
{
namespace
{

struct StubTxPool : public bcos::txpool::TxPoolInterface
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
    void asyncMarkTxs(const bcos::crypto::HashList& /*_txsHash*/, bool /*_sealedFlag*/,
        bcos::protocol::BlockNumber /*_batchId*/, bcos::crypto::HashType const& /*_batchHash*/,
        std::function<void(Error::Ptr)> /*_onRecvResponse*/) override
    {}
    void asyncVerifyBlock(bcos::crypto::PublicPtr /*_generatedNodeID*/,
        bcos::protocol::Block::ConstPtr /*_block*/,
        std::function<void(Error::Ptr, bool)> /*_onVerifyFinished*/) override
    {}
    void asyncFillBlock(bcos::crypto::HashListPtr /*_txsHash*/,
        std::function<void(Error::Ptr, bcos::protocol::ConstTransactionsPtr)> /*_onBlockFilled*/)
        override
    {}
    void asyncNotifyBlockResult(bcos::protocol::BlockNumber /*_blockNumber*/,
        bcos::protocol::TransactionSubmitResultsPtr /*_txsResult*/,
        std::function<void(Error::Ptr)> /*_onNotifyFinished*/) override
    {}
    void asyncNotifyTxsSyncMessage(bcos::Error::Ptr /*_error*/, std::string const& /*_id*/,
        bcos::crypto::NodeIDPtr /*_nodeID*/, bcos::bytesConstRef /*_data*/,
        std::function<void(Error::Ptr _error)> /*_onRecv*/) override
    {}
    void notifyConsensusNodeList(bcos::consensus::ConsensusNodeList const& /*_consensusNodeList*/,
        std::function<void(Error::Ptr)> /*_onRecvResponse*/) override
    {}
    void notifyObserverNodeList(bcos::consensus::ConsensusNodeList const& /*_observerNodeList*/,
        std::function<void(Error::Ptr)> /*_onRecvResponse*/) override
    {}
    void asyncGetPendingTransactionSize(
        std::function<void(Error::Ptr, uint64_t)> /*_onGetTxsSize*/) override
    {}
    void asyncResetTxPool(std::function<void(Error::Ptr)> /*_onRecvResponse*/) override {}
    void notifyConnectedNodes(bcos::crypto::NodeIDSet const& /*_connectedNodes*/,
        std::function<void(Error::Ptr)> /*_onResponse*/) override
    {}
};

struct StubConsensus : public bcos::consensus::ConsensusInterface
{
    void start() override {}
    void stop() override {}

    void asyncSubmitProposal(bool /*_containSysTxs*/, const bcos::protocol::Block& /*proposal*/,
        bcos::protocol::BlockNumber /*_proposalIndex*/,
        bcos::crypto::HashType const& /*_proposalHash*/,
        std::function<void(Error::Ptr)> _onProposalSubmitted) override
    {
        if (_onProposalSubmitted)
        {
            _onProposalSubmitted(nullptr);
        }
    }
    void asyncGetPBFTView(std::function<void(Error::Ptr, bcos::consensus::ViewType)>) override {}
    void asyncCheckBlock(bcos::protocol::Block::Ptr /*_block*/,
        std::function<void(Error::Ptr, bool)> /*_onVerifyFinish*/) override
    {}
    void asyncNotifyNewBlock(bcos::ledger::LedgerConfig::Ptr /*_ledgerConfig*/,
        std::function<void(Error::Ptr)> /*_onRecv*/) override
    {}
    void asyncNotifyConsensusMessage(bcos::Error::Ptr /*_error*/, std::string const& /*_id*/,
        bcos::crypto::NodeIDPtr /*_nodeID*/, bcos::bytesConstRef /*_data*/,
        std::function<void(Error::Ptr)> /*_onRecv*/) override
    {}
    void notifyHighestSyncingNumber(bcos::protocol::BlockNumber /*_number*/) override {}
    void asyncGetConsensusStatus(
        std::function<void(Error::Ptr, std::string)> /*_onGetConsensusStatus*/) override
    {}
    void notifyConnectedNodes(bcos::crypto::NodeIDSet const& /*_connectedNodes*/,
        std::function<void(Error::Ptr)> /*_onResponse*/) override
    {}
};

// Test-only subclass that exposes the protected submitProposal entry point
// and bypasses SealingManager state checks, plus overrides latestNumber so
// the early-return at Sealer.cpp:164 does not skip the hashing path.
struct TestableSealer : public bcos::sealer::Sealer
{
    using bcos::sealer::Sealer::submitProposal;  // expose
    explicit TestableSealer(bcos::sealer::SealerConfig::Ptr cfg)
      : bcos::sealer::Sealer(std::move(cfg))
    {}
};

struct TestableSealingManager : public bcos::sealer::SealingManager
{
    explicit TestableSealingManager(bcos::sealer::SealerConfig::Ptr cfg)
      : bcos::sealer::SealingManager(std::move(cfg))
    {}
    int64_t latestNumber() const override { return -1; }
};

struct Fixture
{
    Fixture()
    {
        hashImpl = std::make_shared<bcos::crypto::Keccak256>();
        auto signatureImpl = std::make_shared<bcos::crypto::Secp256k1Crypto>();
        cryptoSuite = std::make_shared<bcos::crypto::CryptoSuite>(hashImpl, signatureImpl, nullptr);
        auto blockHeaderFactory =
            std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite);
        blockFactory = std::make_shared<bcostars::protocol::BlockFactoryImpl>(
            cryptoSuite, blockHeaderFactory, nullptr, nullptr);
        txpool = std::make_shared<StubTxPool>();
        consensus = std::make_shared<StubConsensus>();
        sealerConfig = std::make_shared<bcos::sealer::SealerConfig>(blockFactory, txpool, nullptr);
        sealerConfig->setConsensusInterface(consensus);
        sealer = std::make_shared<TestableSealer>(sealerConfig);
        sealer->setSealingManager(std::make_shared<TestableSealingManager>(sealerConfig));
    }

    bcos::protocol::Block::Ptr makeBlockWithHashes(
        int64_t blockNumber, std::vector<bcos::crypto::HashType> const& txHashes)
    {
        auto block = blockFactory->createBlock();
        auto header = blockFactory->blockHeaderFactory()->createBlockHeader();
        header->setNumber(blockNumber);
        header->setTimestamp(0);
        header->setSealer(0);
        block->setBlockHeader(header);
        for (auto const& h : txHashes)
        {
            auto md = blockFactory->createTransactionMetaData(h, "to");
            block->appendTransactionMetaData(md);
        }
        return block;
    }

    bcos::crypto::HashType makeHash(std::string const& s)
    {
        return hashImpl->hash(
            bcos::bytesConstRef{reinterpret_cast<const bcos::byte*>(s.data()), s.size()});
    }

    bcos::crypto::Hash::Ptr hashImpl;
    bcos::crypto::CryptoSuite::Ptr cryptoSuite;
    bcos::protocol::BlockFactory::Ptr blockFactory;
    std::shared_ptr<StubTxPool> txpool;
    std::shared_ptr<StubConsensus> consensus;
    bcos::sealer::SealerConfig::Ptr sealerConfig;
    std::shared_ptr<TestableSealer> sealer;
};

}  // namespace

BOOST_FIXTURE_TEST_SUITE(FIB142_ProposalHashEquivocation, Fixture)

BOOST_AUTO_TEST_CASE(submitProposal_must_set_txsRoot_before_hash)
{
    // Two blocks with identical header fields but different transaction sets.
    auto blockA = makeBlockWithHashes(1, {makeHash("txA1"), makeHash("txA2")});
    auto blockB = makeBlockWithHashes(1, {makeHash("txB1"), makeHash("txB2")});

    sealer->submitProposal(false, blockA);
    auto hashA = blockA->blockHeader()->hash();

    sealer->submitProposal(false, blockB);
    auto hashB = blockB->blockHeader()->hash();

    // Without FIB-142 sender-side fix, hashA == hashB (both txsRoot are zero
    // and the rest of the header fields match), enabling equivocation.
    BOOST_CHECK_NE(hashA, hashB);
}

BOOST_AUTO_TEST_CASE(reorder_tx_changes_hash)
{
    auto h1 = makeHash("tx-aaa");
    auto h2 = makeHash("tx-bbb");
    auto block1 = makeBlockWithHashes(2, {h1, h2});
    auto block2 = makeBlockWithHashes(2, {h2, h1});

    sealer->submitProposal(false, block1);
    sealer->submitProposal(false, block2);

    BOOST_CHECK_NE(block1->blockHeader()->hash(), block2->blockHeader()->hash());
}

BOOST_AUTO_TEST_CASE(empty_block_is_skipped_by_submitProposal)
{
    // Blocks with no transactions must be short-circuited (no submission, no hash binding).
    auto empty = makeBlockWithHashes(3, {});
    BOOST_CHECK_EQUAL(empty->transactionsHashSize(), 0u);
    BOOST_CHECK_NO_THROW(sealer->submitProposal(false, empty));
}

BOOST_AUTO_TEST_CASE(same_tx_set_yields_same_hash)
{
    auto h1 = makeHash("tx-x");
    auto h2 = makeHash("tx-y");
    auto block1 = makeBlockWithHashes(4, {h1, h2});
    auto block2 = makeBlockWithHashes(4, {h1, h2});

    sealer->submitProposal(false, block1);
    sealer->submitProposal(false, block2);

    BOOST_CHECK_EQUAL(block1->blockHeader()->hash(), block2->blockHeader()->hash());
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
