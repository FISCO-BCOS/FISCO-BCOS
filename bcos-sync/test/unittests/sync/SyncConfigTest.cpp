/**
 *  Copyright (C) 2021 bcos-sync.
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
 * @brief test for the syncConfig
 * @file SyncConfigTest.h
 * @author: yujiechen
 * @date 2021-06-08
 */
#include "SyncFixture.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-crypto/hash/SM3.h"
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::sync;
using namespace bcos::crypto;

namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(SyncConfigTest, TestPromptFixture)

void testSyncConfig(CryptoSuite::Ptr _cryptoSuite)
{
    auto gateWay = std::make_shared<FakeGateWay>();
    auto faker = std::make_shared<SyncFixture>(_cryptoSuite, gateWay);
    faker->init();
    // check the config
    auto config = faker->syncConfig();
    BOOST_CHECK(config->ledger());
    BOOST_CHECK(config->nodeID()->data() == faker->nodeID()->data());
    BOOST_CHECK(config->blockFactory());
    BOOST_CHECK(config->frontService());
    BOOST_CHECK(config->scheduler());
    BOOST_CHECK(config->consensus());
    BOOST_CHECK(config->msgFactory());

    auto ledgerData = faker->ledger()->ledgerData();
    auto genesisBlock = (ledgerData[0]);
    auto genesisBlockHeader = genesisBlock->blockHeader();
    BOOST_CHECK(config->genesisHash().asBytes() == genesisBlockHeader->hash().asBytes());
    BOOST_CHECK(config->blockNumber() == faker->ledger()->blockNumber());
    BOOST_CHECK(config->nextBlock() == faker->ledger()->blockNumber() + 1);
    BOOST_CHECK(config->hash().asBytes() == faker->ledger()->ledgerConfig()->hash().asBytes());
}

BOOST_AUTO_TEST_CASE(testNonSMSyncConfig)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    testSyncConfig(cryptoSuite);
}

BOOST_AUTO_TEST_CASE(testSMSyncConfig)
{
    auto hashImpl = std::make_shared<SM3>();
    auto signatureImpl = std::make_shared<SM2Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    testSyncConfig(cryptoSuite);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
