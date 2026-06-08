/**
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief Regression test for FIB-124: NewView prePrepareList items must be cross-checked
 *        against the bundled viewChange evidence. A prePrepare whose hash does not match
 *        the highest-view prepared proposal in the viewChange evidence must be rejected.
 * @file FIB124_NewViewPrePrepareCrosscheckTest.cpp
 */
#include "bcos-framework/bcos-framework/testutils/faker/FakeBlock.h"
#include "bcos-framework/bcos-framework/testutils/faker/FakeBlockHeader.h"
#include "test/unittests/pbft/PBFTFixture.h"
#include "test/unittests/protocol/FakePBFTMessage.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-pbft/pbft/protocol/PB/PBFTMessage.h>
#include <bcos-pbft/pbft/protocol/PB/PBFTNewViewMsg.h>
#include <bcos-pbft/pbft/protocol/PB/PBFTProposal.h>
#include <bcos-pbft/pbft/protocol/PB/PBFTViewChangeMsg.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::crypto;

namespace bcos
{
namespace test
{

// A thin subclass of FakePBFTEngine that exposes the protected isValidNewViewMsg for testing.
class TestPBFTEngine : public FakePBFTEngine
{
public:
    using Ptr = std::shared_ptr<TestPBFTEngine>;
    explicit TestPBFTEngine(PBFTConfig::Ptr _config) : FakePBFTEngine(_config) {}
    bool testIsValidNewViewMsg(std::shared_ptr<NewViewMsgInterface> _msg)
    {
        return PBFTEngine::isValidNewViewMsg(_msg);
    }
};

BOOST_FIXTURE_TEST_SUITE(FIB124NewViewPrePrepareCrosscheckTest, TestPromptFixture)

// FIB-124: A NewView message whose prePrepare for an index WITH viewChange evidence carries
// a DIFFERENT hash must be rejected.  We build the viewChange evidence with the real keypairs
// from the fake consensus cluster so that viewChange signature verification passes, then
// inject a prePrepare with a tampered hash and verify rejection.
BOOST_AUTO_TEST_CASE(testRejectNewViewWithTamperedPrePrepareHash)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);

    size_t consensusNodeSize = 4;
    size_t currentBlockNumber = 10;
    auto fakerMap =
        createFakers(cryptoSuite, consensusNodeSize, currentBlockNumber, consensusNodeSize);

    auto verifierFaker = fakerMap[0];
    auto verifierConfig = verifierFaker->pbftConfig();

    // Build a TestPBFTEngine backed by the verifier's config.
    auto testEngine = std::make_shared<TestPBFTEngine>(verifierConfig);

    // Advance to a higher view so that isValidNewViewMsg passes the view check.
    ViewType targetView = 1;
    verifierConfig->setView(0);
    verifierConfig->setToView(targetView);

    // The real committed proposal comes from the verifier's ledger state.
    auto rawCommitted = verifierConfig->committedProposal();
    auto committedPropFromConfig = std::make_shared<PBFTProposal>();
    committedPropFromConfig->setIndex(rawCommitted->index());
    committedPropFromConfig->setHash(rawCommitted->hash());

    BlockNumber preparedIndex = currentBlockNumber + 1;
    auto realHash = hashImpl->hash(bytesConstRef("real-block-hash-for-fib124"));

    // Build quorum viewChange messages using the actual consensus node keypairs.
    // Each viewChange carries a prepared proposal for preparedIndex with realHash.
    ViewChangeMsgList viewChangeList;
    size_t quorum = 3;
    for (size_t i = 0; i < quorum; i++)
    {
        auto vc = std::make_shared<PBFTViewChangeMsg>();
        vc->setView(targetView);
        vc->setGeneratedFrom(i);
        vc->setIndex(committedPropFromConfig->index());
        vc->setHash(committedPropFromConfig->hash());
        vc->setCommittedProposal(committedPropFromConfig);
        vc->setTimestamp(utcTime());
        vc->setVersion(1);
        vc->setPacketType(PacketType::ViewChangePacket);

        // Build prepared proposal with realHash; no signature proofs needed because
        // FakeCacheProcessor::checkPrecommitWeight always returns true.
        auto innerProposal = std::make_shared<PBFTProposal>();
        innerProposal->setIndex(preparedIndex);
        innerProposal->setHash(realHash);

        auto inner = std::make_shared<PBFTMessage>();
        inner->setConsensusProposal(innerProposal);
        inner->setIndex(preparedIndex);
        inner->setHash(realHash);
        inner->setView(0);
        inner->setGeneratedFrom(i);
        inner->setTimestamp(utcTime());
        inner->setVersion(1);
        inner->setPacketType(PacketType::PrePreparePacket);
        vc->setPreparedProposals({inner});

        // PBFTViewChangeMsg::encode() ignores cryptoSuite/keyPair; the codec wraps it.
        // Manually simulate the codec signature: encode payload → hash → sign → set.
        auto signerKP = fakerMap[i]->keyPair();
        auto vcPayload = vc->encode(nullptr, nullptr);
        auto vcPayloadHash = cryptoSuite->hashImpl()->hash(*vcPayload);
        auto vcSig = cryptoSuite->signatureImpl()->sign(*signerKP, vcPayloadHash, false);
        vc->setSignatureDataHash(vcPayloadHash);
        vc->setSignatureData(*vcSig);
        viewChangeList.push_back(vc);
    }

    // Build a NewView with a prePrepare that carries a TAMPERED hash (not realHash).
    auto tamperedHash = hashImpl->hash(bytesConstRef("tampered-hash-by-attacker"));
    auto tamperedProposal = std::make_shared<PBFTProposal>();
    tamperedProposal->setIndex(preparedIndex);
    tamperedProposal->setHash(tamperedHash);

    auto tamperedPrePrepare = std::make_shared<PBFTMessage>();
    tamperedPrePrepare->setConsensusProposal(tamperedProposal);
    tamperedPrePrepare->setIndex(preparedIndex);
    tamperedPrePrepare->setHash(tamperedHash);
    tamperedPrePrepare->setView(targetView);
    tamperedPrePrepare->setGeneratedFrom(0);
    tamperedPrePrepare->setTimestamp(utcTime());
    tamperedPrePrepare->setVersion(1);
    tamperedPrePrepare->setPacketType(PacketType::PrePreparePacket);

    auto newViewMsg = std::make_shared<PBFTNewViewMsg>();
    newViewMsg->setView(targetView);
    newViewMsg->setGeneratedFrom(0);
    newViewMsg->setIndex(committedPropFromConfig->index());
    newViewMsg->setHash(committedPropFromConfig->hash());
    newViewMsg->setTimestamp(utcTime());
    newViewMsg->setVersion(1);
    newViewMsg->setPacketType(PacketType::NewViewPacket);
    newViewMsg->setViewChangeMsgList(viewChangeList);
    newViewMsg->setPrePrepareList({tamperedPrePrepare});

    // PBFTNewViewMsg::encode ignores crypto params (same as ViewChangeMsg). Sign manually.
    auto leaderKP = fakerMap[0]->keyPair();
    auto nvPayload = newViewMsg->encode(nullptr, nullptr);
    auto nvHash = cryptoSuite->hashImpl()->hash(*nvPayload);
    auto nvSig = cryptoSuite->signatureImpl()->sign(*leaderKP, nvHash, false);
    newViewMsg->setSignatureDataHash(nvHash);
    newViewMsg->setSignatureData(*nvSig);

    // The FIB-124 cross-check must reject this NewView (tamperedHash != realHash).
    // We test against `newViewMsg` directly (not re-encoded) to avoid decode issues.
    BOOST_CHECK(!testEngine->testIsValidNewViewMsg(newViewMsg));
}

// FIB-124: The full end-to-end viewchange flow (including NewView) must complete successfully
// after the FIB-124 fix, proving the fix does not break normal consensus recovery.
BOOST_AUTO_TEST_CASE(testViewChangeFLowWithPrecommitProposals)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);

    size_t consensusNodeSize = 10;
    size_t connectedNodes = 10;
    size_t currentBlockNumber = 11;
    auto fakerMap =
        createFakers(cryptoSuite, consensusNodeSize, currentBlockNumber, connectedNodes);

    BlockNumber expectedProposal = fakerMap[0]->ledger()->blockNumber() + 1;
    IndexType leaderIndex = fakerMap[0]->pbftConfig()->leaderIndex(expectedProposal);
    auto leaderFaker = fakerMap[leaderIndex];

    auto block = fakeBlock(cryptoSuite, leaderFaker, expectedProposal, 10);
    auto blockData = std::make_shared<bytes>();
    block->encode(*blockData);

    BlockNumber futureBlockIndex = expectedProposal + 2;
    IndexType futureLeaderIndex = fakerMap[0]->pbftConfig()->leaderIndex(futureBlockIndex);
    auto futureLeader = fakerMap[futureLeaderIndex];
    auto futureBlock = fakeBlock(cryptoSuite, futureLeader, futureBlockIndex, 10);
    auto futureBlockData = std::make_shared<bytes>();
    futureBlock->encode(*futureBlockData);

    auto blockHeader = block->blockHeader();
    leaderFaker->pbftEngine()->asyncSubmitProposal(
        false, *block, blockHeader->number(), blockHeader->hash(), nullptr);
    auto futureBlockHeader = futureBlock->blockHeader();
    futureLeader->pbftEngine()->asyncSubmitProposal(
        false, *futureBlock, futureBlockHeader->number(), futureBlockHeader->hash(), nullptr);

    for (auto const& kv : fakerMap)
    {
        kv.second->pbftEngine()->executeWorker();
    }
    std::this_thread::sleep_for(std::chrono::seconds(5));

    size_t precommitSize = 5;
    for (size_t i = 0; i < std::min(precommitSize, fakerMap.size()); i++)
    {
        FakeCacheProcessor::Ptr cp = std::dynamic_pointer_cast<FakeCacheProcessor>(
            fakerMap[i]->pbftEngine()->cacheProcessor());
        if (cp->caches().count(expectedProposal))
        {
            auto cache = std::dynamic_pointer_cast<FakePBFTCache>(cp->caches()[expectedProposal]);
            if (cache)
            {
                cache->intoPrecommit();
            }
        }
        if (cp->caches().count(futureBlockIndex))
        {
            auto futureCache =
                std::dynamic_pointer_cast<FakePBFTCache>(cp->caches()[futureBlockIndex]);
            if (futureCache)
            {
                futureCache->intoPrecommit();
            }
        }
    }

    for (size_t i = 0; i < fakerMap.size(); i++)
    {
        fakerMap[i]->pbftConfig()->setConsensusTimeout(1000);
    }

    auto startT = utcTime();
    while (utcTime() - startT <= 10 * 3000)
    {
        bool allDone = true;
        for (size_t i = 0; i < fakerMap.size(); i++)
        {
            fakerMap[i]->pbftEngine()->executeWorkerByRoundbin();
            if (fakerMap[i]->ledger()->blockNumber() < futureBlockIndex)
            {
                allDone = false;
            }
        }
        if (allDone)
        {
            break;
        }
    }

    for (size_t i = 0; i < fakerMap.size(); i++)
    {
        BOOST_CHECK_EQUAL(fakerMap[i]->ledger()->blockNumber(), futureBlockIndex);
    }
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace bcos
