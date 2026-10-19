/**
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
 * @brief an invalid-signature PrePrepareRejected is DEBUG within the FIB-131 cap and WARNING
 *        past it, where the reseal notification is suppressed
 * @file FIB131_PrePrepareRejectedLevelTest.cpp
 */
#include "test/unittests/pbft/PBFTFixture.h"
#include "test/unittests/protocol/FakePBFTMessage.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/testutils/LogCapture.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::crypto;

namespace bcos::test
{
BOOST_FIXTURE_TEST_SUITE(FIB131PrePrepareRejectedLevelTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(signatureRejectionWarnsOnlyPastTheCap)
{
    auto cryptoSuite = std::make_shared<CryptoSuite>(
        std::make_shared<Keccak256>(), std::make_shared<Secp256k1Crypto>(), nullptr);
    size_t const consensusNodeSize = 4;
    auto fakerMap = createFakers(cryptoSuite, consensusNodeSize, 10, consensusNodeSize);
    IndexType expectedIndex = fakerMap[0]->pbftConfig()->progressedIndex();
    IndexType leaderIdx = fakerMap[0]->pbftConfig()->leaderIndex(expectedIndex);
    auto victimFaker = fakerMap[(leaderIdx + 1) % consensusNodeSize];
    auto victimEngine = victimFaker->pbftEngine();
    auto victimConfig = victimFaker->pbftConfig();

    auto block = fakeBlock(cryptoSuite, fakerMap[leaderIdx], expectedIndex, 3);
    auto blockData = std::make_shared<bytes>();
    block->encode(*blockData);
    auto hash = block->blockHeader()->hash();
    // signed by a key that is not the leader's, so checkSignature fails
    KeyPairInterface::Ptr wrongKP{cryptoSuite->signatureImpl()->generateKeyPair().release()};
    auto wrongMsgFixture = std::make_shared<PBFTMessageFixture>(cryptoSuite, wrongKP);
    auto pbftMsg = fakePBFTMessage(utcTime(), 1, victimConfig->view(), leaderIdx, hash,
        static_cast<protocol::BlockNumber>(expectedIndex), *blockData, 0, wrongMsgFixture,
        PacketType::PrePreparePacket);
    pbftMsg->setConsensusProposal(wrongMsgFixture->fakePBFTProposal(
        static_cast<protocol::BlockNumber>(expectedIndex), hash, *blockData, {}, {}));
    auto sendInvalid = [&]() {
        auto encoded = victimConfig->codec()->encode(pbftMsg);
        victimEngine->onReceivePBFTMessage(
            nullptr, victimFaker->keyPair()->publicKey(), ref(*encoded), nullptr);
        victimEngine->executeWorker();
    };

    // PBFT at INFO: a DEBUG rejection is filtered out, a WARNING one is printed
    setModuleLogLevel(LogModule::PBFT, LogLevel::INFO);
    LogCapture withinCap;
    // PBFTEngine::c_maxInvalidPrePreparePerPeer (protected) is 3
    for (int i = 0; i < 3; ++i)
    {
        sendInvalid();
    }
    auto withinText = withinCap.text();
    BOOST_CHECK_MESSAGE(
        withinText.find("PrePrepareRejected,reason=signature") == std::string::npos, withinText);

    LogCapture pastCap;
    sendInvalid();
    auto pastText = pastCap.text();
    resetModuleLogLevel(LogModule::PBFT);
    BOOST_CHECK_MESSAGE(
        pastText.find("PrePrepareRejected,reason=signature") != std::string::npos, pastText);
    BOOST_CHECK_MESSAGE(pastText.find("resealSuppressed=true") != std::string::npos, pastText);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
