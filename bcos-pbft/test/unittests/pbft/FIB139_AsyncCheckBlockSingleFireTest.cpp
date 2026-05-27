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
 * @brief Regression test for FIB-139: asyncCheckBlock must fire _onVerifyFinish
 *        exactly once even when the callback itself throws.
 * @file FIB139_AsyncCheckBlockSingleFireTest.cpp
 * @date 2026-05-07
 */
#include "bcos-pbft/pbft/engine/BlockValidator.h"
#include "test/unittests/pbft/PBFTFixture.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::crypto;
using namespace bcos::test;
using namespace bcos::protocol;

namespace bcos::test
{

BOOST_AUTO_TEST_SUITE(FIB139_AsyncCheckBlockSingleFireTest)

// Helper: build a minimal block with a given block number.
static Block::Ptr makeBlock(BlockFactory::Ptr blockFactory, BlockNumber number)
{
    auto block = blockFactory->createBlock();
    auto blockHeader = blockFactory->blockHeaderFactory()->createBlockHeader();
    blockHeader->setNumber(number);
    blockHeader->calculateHash(*blockFactory->cryptoSuite()->hashImpl());
    block->setBlockHeader(blockHeader);
    return block;
}

// Test: callback is invoked exactly once on the normal (non-exception) path.
BOOST_AUTO_TEST_CASE(CallbackFiredExactlyOnce_NormalPath)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signImpl, nullptr);

    auto faker = createPBFTFixture(cryptoSuite);
    faker->appendConsensusNode(faker->nodeID());
    faker->init();

    auto config = faker->pbftConfig();
    auto blockValidator = std::make_shared<BlockValidator>(config);

    // Genesis block (number == 0) fires the callback immediately with (nullptr, true).
    std::atomic<int> callCount{0};
    auto genesisBlock = makeBlock(faker->blockFactory(), 0);
    blockValidator->asyncCheckBlock(genesisBlock, [&](Error::Ptr, bool) { ++callCount; });

    // Give the thread pool time to complete.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    BOOST_CHECK_EQUAL(callCount.load(), 1);
    blockValidator->stop();
}

// Test: callback is invoked exactly once when the callback itself throws.
// Before the fix the outer catch would invoke it a second time.
BOOST_AUTO_TEST_CASE(CallbackFiredExactlyOnce_CallbackThrows)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signImpl, nullptr);

    auto faker = createPBFTFixture(cryptoSuite);
    faker->appendConsensusNode(faker->nodeID());
    faker->init();

    auto config = faker->pbftConfig();
    auto blockValidator = std::make_shared<BlockValidator>(config);

    // Genesis block fires immediately with true, then the callback throws.
    // Before the fix: outer catch catches that throw and calls the callback again → count == 2.
    std::atomic<int> callCount{0};
    auto genesisBlock = makeBlock(faker->blockFactory(), 0);
    blockValidator->asyncCheckBlock(genesisBlock, [&](Error::Ptr, bool) {
        ++callCount;
        throw std::runtime_error("callback deliberate throw");
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    // Must be exactly 1 even though the callback threw.
    BOOST_CHECK_EQUAL(callCount.load(), 1);
    blockValidator->stop();
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
