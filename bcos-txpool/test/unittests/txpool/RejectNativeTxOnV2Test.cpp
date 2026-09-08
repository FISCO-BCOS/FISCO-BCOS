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
 * @brief regression test for the round-2 review finding F2 on #5554: an
 *        executor_version >= 2 (pure-Ethereum) chain seals ONLY Web3
 *        transactions, so a native BCOS transaction must be refused at pool
 *        admission — otherwise the leader's finishExecute throws in
 *        calculateEthereumTransactionRoot and block production halts.
 * @file RejectNativeTxOnV2Test.cpp
 */
#include "bcos-framework/bcos-framework/testutils/faker/FakeTransaction.h"
#include "bcos-protocol/TransactionStatus.h"
#include "bcos-txpool/txpool/validator/TxValidator.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/dispatcher/SchedulerInterface.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::txpool;
using namespace bcos::protocol;

namespace bcos::test
{
BOOST_FIXTURE_TEST_SUITE(RejectNativeTxOnV2Test, TestPromptFixture)

BOOST_AUTO_TEST_CASE(nativeTxRefusedOnV2Chain)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    // A genuinely signed, valid native (BCOS) transaction.
    auto nativeTx = fakeTransaction(cryptoSuite, "1", 1000, "chainId", "groupId");

    std::weak_ptr<bcos::scheduler::SchedulerInterface> noScheduler;

    // Control on a v0/v1 validator (flag OFF, wrong group): the tx is NOT refused
    // by the v2 gate — it proceeds and fails the ordinary group check.
    auto v1Validator = std::make_shared<TxValidator>(
        nullptr, nullptr, nullptr, "other_group", "chainId", noScheduler,
        /*rejectNativeTxOnV2Chain=*/false);
    auto v1Result = v1Validator->verify(*nativeTx);
    BOOST_CHECK(v1Result != TransactionStatus::TxTypeNotSupported);
    BOOST_CHECK(v1Result == TransactionStatus::InvalidGroupId);

    // v2-chain validator: the native tx is refused at the gate, before any of the
    // (null here) checker dependencies are touched.
    auto v2Validator = std::make_shared<TxValidator>(
        nullptr, nullptr, nullptr, "groupId", "chainId", noScheduler,
        /*rejectNativeTxOnV2Chain=*/true);
    BOOST_CHECK(v2Validator->verify(*nativeTx) == TransactionStatus::TxTypeNotSupported);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
