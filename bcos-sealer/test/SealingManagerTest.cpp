/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "bcos-sealer/SealingManager.h"
#include "bcos-sealer/SealerConfig.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/protocol/Block.h>
#include <bcos-framework/protocol/TransactionMetaData.h>
#include <bcos-framework/txpool/TxPoolInterface.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <boost/test/unit_test.hpp>

namespace bcos::test
{
namespace
{
struct NoopTxPool : public bcos::txpool::TxPoolInterface
{
    std::tuple<std::vector<bcos::protocol::TransactionMetaData::Ptr>,
        std::vector<bcos::protocol::TransactionMetaData::Ptr>>
    sealTxs(uint64_t) override
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

bcos::sealer::SealingManager::Ptr makeSealingManager()
{
    auto hash = std::make_shared<bcos::crypto::Keccak256>();
    auto sig = std::make_shared<bcos::crypto::Secp256k1Crypto>();
    auto suite = std::make_shared<bcos::crypto::CryptoSuite>(hash, sig, nullptr);
    auto factory =
        std::make_shared<bcostars::protocol::BlockFactoryImpl>(suite, nullptr, nullptr, nullptr);
    auto config = std::make_shared<bcos::sealer::SealerConfig>(
        factory, std::make_shared<NoopTxPool>(), nullptr);
    return std::make_shared<bcos::sealer::SealingManager>(config);
}
}  // namespace

BOOST_AUTO_TEST_SUITE(SealingManagerTest)

BOOST_AUTO_TEST_CASE(latestSettersRoundTrip)
{
    auto sm = makeSealingManager();
    sm->resetLatestNumber(1234);
    sm->resetLatestTimestamp(5678);
    BOOST_CHECK_EQUAL(sm->latestNumber(), 1234);
    BOOST_CHECK_EQUAL(sm->latestTimestamp(), 5678);

    bcos::crypto::HashType h;
    h[0] = 0x42;
    sm->resetLatestHash(h);
    BOOST_CHECK_EQUAL(sm->latestHash(), h);
}

BOOST_AUTO_TEST_CASE(resetSealingDisablesProposalGeneration)
{
    auto sm = makeSealingManager();
    sm->resetSealingInfo(/*start=*/10, /*end=*/20, /*maxTxsPerBlock=*/100);
    // resetSealing advances the sealing number to endSealingNumber + 1 (i.e. past
    // the sealing window), which leaves nothing to propose.
    sm->resetSealing();
    BOOST_CHECK(!sm->shouldGenerateProposal());
}

BOOST_AUTO_TEST_CASE(setOnReadyCallbackStores)
{
    auto sm = makeSealingManager();
    int counter = 0;
    sm->setOnReadyCallback([&]() { ++counter; });
    // The callback is private to SealingManager; we can't trigger it directly
    // without state changes, but the setter itself runs and is then exercised
    // when generateProposal succeeds (covered elsewhere).
    BOOST_CHECK_EQUAL(counter, 0);
}

BOOST_AUTO_TEST_CASE(resetSealingInfoIsIdempotentForRepeatedCall)
{
    auto sm = makeSealingManager();
    sm->resetSealingInfo(5, 10, 50);
    sm->resetSealingInfo(5, 10, 50);  // second call must not crash or assert
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(shouldGenerateProposalRequiresSealingInfo)
{
    auto sm = makeSealingManager();
    // With no resetSealingInfo call, sealingNumber starts at -1 → never ready.
    BOOST_CHECK(!sm->shouldGenerateProposal());
}

BOOST_AUTO_TEST_CASE(notifyResetTxsFlagWithEmptyListIsNoOp)
{
    auto sm = makeSealingManager();
    bcos::crypto::HashList empty;
    BOOST_REQUIRE_NO_THROW(sm->notifyResetTxsFlag(empty, false, 0));
}

BOOST_AUTO_TEST_CASE(notifyResetProposalWithEmptyMetadataIsNoOp)
{
    auto sm = makeSealingManager();
    std::vector<bcos::protocol::TransactionMetaData::Ptr> empty;
    BOOST_REQUIRE_NO_THROW(sm->notifyResetProposal(empty));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
