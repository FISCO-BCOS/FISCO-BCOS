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
 * @brief demo for bcos-txpool
 * @file txpool_demo.cpp
 * @date 2022.01.24
 * @author yujiechen
 */

#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-crypto/hash/SM3.h"
#include "bcos-protocol/TransactionSubmitResultImpl.h"
#include "bcos-txpool/test/unittests/txpool/TxPoolFixture.h"
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/interfaces/crypto/CryptoSuite.h>
#include <bcos-framework/interfaces/protocol/CommonError.h>
#include <bcos-protocol/testutils/protocol/FakeTransaction.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/test/unit_test.hpp>
using namespace bcos;
using namespace bcos::front;
using namespace bcos::protocol;
using namespace bcos::test;
using namespace bcos::txpool;
using namespace bcos::crypto;

void testSubmitAndRemoveTransaction(bcos::crypto::CryptoSuite::Ptr _cryptoSuite, size_t _count)
{
    auto signatureImpl = _cryptoSuite->signatureImpl();
    auto hashImpl = _cryptoSuite->hashImpl();
    auto keyPair = signatureImpl->generateKeyPair();
    std::string groupId = "group_test_for_txpool";
    std::string chainId = "chain_test_for_txpool";
    int64_t blockLimit = 1000;
    auto fakeGateWay = std::make_shared<FakeGateWay>();
    auto faker = std::make_shared<TxPoolFixture>(
        keyPair->publicKey(), _cryptoSuite, groupId, chainId, blockLimit, fakeGateWay);
    faker->init();
    faker->appendSealer(faker->nodeID());
    auto ledger = faker->ledger();
    auto txpool = faker->txpool();

    auto txsResult = std::make_shared<TransactionSubmitResults>();
    txsResult->resize(_count);
    txpool->txpoolConfig()->setPoolLimit(_count + 1000);
    tbb::parallel_for(tbb::blocked_range<int>(0, _count), [&](const tbb::blocked_range<int>& _r) {
        for (auto i = _r.begin(); i < _r.end(); i++)
        {
            auto tx = fakeTransaction(_cryptoSuite, 1000 + i,
                ledger->blockNumber() + blockLimit - 4, faker->chainId(), faker->groupId());
            auto encodedData = tx->encode();
            auto txData = std::make_shared<bytes>(encodedData.begin(), encodedData.end());
            txpool->asyncSubmit(txData, [](Error::Ptr, TransactionSubmitResult::Ptr) {});
            auto result = std::make_shared<TransactionSubmitResultImpl>();
            result->setTxHash(tx->hash());
            (*txsResult)[i] = result;
        }
    });
    while (txpool->txpoolStorage()->size() < _count)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::cout << "### remove submitted txs, size:" << txpool->txpoolStorage()->size() << std::endl;
    // remove the txs
    txpool->asyncNotifyBlockResult(ledger->blockNumber() + 1, txsResult, [](Error::Ptr) {});
}

void Usage(std::string const& _appName)
{
    std::cout << _appName << " count" << std::endl;
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        Usage(argv[0]);
        return -1;
    }
    size_t count = atoi(argv[1]);
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    testSubmitAndRemoveTransaction(cryptoSuite, count);
    getchar();
}