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
#include <bcos-cpp-sdk/utilities/tx/TransactionBuilderV1.h>
#include <bcos-cpp-sdk/utilities/tx/TransactionBuilderV2.h>
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

    auto hash = txBuilder->calculateTransactionDataHash(CryptoType::Secp256K1, *txData).hex();
    BOOST_CHECK_EQUAL(hash, "0359a5588c5e9c9dcfd2f4ece850d6f4c41bc88e2c27cc051890f26ef0ef118f");

    auto txDataBytes = txBuilder->encodeTransactionData(*txData);
    auto txHash = txBuilder->calculateTransactionDataHash(CryptoType::Secp256K1, *txData).hex();

    BOOST_CHECK_EQUAL(txHash, "0359a5588c5e9c9dcfd2f4ece850d6f4c41bc88e2c27cc051890f26ef0ef118f");

    auto json = txBuilder->decodeTransactionDataToJsonObj(*txDataBytes);
    BOOST_CHECK(json.find("0x6849f21d1e455e9f0712b1e99fa4fcd23758e8f1") != std::string::npos);
    auto newTxData = TarsTransactionDataReadFromJsonString(json);

    auto hash2 = txBuilder->calculateTransactionDataHash(CryptoType::Secp256K1, *newTxData).hex();
    BOOST_CHECK_EQUAL(hash2, "0359a5588c5e9c9dcfd2f4ece850d6f4c41bc88e2c27cc051890f26ef0ef118f");

    // clang-format off
    auto rawJson = R"({"abi":"","blockLimit":501,"chainID":"chain0","from":"0x3d20a4e26f41b57c2061e520c825fbfa5f321f22","groupID":"group0","hash":"0x0359a5588c5e9c9dcfd2f4ece850d6f4c41bc88e2c27cc051890f26ef0ef118f","importTime":1670467885565,"input":"0x2fe99bdc000000000000000000000000000000000000000000000000000000000000006000000000000000000000000000000000000000000000000000000000000000a000000000000000000000000000000000000000000000000000000000000000e0000000000000000000000000000000000000000000000000000000000000000574657374310000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000005746573743200000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000057465737433000000000000000000000000000000000000000000000000000000","nonce":"82456994196425209985513938878262806289094428076435270713862099506789849517246","signature":"0xd752fbe3218d6f0d4bdcbaf45875bba4a52f9af263badabc0ef93fa31f28d79b0779728b594c1212613a38daea8bdd36670777100b159ac537dfbd83e700d20901","to":"0x6849f21d1e455e9f0712b1e99fa4fcd23758e8f1","version":0})";
    // clang-format on
    auto newJsonTx = txBuilder->createTransactionDataWithJson(rawJson);
    auto hash3 = txBuilder->calculateTransactionDataHash(CryptoType::Secp256K1, *newJsonTx).hex();
    BOOST_CHECK_EQUAL(hash3, "0359a5588c5e9c9dcfd2f4ece850d6f4c41bc88e2c27cc051890f26ef0ef118f");
}

BOOST_AUTO_TEST_CASE(test_transaction_v1)
{
    auto cryptoSuite =
        std::make_shared<bcos::crypto::CryptoSuite>(std::make_shared<bcos::crypto::Keccak256>(),
            std::make_shared<bcos::crypto::Secp256k1Crypto>(), nullptr);

    auto fixedSec1 = h256(
        "722ecc9325297d801632938d7d1bd0a922f8db10e86f5953431686ad9e37db04f47aac0468c655e678dd85869e"
        "abaab14b034f79df97ccc6e9c7714d574aed89");
    auto sec1 = std::make_shared<bcos::crypto::KeyImpl>(fixedSec1.asBytes());
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto keyPair1 = signatureImpl->createKeyPair(sec1);
    TransactionBuilderV1 transactionBuilderV1;
    // clang-format off
    bcos::bytes  input = fromHex(std::string("1a10012606636861696e30360667726f7570304100855628313331343736363932313039343437323233313631323634333933363134333635383636353733317d00010426608060405234801561001057600080fd5b506040518060400160405280600d81526020017f48656c6c6f2c20576f726c6421000000000000000000000000000000000000008152506000908051906020019061005c929190610062565b50610107565b828054600181600116156101000203166002900490600052602060002090601f016020900481019282601f106100a357805160ff19168380011785556100d1565b828001600101855582156100d1579182015b828111156100d05782518255916020019190600101906100b5565b5b5090506100de91906100e2565b5090565b61010491905b808211156101005760008160009055506001016100e8565b5090565b90565b610310806101166000396000f3fe608060405234801561001057600080fd5b50600436106100365760003560e01c80634ed3885e1461003b5780636d4ce63c146100f6575b600080fd5b6100f46004803603602081101561005157600080fd5b810190808035906020019064010000000081111561006e57600080fd5b82018360208201111561008057600080fd5b803590602001918460018302840111640100000000831117156100a257600080fd5b91908080601f016020809104026020016040519081016040528093929190818152602001838380828437600081840152601f19601f820116905080830192505050505050509192919290505050610179565b005b6100fe610193565b6040518080602001828103825283818151815260200191508051906020019080838360005b8381101561013e578082015181840152602081019050610123565b50505050905090810190601f16801561016b5780820380516001836020036101000a031916815260200191505b509250505060405180910390f35b806000908051906020019061018f929190610235565b5050565b606060008054600181600116156101000203166002900480601f01602080910402602001604051908101604052809291908181526020018280546001816001161561010002031660029004801561022b5780601f106102005761010080835404028352916020019161022b565b820191906000526020600020905b81548152906001019060200180831161020e57829003601f168201915b5050505050905090565b828054600181600116156101000203166002900490600052602060002090601f016020900481019282601f1061027657805160ff19168380011785556102a4565b828001600101855582156102a4579182015b828111156102a3578251825591602001919060010190610288565b5b5090506102b191906102b5565b5090565b6102d791905b808211156102d35760008160009055506001016102bb565b5090565b9056fea2646970667358221220b5943f43c48cc93c6d71cdcf27aee5072566c88755ce9186e32ce83b24e8dc6c64736f6c634300060a0033b0010b2d000020f03b8fdfa96a9b2128a83c22ddc691e6f1d9e0589b7c34088675e33cb059dbb23d000041313563c18509553f1d83747c004f29ec67f1137bb1c3104e8551ecc4c057187609c58a9d0220f02971abcf3f9bc6ba44423de56cb435b20b1fcf5eee4587840b01"));
    // clang-format on
    auto tx = transactionBuilderV1.decodeTransaction(input);
    auto txJson = transactionBuilderV1.decodeTransactionToJsonObj(input);
    std::cout << txJson << std::endl;
    auto txData = tx->data;
    auto hash = transactionBuilderV1.calculateTransactionDataHash(CryptoType::Secp256K1, tx->data);


    auto hex1 = hash.hex();
    std::string hex = toHex(tx->dataHash);
    BOOST_CHECK(hex == hex1);
    BOOST_CHECK(hex == "f03b8fdfa96a9b2128a83c22ddc691e6f1d9e0589b7c34088675e33cb059dbb2");

    bytes txDataInput;
    txDataInput.insert(txDataInput.begin(), txData.input.begin(), txData.input.end());

    auto newTxData =
        transactionBuilderV1.createTransactionData(txData.version, txData.groupID, txData.chainID,
            txData.to, txData.nonce, txDataInput, txData.abi, txData.blockLimit, txData.value,
            txData.gasPrice, txData.gasLimit, txData.maxFeePerGas, txData.maxPriorityFeePerGas);

    auto newHash = transactionBuilderV1.TransactionBuilder::calculateTransactionDataHash(
        CryptoType::Secp256K1, *newTxData);
    BOOST_CHECK(newHash.hex() == hex);

    auto newHash2 = transactionBuilderV1.calculateTransactionDataHash(CryptoType::Secp256K1,
        txData.version, txData.groupID, txData.chainID, txData.to, txData.nonce, txDataInput,
        txData.abi, txData.blockLimit, txData.value, txData.gasPrice, txData.gasLimit,
        txData.maxFeePerGas, txData.maxPriorityFeePerGas);
    BOOST_CHECK(newHash2.hex() == hex);
}

BOOST_AUTO_TEST_CASE(test_transaction_v2)
{
    auto cryptoSuite =
        std::make_shared<bcos::crypto::CryptoSuite>(std::make_shared<bcos::crypto::Keccak256>(),
            std::make_shared<bcos::crypto::Secp256k1Crypto>(), nullptr);

    auto fixedSec1 = h256(
        "722ecc9325297d801632938d7d1bd0a922f8db10e86f5953431686ad9e37db04f47aac0468c655e678dd85869e"
        "abaab14b034f79df97ccc6e9c7714d574aed89");
    auto sec1 = std::make_shared<bcos::crypto::KeyImpl>(fixedSec1.asBytes());
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto keyPair1 = signatureImpl->createKeyPair(sec1);
    TransactionBuilderV2 transactionBuilderV2;
    // clang-format off
    bcos::bytes  input = fromHex(std::string("1a10012606636861696e30360667726f7570304100855628313331343736363932313039343437323233313631323634333933363134333635383636353733317d00010426608060405234801561001057600080fd5b506040518060400160405280600d81526020017f48656c6c6f2c20576f726c6421000000000000000000000000000000000000008152506000908051906020019061005c929190610062565b50610107565b828054600181600116156101000203166002900490600052602060002090601f016020900481019282601f106100a357805160ff19168380011785556100d1565b828001600101855582156100d1579182015b828111156100d05782518255916020019190600101906100b5565b5b5090506100de91906100e2565b5090565b61010491905b808211156101005760008160009055506001016100e8565b5090565b90565b610310806101166000396000f3fe608060405234801561001057600080fd5b50600436106100365760003560e01c80634ed3885e1461003b5780636d4ce63c146100f6575b600080fd5b6100f46004803603602081101561005157600080fd5b810190808035906020019064010000000081111561006e57600080fd5b82018360208201111561008057600080fd5b803590602001918460018302840111640100000000831117156100a257600080fd5b91908080601f016020809104026020016040519081016040528093929190818152602001838380828437600081840152601f19601f820116905080830192505050505050509192919290505050610179565b005b6100fe610193565b6040518080602001828103825283818151815260200191508051906020019080838360005b8381101561013e578082015181840152602081019050610123565b50505050905090810190601f16801561016b5780820380516001836020036101000a031916815260200191505b509250505060405180910390f35b806000908051906020019061018f929190610235565b5050565b606060008054600181600116156101000203166002900480601f01602080910402602001604051908101604052809291908181526020018280546001816001161561010002031660029004801561022b5780601f106102005761010080835404028352916020019161022b565b820191906000526020600020905b81548152906001019060200180831161020e57829003601f168201915b5050505050905090565b828054600181600116156101000203166002900490600052602060002090601f016020900481019282601f1061027657805160ff19168380011785556102a4565b828001600101855582156102a4579182015b828111156102a3578251825591602001919060010190610288565b5b5090506102b191906102b5565b5090565b6102d791905b808211156102d35760008160009055506001016102bb565b5090565b9056fea2646970667358221220b5943f43c48cc93c6d71cdcf27aee5072566c88755ce9186e32ce83b24e8dc6c64736f6c634300060a0033b0010b2d000020f03b8fdfa96a9b2128a83c22ddc691e6f1d9e0589b7c34088675e33cb059dbb23d000041313563c18509553f1d83747c004f29ec67f1137bb1c3104e8551ecc4c057187609c58a9d0220f02971abcf3f9bc6ba44423de56cb435b20b1fcf5eee4587840b01"));
    // clang-format on
    auto tx = transactionBuilderV2.decodeTransaction(input);
    auto txJson = transactionBuilderV2.decodeTransactionToJsonObj(input);
    std::cout << txJson << std::endl;
    auto txData = tx->data;
    auto hash = transactionBuilderV2.calculateTransactionDataHash(CryptoType::Secp256K1, tx->data);


    auto hex1 = hash.hex();
    std::string hex = toHex(tx->dataHash);
    BOOST_CHECK(hex == hex1);
    BOOST_CHECK(hex == "f03b8fdfa96a9b2128a83c22ddc691e6f1d9e0589b7c34088675e33cb059dbb2");

    bytes txDataInput;
    txDataInput.insert(txDataInput.begin(), txData.input.begin(), txData.input.end());

    auto newTxData =
        transactionBuilderV2.createTransactionData(txData.version, txData.groupID, txData.chainID,
            txData.to, txData.nonce, txDataInput, txData.abi, txData.blockLimit, txData.value,
            txData.gasPrice, txData.gasLimit, txData.maxFeePerGas, txData.maxPriorityFeePerGas);

    auto newHash = transactionBuilderV2.TransactionBuilder::calculateTransactionDataHash(
        CryptoType::Secp256K1, *newTxData);
    BOOST_CHECK(newHash.hex() == hex);

    auto newHash2 = transactionBuilderV2.calculateTransactionDataHash(CryptoType::Secp256K1,
        txData.version, txData.groupID, txData.chainID, txData.to, txData.nonce, txDataInput,
        txData.abi, txData.blockLimit, txData.value, txData.gasPrice, txData.gasLimit,
        txData.maxFeePerGas, txData.maxPriorityFeePerGas);
    BOOST_CHECK(newHash2.hex() == hex);


    auto [txHash, _] = transactionBuilderV2.createSignedTransaction(*keyPair1, txData);
    BOOST_CHECK(newHash2.hex() == toHex(txHash));

    // set extension
    std::string_view extension = "Hello World!";
    txData.extension.insert(txData.extension.begin(), extension.begin(), extension.end());
    txData.version = (uint32_t)protocol::TransactionVersion::V2_VERSION;
    auto [txHash2, signedTx] = transactionBuilderV2.createSignedTransaction(*keyPair1, txData);
    BOOST_CHECK(newHash2.hex() != toHex(txHash2));

    auto newTx = transactionBuilderV2.decodeTransaction(signedTx);
    BOOST_CHECK(newTx->data.extension == txData.extension);

    HashType v2Hash(txHash2);
    auto signature = transactionBuilderV2.signTransactionDataHash(*keyPair1, v2Hash);
    // wrong hash
    BOOST_CHECK_THROW(
        transactionBuilderV2.createSignedTransactionWithSign(*signature, newHash2, txData),
        std::invalid_argument);

    // wrong hash algorithm
    BOOST_CHECK_THROW(transactionBuilderV2.createSignedTransactionWithSign(
                          *signature, v2Hash, txData, 0, "", true, CryptoType::SM2),
        std::invalid_argument);

    auto newSignedTx =
        transactionBuilderV2.createSignedTransactionWithSign(*signature, v2Hash, txData);
    BOOST_CHECK(newSignedTx == signedTx);

    auto jsonTx = TarsTransactionWriteToJsonString(newTx);
    auto txDecodeFromJson = TarsTransactionReadFromJsonString(jsonTx);
    {
        auto txBytes1 = transactionBuilderV2.encodeTransaction(*txDecodeFromJson);
        auto txBytes2 = transactionBuilderV2.encodeTransaction(*newTx);
        BOOST_CHECK(toHex(*txBytes1) == toHex(*txBytes2));
    }
    auto jsonTxData = TarsTransactionDataWriteToJsonString(newTx->data);
    auto txDataDecodeFromJson = TarsTransactionDataReadFromJsonString(jsonTxData);
    {
        auto txDataBytes1 = transactionBuilderV2.encodeTransactionData(*txDataDecodeFromJson);
        auto txDataBytes2 = transactionBuilderV2.encodeTransactionData(newTx->data);
        BOOST_CHECK(toHex(*txDataBytes1) == toHex(*txDataBytes2));
    }
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

    auto hash = receiptBuilder->calculateReceiptDataHash(CryptoType::Secp256K1, *receiptData).hex();
    BOOST_CHECK_EQUAL(hash, "296b4598a56d6e2a295e5fe913e6c55459bef0c290f0e713744be8ade2ceec51");
    auto hash2 =
        receiptBuilder->calculateReceiptDataHash(CryptoType::Secp256K1, *receiptData).hex();
    BOOST_CHECK_EQUAL(hash2, "296b4598a56d6e2a295e5fe913e6c55459bef0c290f0e713744be8ade2ceec51");

    auto receiptDataByte = receiptBuilder->encodeReceipt(*receiptData);
    auto json = receiptBuilder->decodeReceiptDataToJsonObj(*receiptDataByte);
    BOOST_CHECK(json.find("0102e8b6fc8cdf9626fddc1c3ea8c1e79b3fce94") != std::string::npos);
    auto newReceipt = TarsReceiptDataReadFromJsonString(json);
    auto hash3 = receiptBuilder->calculateReceiptDataHash(CryptoType::Secp256K1, *newReceipt).hex();
    BOOST_CHECK_EQUAL(hash3, "296b4598a56d6e2a295e5fe913e6c55459bef0c290f0e713744be8ade2ceec51");

    // clang-format off
    auto rawJson = R"({"blockNumber":2,"checksumContractAddress":"","contractAddress":"","from":"0x3d20a4e26f41b57c2061e520c825fbfa5f321f22","gasUsed":"19413","hash":"0xb59cfe6ef607b72a6bab515042e0882213d179bd421afba353e2259b2a6396e4","input":"0x2fe99bdc000000000000000000000000000000000000000000000000000000000000006000000000000000000000000000000000000000000000000000000000000000a000000000000000000000000000000000000000000000000000000000000000e0000000000000000000000000000000000000000000000000000000000000000574657374310000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000005746573743200000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000057465737433000000000000000000000000000000000000000000000000000000","logEntries":[{"address":"6849f21d1e455e9f0712b1e99fa4fcd23758e8f1","data":"0x0000000000000000000000000000000000000000000000000000000000000001","topics":["0xc57b01fa77f41df77eaab79a0e2623fab2e7ae3e9530d9b1cab225ad65f2b7ce"]}],"message":"","output":"0x0000000000000000000000000000000000000000000000000000000000000001","status":0,"to":"0x6849f21d1e455e9f0712b1e99fa4fcd23758e8f1","transactionHash":"0x0359a5588c5e9c9dcfd2f4ece850d6f4c41bc88e2c27cc051890f26ef0ef118f","transactionProof":null,"version":0})";
    // clang-format on
    auto newJsonReceipt = receiptBuilder->createReceiptDataWithJson(rawJson);
    auto hash4 =
        receiptBuilder->calculateReceiptDataHash(CryptoType::Secp256K1, *newJsonReceipt).hex();
    BOOST_CHECK_EQUAL(hash4, "b59cfe6ef607b72a6bab515042e0882213d179bd421afba353e2259b2a6396e4");
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test