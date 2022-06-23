/*
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
 * @brief test for Transaction
 * @file PBTransactionTest.h
 * @author: yujiechen
 * @date: 2021-03-16
 */
#include "bcos-protocol/protobuf/PBTransaction.h"
#include "bcos-protocol/Common.h"
#include "bcos-protocol/testutils/protocol/FakeTransaction.h"
#include "bcos-crypto/interfaces/crypto/KeyPairInterface.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-crypto/signature/sm2/SM2Crypto.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/tools/old/interface.hpp>

using namespace bcos;
using namespace bcos::crypto;
using namespace bcos::protocol;

namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(PBTransactionTest, TestPromptFixture)
BOOST_AUTO_TEST_CASE(testNormalTransaction)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    bcos::crypto::KeyPairInterface::Ptr keyPair = cryptoSuite->signatureImpl()->generateKeyPair();
    auto to = cryptoSuite->calculateAddress(keyPair->publicKey());
    std::string inputStr = "testTransaction";
    bytes input = asBytes(inputStr);
    u256 nonce = 120012323;
    int64_t blockLimit = 1000023;
    std::string chainId = "chainId";
    std::string groupId = "groupId";

    testTransaction(cryptoSuite, keyPair, std::string_view((char*)to.asBytes().data(), 20), input,
        nonce, blockLimit, chainId, groupId);
}
BOOST_AUTO_TEST_CASE(testSMTransaction)
{
    auto hashImpl = std::make_shared<SM3>();
    auto signatureImpl = std::make_shared<SM2Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    bcos::crypto::KeyPairInterface::Ptr keyPair = cryptoSuite->signatureImpl()->generateKeyPair();
    auto to = Address();
    std::string inputStr = "testTransaction";
    bytes input = asBytes(inputStr);
    u256 nonce = 120012323;
    int64_t blockLimit = 1000023;
    std::string chainId = "chainId";
    std::string groupId = "groupId";

    testTransaction(cryptoSuite, keyPair, std::string_view((char*)to.data(), 20), input, nonce,
        blockLimit, chainId, groupId);
}

void testBlock(CryptoSuite::Ptr cryptoSuite)
{
    std::string to = "5fe3c4c3e2079879a0dba1937aca95ac16e68f0f";
    std::string input =
        "616e73616374696f6e1241e4dd502f6f5f3dbc0639e8587f2a9d6227dddac55e4c40b098fd3e3c4a60cabe6cd"
        "7";
    PBTransaction tx(cryptoSuite, 100, to, *fromHexString(input), u256(10086), 1000, "testChain",
        "testGroup", 888);

    auto keyPair = cryptoSuite->signatureImpl()->generateKeyPair();
    auto sign = cryptoSuite->signatureImpl()->sign(*keyPair, tx.hash());
    tx.updateSignature(bcos::ref(*sign), keyPair->publicKey()->data());

    auto buffer = tx.encode();

    PBTransaction decodedTx(cryptoSuite, buffer, false);

    BOOST_CHECK_EQUAL(tx.version(), decodedTx.version());
    BOOST_CHECK_EQUAL(tx.to(), decodedTx.to());
    BOOST_CHECK_EQUAL(tx.input().toString(), decodedTx.input().toString());
    BOOST_CHECK_EQUAL(tx.nonce().convert_to<int>(), decodedTx.nonce().convert_to<int>());
    BOOST_CHECK_EQUAL(tx.blockLimit(), decodedTx.blockLimit());
    BOOST_CHECK_EQUAL(tx.chainId(), decodedTx.chainId());
    BOOST_CHECK_EQUAL(tx.groupId(), decodedTx.groupId());
    BOOST_CHECK_EQUAL(tx.importTime(), decodedTx.importTime());
    BOOST_CHECK_NO_THROW(tx.verify());

    (const_cast<byte*>(buffer.data()))[0] += 1;
    BOOST_CHECK_THROW(
        std::make_shared<PBTransaction>(cryptoSuite, buffer, true), PBObjectDecodeException);
}

BOOST_AUTO_TEST_CASE(testTransactionWithRawData)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);

    testBlock(cryptoSuite);
    /*
    auto encodedData =
        "0a6108011207636861696e49641a0767726f7570496420d7843d2a200000000000000000000000000000000000"
        "000000000000000000000007273e2332145fe3c4c3e2079879a0dba1937aca95ac16e68f0f3a0f746573745472"
        "616e73616374696f6e1241e4dd502f6f5f3dbc0639e8587f2a9d6227dddac55e4c40b098fd3e3c4a60cabe6cd7"
        "2c4b98b5d4d08f484b93541026e0144d1025d4b24e2cfcaa12f48bcf6499006080b99081842f";
    auto hashData = "4f453741741d530125c9dc170dec385d2711c89d22f7c6a1842b51a4f0d9c486";
    auto sender = "5fe3c4c3e2079879a0dba1937aca95ac16e68f0f";
    auto encodedBytes = fromHexString(encodedData);
    auto decodedTransaction = std::make_shared<PBTransaction>(cryptoSuite, *encodedBytes, true);
    BOOST_CHECK(decodedTransaction->hash().hex() == hashData);
    BOOST_CHECK(
        *toHexString(decodedTransaction->to()) == "5fe3c4c3e2079879a0dba1937aca95ac16e68f0f");
    BOOST_CHECK(*toHexString(decodedTransaction->sender()) == sender);
    BOOST_CHECK(decodedTransaction->type() == TransactionType::MessageCall);
    BOOST_CHECK(decodedTransaction->groupId() == "groupId");
    BOOST_CHECK(decodedTransaction->chainId() == "chainId");
    BOOST_CHECK(decodedTransaction->nonce() == u256(120012323));
    BOOST_CHECK(decodedTransaction->blockLimit() == 1000023);

    // test exception case
    (*encodedBytes)[0] += 1;
    BOOST_CHECK_THROW(
        std::make_shared<PBTransaction>(cryptoSuite, *encodedBytes, true), PBObjectDecodeException);
        */
}

BOOST_AUTO_TEST_CASE(testSMTransactionWithRawData)
{
    auto hashImpl = std::make_shared<SM3>();
    auto signatureImpl = std::make_shared<SM2Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);

    testBlock(cryptoSuite);
    /*
    auto encodedData =
        "0a4b08011207636861696e49641a0767726f7570496420d7843d2a200000000000000000000000000000000000"
        "000000000000000000000007273e233a0f746573745472616e73616374696f6e128001dbbdfc7aafc79dda0225"
        "61e020fc7246fe078ca2c652214bf8bff063b4bb9c4595af762147850eecd5bd002e43a99e653172920452b07b"
        "12ae114e9d7b813d08679703c81bd859386b0c1496b715696a1e5848004558714e784ce2ead9272173314e9e5c"
        "7d7b10756a55e2bf7f05e8ba8caebc9c1dc034cfdbdd11db78c7ecdc6083bb9081842f";
    auto hashData = "0d7972e05888c7c6638aaeb0d3bfd8b5d4dd4cf73e3fbdae9ee6b6df733db040";
    auto sender = "7f7be1e116815757279adf37a781863fee194e3b";
    auto encodedBytes = fromHexString(encodedData);
    auto decodedTransaction = std::make_shared<PBTransaction>(cryptoSuite, *encodedBytes, true);
    BOOST_CHECK(decodedTransaction->hash().hex() == hashData);
    BOOST_CHECK(*toHexString(decodedTransaction->sender()) == sender);
    BOOST_CHECK(decodedTransaction->to().empty());
    BOOST_CHECK(decodedTransaction->to().empty());
    BOOST_CHECK(decodedTransaction->type() == TransactionType::ContractCreation);
    BOOST_CHECK(decodedTransaction->groupId() == "groupId");
    BOOST_CHECK(decodedTransaction->chainId() == "chainId");
    BOOST_CHECK(decodedTransaction->nonce() == u256(120012323));
    BOOST_CHECK(decodedTransaction->blockLimit() == 1000023);
    // test exception case
    (*encodedBytes)[0] += 1;
    BOOST_CHECK_THROW(
        std::make_shared<PBTransaction>(cryptoSuite, *encodedBytes, true), PBObjectDecodeException);
        */
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos