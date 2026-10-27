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
#include "bcos-txpool/TxPoolFactory.h"
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-crypto/vrf/Secp256k1Vrf.h>
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-protocol/TransactionSubmitResultFactoryImpl.h>
#include <bcos-utilities/IOServicePool.h>
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
            ledger, "", "", 1000, bcos::txpool::DEFAULT_POOL_LIMIT, true);
        txpool = factory.createTxPool(*ioServicePool->getIOService(), ioServicePool);
        txpool->init();
    }

    ~TestSealerFixture() = default;
    // ioServicePool MUST be declared before txpool to ensure it outlives
    // Timer objects created by txpool that reference its io_context.
    bcos::IOServicePool::Ptr ioServicePool = std::make_shared<bcos::IOServicePool>(1, "vrfTest");
    crypto::Hash::Ptr hashImpl;
    txpool::TxPool::Ptr txpool;
    protocol::BlockFactory::Ptr blockFactory;
    crypto::CryptoSuite::Ptr cryptoSuite = nullptr;
    bcos::tool::NodeConfig::Ptr nodeConfig;
    crypto::KeyPairInterface::Ptr keyPair;
};

BOOST_FIXTURE_TEST_SUITE(TestVRFSealerFactory, TestSealerFixture)
BOOST_AUTO_TEST_CASE(testVRFSealer)
{
    auto factory = std::make_shared<bcos::sealer::SealerFactory>(
        nodeConfig, blockFactory, txpool, nullptr, keyPair, *ioServicePool->getIOService());

    auto sealer = factory->createVRFBasedSealer();
    auto block = fakeAndCheckBlock(cryptoSuite, blockFactory, 0, 0, 10, true, false);
    auto consensus = std::make_shared<FakeConsensus>();
    sealer->init(consensus);
    auto result = sealer::VRFBasedSealer::generateTransactionForRotating(
        block, sealer->sealerConfig(), sealer->sealingManager(), hashImpl, false);
    BOOST_CHECK(result == sealer::Sealer::SealBlockResult::WAIT_FOR_LATEST_BLOCK);
    sealer->sealingManager()->resetLatestNumber(8);

    result = sealer::VRFBasedSealer::generateTransactionForRotating(
        block, sealer->sealerConfig(), sealer->sealingManager(), hashImpl, false);
    BOOST_CHECK(result == sealer::Sealer::SealBlockResult::WAIT_FOR_LATEST_BLOCK);

    sealer->sealingManager()->resetLatestNumber(9);
    result = sealer::VRFBasedSealer::generateTransactionForRotating(
        block, sealer->sealerConfig(), sealer->sealingManager(), hashImpl, false);
    BOOST_CHECK(result == sealer::Sealer::SealBlockResult::SUCCESS);
    BOOST_CHECK(block->transactionsMetaDataSize() == 1);
    BOOST_CHECK(block->transactionMetaDatas()[0]->to() == precompiled::CONSENSUS_ADDRESS);

    auto block2 = fakeAndCheckBlock(cryptoSuite, blockFactory, 0, 0, 10, true, false);
    sealer->sealingManager()->resetLatestNumber(0);
    result = sealer::VRFBasedSealer::generateTransactionForRotating(
        block2, sealer->sealerConfig(), sealer->sealingManager(), hashImpl, true);
    BOOST_CHECK(result == sealer::Sealer::SealBlockResult::SUCCESS);
    BOOST_CHECK(block2->transactionsMetaDataSize() == 1);
    BOOST_CHECK(block2->transactionMetaDatas()[0]->to() == precompiled::CONSENSUS_ADDRESS);
}

BOOST_AUTO_TEST_CASE(testVRFSecp256k1)
{
    hashImpl = std::make_shared<crypto::Keccak256>();
    auto signatureImpl = std::make_shared<crypto::Secp256k1Crypto>();
    cryptoSuite = std::make_shared<crypto::CryptoSuite>(hashImpl, signatureImpl, nullptr);
    nodeConfig = std::make_shared<bcos::tool::NodeConfig>();
    blockFactory = createBlockFactory(cryptoSuite);
    keyPair = cryptoSuite->signatureImpl()->generateKeyPair();

    auto secretRef =
        bytesConstRef(reinterpret_cast<const byte*>(keyPair->secretKey()->data().data()),
            keyPair->secretKey()->size());
    bytes inputBytes{'t', 'e', 's', 't'};
    auto inputRef = bytesConstRef(inputBytes.data(), inputBytes.size());

    auto vrfPublicKey = crypto::secp256k1VrfDerivePublicKey(secretRef);
    BOOST_REQUIRE_EQUAL(vrfPublicKey.size(), sealer::secp256k1PublicKeySize);

    auto vrfProof = crypto::secp256k1VrfProve(secretRef, inputRef);
    BOOST_REQUIRE_EQUAL(vrfProof.size(), sealer::secp256k1VRFProofSize);

    auto publicKeyRef = bytesConstRef(vrfPublicKey.data(), vrfPublicKey.size());
    auto proofRef = bytesConstRef(vrfProof.data(), vrfProof.size());
    BOOST_CHECK(crypto::secp256k1VrfIsValidPublicKey(publicKeyRef));

    auto vrfProofHash = crypto::secp256k1VrfProofToHash(proofRef);
    BOOST_CHECK_EQUAL(vrfProofHash.size(), 32u);

    BOOST_CHECK(crypto::secp256k1VrfVerify(publicKeyRef, inputRef, proofRef));

    // check error input
    bytes inputError{'t', 'e', 's', 't', ' ', 'e', 'r', 'r', 'o', 'r'};
    BOOST_CHECK(!crypto::secp256k1VrfVerify(
        publicKeyRef, bytesConstRef(inputError.data(), inputError.size()), proofRef));

    // check error public key
    bytes vrfPublicKeyError(sealer::secp256k1PublicKeySize, 0);
    BOOST_CHECK(!crypto::secp256k1VrfVerify(
        bytesConstRef(vrfPublicKeyError.data(), vrfPublicKeyError.size()), inputRef, proofRef));

    // check error proof
    bytes vrfProofError(sealer::secp256k1VRFProofSize, 0);
    BOOST_CHECK(!crypto::secp256k1VrfVerify(
        publicKeyRef, inputRef, bytesConstRef(vrfProofError.data(), vrfProofError.size())));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
