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
 * @file FIB151_SubmitProposalUnsealTest.cpp
 * @brief Regression test for FIB-151: when asyncSubmitProposal fails, the
 *        sealer must unseal the proposal's transactions back to the pool.
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
#include <atomic>
#include <memory>
#include <vector>

namespace bcos::test
{
namespace
{

struct UnsealRecordingTxPool : public bcos::txpool::TxPoolInterface
{
    std::atomic<bool> markTxsCalled{false};
    bcos::crypto::HashList lastHashes;
    bool lastSealedFlag{true};

    std::tuple<std::vector<bcos::protocol::TransactionMetaData::Ptr>,
        std::vector<bcos::protocol::TransactionMetaData::Ptr>>
    sealTxs(uint64_t /*_txsLimit*/) override
    {
        return {};
    }
    void tryToSyncTxsFromPeers() override {}
    void start() override {}
    void stop() override {}
    void asyncMarkTxs(const bcos::crypto::HashList& _txsHash, bool _sealedFlag,
        bcos::protocol::BlockNumber /*_batchId*/, bcos::crypto::HashType const& /*_batchHash*/,
        std::function<void(Error::Ptr)> _onRecvResponse) override
    {
        lastHashes = _txsHash;
        lastSealedFlag = _sealedFlag;
        markTxsCalled = true;
        if (_onRecvResponse)
        {
            _onRecvResponse(nullptr);
        }
    }
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

struct FailingConsensus : public bcos::consensus::ConsensusInterface
{
    bool failNext = true;

    void start() override {}
    void stop() override {}
    void asyncSubmitProposal(bool /*_containSysTxs*/, const bcos::protocol::Block& /*proposal*/,
        bcos::protocol::BlockNumber /*_proposalIndex*/,
        bcos::crypto::HashType const& /*_proposalHash*/,
        std::function<void(Error::Ptr)> _onProposalSubmitted) override
    {
        if (_onProposalSubmitted)
        {
            if (failNext)
            {
                _onProposalSubmitted(BCOS_ERROR_PTR(-1, "submit failure for FIB-151"));
            }
            else
            {
                _onProposalSubmitted(nullptr);
            }
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

struct FIB151TestableSealer : public bcos::sealer::Sealer
{
    using bcos::sealer::Sealer::submitProposal;  // expose
    explicit FIB151TestableSealer(bcos::sealer::SealerConfig::Ptr cfg)
      : bcos::sealer::Sealer(std::move(cfg))
    {}
};

struct FIB151TestableSealingManager : public bcos::sealer::SealingManager
{
    explicit FIB151TestableSealingManager(bcos::sealer::SealerConfig::Ptr cfg)
      : bcos::sealer::SealingManager(std::move(cfg))
    {}
    int64_t latestNumber() const override { return -1; }
};

struct FIB151Fixture
{
    FIB151Fixture()
    {
        hashImpl = std::make_shared<bcos::crypto::Keccak256>();
        auto signatureImpl = std::make_shared<bcos::crypto::Secp256k1Crypto>();
        cryptoSuite = std::make_shared<bcos::crypto::CryptoSuite>(hashImpl, signatureImpl, nullptr);
        auto blockHeaderFactory =
            std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite);
        blockFactory = std::make_shared<bcostars::protocol::BlockFactoryImpl>(
            cryptoSuite, blockHeaderFactory, nullptr, nullptr);
        txpool = std::make_shared<UnsealRecordingTxPool>();
        consensus = std::make_shared<FailingConsensus>();
        sealerConfig = std::make_shared<bcos::sealer::SealerConfig>(blockFactory, txpool, nullptr);
        sealerConfig->setConsensusInterface(consensus);
        sealer = std::make_shared<FIB151TestableSealer>(sealerConfig);
        sealer->setSealingManager(std::make_shared<FIB151TestableSealingManager>(sealerConfig));
    }

    bcos::protocol::Block::Ptr makeBlock(
        int64_t blockNumber, std::vector<bcos::crypto::HashType> const& txHashes)
    {
        auto block = blockFactory->createBlock();
        auto header = blockFactory->blockHeaderFactory()->createBlockHeader();
        header->setNumber(blockNumber);
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
    std::shared_ptr<UnsealRecordingTxPool> txpool;
    std::shared_ptr<FailingConsensus> consensus;
    bcos::sealer::SealerConfig::Ptr sealerConfig;
    std::shared_ptr<FIB151TestableSealer> sealer;
};

}  // namespace

BOOST_FIXTURE_TEST_SUITE(FIB151_SubmitProposalUnseal, FIB151Fixture)

BOOST_AUTO_TEST_CASE(submitProposal_failure_returns_txs_to_pool)
{
    auto txA = makeHash("tx-a");
    auto txB = makeHash("tx-b");
    auto block = makeBlock(1, {txA, txB});

    consensus->failNext = true;
    sealer->submitProposal(false, block);

    BOOST_REQUIRE(txpool->markTxsCalled.load());
    BOOST_CHECK_EQUAL(txpool->lastSealedFlag, false);
    BOOST_REQUIRE_EQUAL(txpool->lastHashes.size(), 2u);
    BOOST_CHECK_EQUAL(txpool->lastHashes[0], txA);
    BOOST_CHECK_EQUAL(txpool->lastHashes[1], txB);
}

BOOST_AUTO_TEST_CASE(submitProposal_success_does_not_unseal)
{
    auto txA = makeHash("tx-aa");
    auto block = makeBlock(2, {txA});

    consensus->failNext = false;
    sealer->submitProposal(false, block);

    BOOST_CHECK(!txpool->markTxsCalled.load());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
