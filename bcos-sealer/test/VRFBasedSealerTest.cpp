/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @file VRFBasedSealerTest.cpp
 * @author: kyonGuo
 * @date 2023/8/30
 */

#include "bcos-sealer/VRFBasedSealer.h"
#include "bcos-crypto/bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/testutils/faker/FakeBlock.h"
#include "bcos-framework/testutils/faker/FakeConsensus.h"
#include "bcos-framework/testutils/faker/FakeLedger.h"
#include "bcos-sealer/SealerFactory.h"
#include "bcos-tars-protocol/protocol/BlockFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptImpl.h"
#include "bcos-txpool/TxPoolFactory.h"
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-protocol/TransactionSubmitResultFactoryImpl.h>
#include <boost/filesystem.hpp>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <memory>

using namespace bcos::storage;
using namespace std;

namespace bcos::test
{
struct TestSealerFixture
{
    TestSealerFixture()
    {
        hashImpl = std::make_shared<crypto::Keccak256>();
        auto signatureImpl = std::make_shared<crypto::Secp256k1Crypto>();
        cryptoSuite = std::make_shared<crypto::CryptoSuite>(hashImpl, signatureImpl, nullptr);
        nodeConfig = std::make_shared<bcos::tool::NodeConfig>();
        blockFactory = createBlockFactory(cryptoSuite);
        keyPair = cryptoSuite->signatureImpl()->generateKeyPair();
        auto ledger = std::make_shared<FakeLedger>(blockFactory, 20, 10, 10);
        ledger->setSystemConfig(SYSTEM_KEY_TX_COUNT_LIMIT, std::to_string(1000));
        ledger->setSystemConfig(SYSTEM_KEY_CONSENSUS_LEADER_PERIOD, std::to_string(1));
        ledger->setSystemConfig(SYSTEM_KEY_AUTH_CHECK_STATUS, std::to_string(0));
        ledger->setSystemConfig(SYSTEM_KEY_COMPATIBILITY_VERSION, protocol::DEFAULT_VERSION_STR);
        ledger->ledgerConfig()->setBlockTxCountLimit(1000);
        txpool::TxPoolFactory factory(keyPair->publicKey(), cryptoSuite,
            std::make_shared<protocol::TransactionSubmitResultFactoryImpl>(), blockFactory, nullptr,
            ledger, "", "", 1000);
        txpool = factory.createTxPool();
        txpool->init();
    }

    ~TestSealerFixture() = default;
    crypto::Hash::Ptr hashImpl;
    txpool::TxPool::Ptr txpool;
    protocol::BlockFactory::Ptr blockFactory;
    crypto::CryptoSuite::Ptr cryptoSuite = nullptr;
    bcos::tool::NodeConfig::Ptr nodeConfig;
    crypto::KeyPairInterface::Ptr keyPair;
};

BOOST_FIXTURE_TEST_SUITE(TestSealerFactory, TestSealerFixture)
BOOST_AUTO_TEST_CASE(testVRFSealer)
{
    auto factory = std::make_shared<bcos::sealer::SealerFactory>(
        nodeConfig, blockFactory, txpool, nullptr, keyPair);

    auto sealer = factory->createVRFBasedSealer();
    auto block = fakeAndCheckBlock(cryptoSuite, blockFactory, 0, 0, 10, true, false);
    auto consensus = std::make_shared<FakeConsensus>();
    sealer->init(consensus);
    auto result = sealer::VRFBasedSealer::generateTransactionForRotating(
        block, sealer->sealerConfig(), sealer->sealingManager(), hashImpl);
    BOOST_CHECK(result == sealer::Sealer::SealBlockResult::WAIT_FOR_LATEST_BLOCK);
    sealer->sealingManager()->resetLatestNumber(8);

    result = sealer::VRFBasedSealer::generateTransactionForRotating(
        block, sealer->sealerConfig(), sealer->sealingManager(), hashImpl);
    BOOST_CHECK(result == sealer::Sealer::SealBlockResult::WAIT_FOR_LATEST_BLOCK);

    sealer->sealingManager()->resetLatestNumber(9);
    result = sealer::VRFBasedSealer::generateTransactionForRotating(
        block, sealer->sealerConfig(), sealer->sealingManager(), hashImpl);
    BOOST_CHECK(result == sealer::Sealer::SealBlockResult::SUCCESS);
    BOOST_CHECK(block->transactionsMetaDataSize() == 1);
    BOOST_CHECK(block->transactionMetaData(0)->to() == precompiled::CONSENSUS_ADDRESS);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
