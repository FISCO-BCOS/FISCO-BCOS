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
 * @brief test for pbft related messages
 * @file PBFTMessageTest.h
 * @author: yujiechen
 * @date 2021-04-16
 */
#include "FakePBFTMessage.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-crypto/signature/sm2/SM2Crypto.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::crypto;
namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(PBFTMessageTest, TestPromptFixture)
BOOST_AUTO_TEST_CASE(testNormalPBFTMessage)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    testPBFTMessage(PacketType::PrePreparePacket, cryptoSuite);
    testPBFTMessage(PacketType::PreparePacket, cryptoSuite);
    testPBFTMessage(PacketType::CommitPacket, cryptoSuite);
    testPBFTMessage(PacketType::CommittedProposalResponse, cryptoSuite);
    testPBFTMessage(PacketType::CheckPoint, cryptoSuite);
}

BOOST_AUTO_TEST_CASE(testSMPBFTMessage)
{
    auto hashImpl = std::make_shared<SM3>();
    auto signatureImpl = std::make_shared<SM2Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    testPBFTMessage(PacketType::PrePreparePacket, cryptoSuite);
    testPBFTMessage(PacketType::PreparePacket, cryptoSuite);
    testPBFTMessage(PacketType::CommitPacket, cryptoSuite);
    testPBFTMessage(PacketType::CommittedProposalResponse, cryptoSuite);
    testPBFTMessage(PacketType::CheckPoint, cryptoSuite);
}

BOOST_AUTO_TEST_CASE(testNormalViewChangeMessage)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    testPBFTViewChangeMessage(cryptoSuite);
}

BOOST_AUTO_TEST_CASE(testSMViewChangeMessage)
{
    auto hashImpl = std::make_shared<SM3>();
    auto signatureImpl = std::make_shared<SM2Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    testPBFTViewChangeMessage(cryptoSuite);
}

BOOST_AUTO_TEST_CASE(testNormalNewViewMessage)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    testPBFTNewViewMessage(cryptoSuite);
}

BOOST_AUTO_TEST_CASE(testSMNewViewMessage)
{
    auto hashImpl = std::make_shared<SM3>();
    auto signatureImpl = std::make_shared<SM2Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    testPBFTNewViewMessage(cryptoSuite);
}

BOOST_AUTO_TEST_CASE(testNormalPBFTRequest)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    testPBFTRequest(cryptoSuite, PacketType::CommittedProposalRequest);
    testPBFTRequest(cryptoSuite, PacketType::PreparedProposalRequest);
}

BOOST_AUTO_TEST_CASE(testSMPBFTRequest)
{
    auto hashImpl = std::make_shared<SM3>();
    auto signatureImpl = std::make_shared<SM2Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    testPBFTRequest(cryptoSuite, PacketType::CommittedProposalRequest);
    testPBFTRequest(cryptoSuite, PacketType::PreparedProposalRequest);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos