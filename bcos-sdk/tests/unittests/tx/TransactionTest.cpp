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
 * @file TransactionTest.cpp
 * @author: yujiechen
 * @date 2022-05-31
 */
#include <bcos-cpp-sdk/utilities/receipt/ReceiptBuilder.h>
#include <bcos-cpp-sdk/utilities/tx/TransactionBuilder.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>

using namespace bcostars;
using namespace bcos;
using namespace bcos::crypto;
using namespace bcos::cppsdk::utilities;

namespace bcos::test
{
BOOST_FIXTURE_TEST_SUITE(TransactionTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(test_transaction)
{
    auto txBuilder = std::make_unique<TransactionBuilder>();
    auto cryptoSuite =
        std::make_shared<bcos::crypto::CryptoSuite>(std::make_shared<bcos::crypto::Keccak256>(),
            std::make_shared<bcos::crypto::Secp256k1Crypto>(), nullptr);
    bcos::bytes input = fromHex(std::string(
        "2fe99bdc0000000000000000000000000000000000000000000000000000000000000060000000000000000000"
        "00000000000000000000000000000000000000000000a000000000000000000000000000000000000000000000"
        "000000000000000000e00000000000000000000000000000000000000000000000000000000000000005746573"
        "743100000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000057465737432000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000574657374330000000000"
        "00000000000000000000000000000000000000000000"));
    auto txData = txBuilder->createTransactionData(
        "group0", "chain0", "0x6849f21d1e455e9f0712b1e99fa4fcd23758e8f1", input, "", 501);
    txData->nonce = "82456994196425209985513938878262806289094428076435270713862099506789849517246";
    auto hash = txData->hash(cryptoSuite->hashImpl()).hex();
    BOOST_CHECK_EQUAL(hash, "0359a5588c5e9c9dcfd2f4ece850d6f4c41bc88e2c27cc051890f26ef0ef118f");

    auto txDataBytes = txBuilder->encodeTransactionData(*txData);
    auto txHash = txBuilder->calculateTransactionDataHash(CryptoType::Secp256K1, *txData).hex();

    BOOST_CHECK_EQUAL(txHash, "0359a5588c5e9c9dcfd2f4ece850d6f4c41bc88e2c27cc051890f26ef0ef118f");

    auto json = txBuilder->decodeTransactionDataToJsonObj(*txDataBytes);
    BOOST_CHECK(json.find("0x6849f21d1e455e9f0712b1e99fa4fcd23758e8f1") != std::string::npos);
    auto newTxData = std::make_unique<TransactionData>();
    newTxData->readFromJsonString(json);
    auto hash2 = newTxData->hash(cryptoSuite->hashImpl()).hex();
    BOOST_CHECK_EQUAL(hash2, "0359a5588c5e9c9dcfd2f4ece850d6f4c41bc88e2c27cc051890f26ef0ef118f");

    // clang-format off
    auto rawJson = R"({"abi":"","blockLimit":501,"chainID":"chain0","from":"0x3d20a4e26f41b57c2061e520c825fbfa5f321f22","groupID":"group0","hash":"0x0359a5588c5e9c9dcfd2f4ece850d6f4c41bc88e2c27cc051890f26ef0ef118f","importTime":1670467885565,"input":"0x2fe99bdc000000000000000000000000000000000000000000000000000000000000006000000000000000000000000000000000000000000000000000000000000000a000000000000000000000000000000000000000000000000000000000000000e0000000000000000000000000000000000000000000000000000000000000000574657374310000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000005746573743200000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000057465737433000000000000000000000000000000000000000000000000000000","nonce":"82456994196425209985513938878262806289094428076435270713862099506789849517246","signature":"0xd752fbe3218d6f0d4bdcbaf45875bba4a52f9af263badabc0ef93fa31f28d79b0779728b594c1212613a38daea8bdd36670777100b159ac537dfbd83e700d20901","to":"0x6849f21d1e455e9f0712b1e99fa4fcd23758e8f1","version":0})";
    // clang-format on
    auto newJsonTx = txBuilder->createTransactionDataWithJson(rawJson);
    auto hash3 = newJsonTx->hash(cryptoSuite->hashImpl()).hex();
    BOOST_CHECK_EQUAL(hash3, "0359a5588c5e9c9dcfd2f4ece850d6f4c41bc88e2c27cc051890f26ef0ef118f");
}

BOOST_AUTO_TEST_CASE(test_receipt)
{
    auto receiptBuilder = std::make_unique<ReceiptBuilder>();
    bcos::bytes output(bcos::asBytes(""));

    auto receiptData = receiptBuilder->createReceiptData(
        "24363", "0102e8b6fc8cdf9626fddc1c3ea8c1e79b3fce94", output, 9);

    auto cryptoSuite =
        std::make_shared<bcos::crypto::CryptoSuite>(std::make_shared<bcos::crypto::Keccak256>(),
            std::make_shared<bcos::crypto::Secp256k1Crypto>(), nullptr);

    auto hash = receiptData->hash(cryptoSuite->hashImpl()).hex();
    BOOST_CHECK_EQUAL(hash, "296b4598a56d6e2a295e5fe913e6c55459bef0c290f0e713744be8ade2ceec51");
    auto hash2 =
        receiptBuilder->calculateReceiptDataHash(CryptoType::Secp256K1, *receiptData).hex();
    BOOST_CHECK_EQUAL(hash2, "296b4598a56d6e2a295e5fe913e6c55459bef0c290f0e713744be8ade2ceec51");

    auto receiptDataByte = receiptBuilder->encodeReceipt(*receiptData);
    auto json = receiptBuilder->decodeReceiptDataToJsonObj(*receiptDataByte);
    BOOST_CHECK(json.find("0102e8b6fc8cdf9626fddc1c3ea8c1e79b3fce94") != std::string::npos);
    auto newReceipt = std::make_unique<TransactionReceiptData>();
    newReceipt->readFromJsonString(json);
    auto hash3 = newReceipt->hash(cryptoSuite->hashImpl()).hex();
    BOOST_CHECK_EQUAL(hash3, "296b4598a56d6e2a295e5fe913e6c55459bef0c290f0e713744be8ade2ceec51");

    // clang-format off
    auto rawJson = R"({"blockNumber":2,"checksumContractAddress":"","contractAddress":"","from":"0x3d20a4e26f41b57c2061e520c825fbfa5f321f22","gasUsed":"19413","hash":"0xb59cfe6ef607b72a6bab515042e0882213d179bd421afba353e2259b2a6396e4","input":"0x2fe99bdc000000000000000000000000000000000000000000000000000000000000006000000000000000000000000000000000000000000000000000000000000000a000000000000000000000000000000000000000000000000000000000000000e0000000000000000000000000000000000000000000000000000000000000000574657374310000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000005746573743200000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000057465737433000000000000000000000000000000000000000000000000000000","logEntries":[{"address":"6849f21d1e455e9f0712b1e99fa4fcd23758e8f1","data":"0x0000000000000000000000000000000000000000000000000000000000000001","topics":["0xc57b01fa77f41df77eaab79a0e2623fab2e7ae3e9530d9b1cab225ad65f2b7ce"]}],"message":"","output":"0x0000000000000000000000000000000000000000000000000000000000000001","status":0,"to":"0x6849f21d1e455e9f0712b1e99fa4fcd23758e8f1","transactionHash":"0x0359a5588c5e9c9dcfd2f4ece850d6f4c41bc88e2c27cc051890f26ef0ef118f","transactionProof":null,"version":0})";
    // clang-format on
    auto newJsonReceipt = receiptBuilder->createReceiptDataWithJson(rawJson);
    auto hash4 = newJsonReceipt->hash(cryptoSuite->hashImpl()).hex();
    BOOST_CHECK_EQUAL(hash4, "b59cfe6ef607b72a6bab515042e0882213d179bd421afba353e2259b2a6396e4");
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test