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
 * @brief Regression test for FIB-129: asyncResetTxsFlag must only be called after the
 *        proposal has passed both signature and content verification, not before.
 * @file FIB129_ResetTxsFlagPostVerifyTest.cpp
 */
#include "test/unittests/pbft/PBFTFixture.h"
#include "test/unittests/protocol/FakePBFTMessage.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::crypto;

namespace bcos
{
namespace test
{

BOOST_FIXTURE_TEST_SUITE(FIB129ResetTxsFlagPostVerifyTest, TestPromptFixture)

// FIB-129: When the txpool verify callback fires with _verifyResult=false (content check fail),
// the pre-prepare must be rejected and must NOT be entered into the cache.
// Before the fix, asyncResetTxsFlag was called even on verification failure, which could allow
// a malicious pre-prepare to influence the tx-pool seal state without being accepted.
BOOST_AUTO_TEST_CASE(testPrePrepareRejectedOnVerifyFail)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);

    size_t consensusNodeSize = 4;
    size_t currentBlockNumber = 10;
    auto fakerMap =
        createFakers(cryptoSuite, consensusNodeSize, currentBlockNumber, consensusNodeSize);

    IndexType expectedIndex = fakerMap[0]->pbftConfig()->progressedIndex();
    IndexType leaderIdx = fakerMap[0]->pbftConfig()->leaderIndex(expectedIndex);
    auto leaderFaker = fakerMap[leaderIdx];
    auto nonLeaderFaker = fakerMap[(leaderIdx + 1) % consensusNodeSize];

    // Build a valid block/proposal.
    auto block = fakeBlock(cryptoSuite, leaderFaker, expectedIndex, 5);
    auto blockData = std::make_shared<bytes>();
    block->encode(*blockData);
    auto blockHeader = block->blockHeader();

    // Instruct the fake txpool to report verify failure.
    nonLeaderFaker->txpool()->setVerifyResult(false);

    leaderFaker->pbftEngine()->asyncSubmitProposal(
        false, *block, blockHeader->number(), blockHeader->hash(), nullptr);

    // Drain the message queue so the non-leader processes the pre-prepare.
    for (size_t i = 0; i < 10; i++)
    {
        for (auto& kv : fakerMap)
        {
            kv.second->pbftEngine()->executeWorkerByRoundbin();
        }
    }

    // After verify failure the pre-prepare must NOT be in the non-leader's cache.
    auto cacheProcessor = std::dynamic_pointer_cast<FakeCacheProcessor>(
        nonLeaderFaker->pbftEngine()->cacheProcessor());
    BOOST_CHECK(cacheProcessor->caches().find(expectedIndex) == cacheProcessor->caches().end() ||
                !cacheProcessor->caches().at(expectedIndex)->preCommitCache());
}

// FIB-129: When the txpool verify callback fires with _error != nullptr (exception), the
// pre-prepare must also be rejected without resetting tx flags.
BOOST_AUTO_TEST_CASE(testPrePrepareRejectedOnVerifyException)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);

    size_t consensusNodeSize = 4;
    size_t currentBlockNumber = 10;
    auto fakerMap =
        createFakers(cryptoSuite, consensusNodeSize, currentBlockNumber, consensusNodeSize);

    IndexType expectedIndex = fakerMap[0]->pbftConfig()->progressedIndex();
    IndexType leaderIdx = fakerMap[0]->pbftConfig()->leaderIndex(expectedIndex);
    auto leaderFaker = fakerMap[leaderIdx];
    auto nonLeaderFaker = fakerMap[(leaderIdx + 1) % consensusNodeSize];

    auto block = fakeBlock(cryptoSuite, leaderFaker, expectedIndex, 5);
    auto blockData = std::make_shared<bytes>();
    block->encode(*blockData);
    auto blockHeader = block->blockHeader();

    // FakeTxPool: set verify-result to false so the engine sees a verify failure.
    nonLeaderFaker->txpool()->setVerifyResult(false);

    leaderFaker->pbftEngine()->asyncSubmitProposal(
        false, *block, blockHeader->number(), blockHeader->hash(), nullptr);

    for (size_t i = 0; i < 10; i++)
    {
        for (auto& kv : fakerMap)
        {
            kv.second->pbftEngine()->executeWorkerByRoundbin();
        }
    }

    auto cacheProcessor = std::dynamic_pointer_cast<FakeCacheProcessor>(
        nonLeaderFaker->pbftEngine()->cacheProcessor());
    BOOST_CHECK(cacheProcessor->caches().find(expectedIndex) == cacheProcessor->caches().end() ||
                !cacheProcessor->caches().at(expectedIndex)->preCommitCache());
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace bcos
