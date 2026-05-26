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
 * @brief FIB-147: VRFBasedSealer Curve25519 path used to assign the
 *        wedpr_curve25519_vrf_prove_utf8 return value to a SHADOWED inner
 *        variable, leaving the outer-scope vrfProve at its initial 0.
 *        Subsequent error checks on the outer would always pass, treating
 *        a failed VRF proof as success.
 *
 *        This regression test exercises the Curve25519 default path of
 *        generateTransactionForRotating end-to-end. After the fix, the
 *        outer-scope vrfProve receives the actual call result and is
 *        checked. We verify the success path still returns SUCCESS to
 *        guard against regressions in the curve25519 branch.
 *
 *        The failure path (vrf_prove returns -1) cannot be triggered
 *        portably from a UT: wedpr_curve25519_vrf_prove_utf8 succeeds for
 *        any non-empty private key, and using a too-small proof buffer
 *        causes a Rust panic in the underlying library rather than a
 *        clean error return. The bug fix is therefore verified by code
 *        review (the shadowing 'auto vrfProve = ...' is removed) plus
 *        this regression test on the success path.
 *
 * @file FIB147_VrfShadowingTest.cpp
 * @date 2026-05-08
 */
#include "bcos-crypto/bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/testutils/faker/FakeBlock.h"
#include "bcos-framework/testutils/faker/FakeConsensus.h"
#include "bcos-framework/testutils/faker/FakeLedger.h"
#include "bcos-sealer/SealerFactory.h"
#include "bcos-sealer/VRFBasedSealer.h"
#include "bcos-txpool/TxPoolFactory.h"
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-protocol/TransactionSubmitResultFactoryImpl.h>
#include <wedpr-crypto/WedprUtilities.h>
#include <boost/test/unit_test.hpp>
#include <memory>

using namespace bcos::storage;

namespace bcos::test
{

struct Fib147SealerFixture
{
    Fib147SealerFixture()
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
        txpool = factory.createTxPool();
        txpool->init();
    }

    ~Fib147SealerFixture() = default;
    crypto::Hash::Ptr hashImpl;
    txpool::TxPool::Ptr txpool;
    protocol::BlockFactory::Ptr blockFactory;
    crypto::CryptoSuite::Ptr cryptoSuite;
    bcos::tool::NodeConfig::Ptr nodeConfig;
    crypto::KeyPairInterface::Ptr keyPair;
};

BOOST_FIXTURE_TEST_SUITE(FIB147VrfShadowingTest, Fib147SealerFixture)

// Regression test: the default Curve25519 VRF path of generateTransactionForRotating
// must return SUCCESS for a well-formed input. Pre-fix this still returned SUCCESS
// (because the shadow caused failures to be silently ignored — a different problem)
// but the fix replaces the shadowed assignment with a direct one to the outer
// vrfProve and checks it. This test guards against a regression that would break
// the curve25519 success path in the process of removing the shadow.
BOOST_AUTO_TEST_CASE(curve25519_default_path_returns_success_after_shadow_fix)
{
    auto factory = std::make_shared<bcos::sealer::SealerFactory>(
        nodeConfig, blockFactory, txpool, nullptr, keyPair);

    auto sealer = factory->createVRFBasedSealer();
    auto block = fakeAndCheckBlock(cryptoSuite, blockFactory, 0, 0, 10, true, false);
    auto consensus = std::make_shared<FakeConsensus>();
    sealer->init(consensus);

    // Set sealing manager's latest number to satisfy generateTransactionForRotating's
    // pipeline-wait check (uses blockNumberInput=false here, so it requires
    // latestNumber >= block.number - 1; block was created at index 9 so 9 satisfies it).
    sealer->sealingManager()->resetLatestNumber(9);

    // blockNumberInput=false drives the curve25519 default path (no
    // feature_rpbft_vrf_type_secp256k1 set on the consensus config).
    auto result = sealer::VRFBasedSealer::generateTransactionForRotating(block,
        sealer->sealerConfig(), sealer->sealingManager(), hashImpl,
        /*blockNumberInput=*/false);

    BOOST_CHECK_EQUAL(result, sealer::Sealer::SealBlockResult::SUCCESS);
    BOOST_CHECK_EQUAL(block->transactionsMetaDataSize(), 1);
    BOOST_CHECK_EQUAL(block->transactionMetaDatas()[0]->to(), precompiled::CONSENSUS_ADDRESS);
}

// Direct check at the wedpr layer: confirm the success indicator the outer
// vrfProve compares against (WEDPR_SUCCESS = 0). If wedpr ever changes this
// contract, the FIB-147 fix's check predicate (vrfProve != WEDPR_SUCCESS)
// would become stale and this test would catch it.
BOOST_AUTO_TEST_CASE(wedpr_curve25519_success_constant_is_zero)
{
    BOOST_CHECK_EQUAL(WEDPR_SUCCESS, 0);
    BOOST_CHECK_EQUAL(WEDPR_ERROR, -1);

    // Sanity: a well-formed direct call returns 0.
    bcos::crypto::KeyPairInterface::Ptr kp =
        std::make_shared<crypto::Secp256k1Crypto>()->generateKeyPair();
    CInputBuffer privateKey{
        reinterpret_cast<const char*>(kp->secretKey()->data().data()), kp->secretKey()->size()};
    CInputBuffer inputMsg{"hello", 5};
    bcos::bytes proofData(96, 0);
    COutputBuffer proof{reinterpret_cast<char*>(proofData.data()), proofData.size()};
    int8_t result = wedpr_curve25519_vrf_prove_utf8(&privateKey, &inputMsg, &proof);
    BOOST_CHECK_EQUAL(result, WEDPR_SUCCESS);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
