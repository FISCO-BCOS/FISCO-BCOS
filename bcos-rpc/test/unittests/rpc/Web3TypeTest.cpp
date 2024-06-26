/**
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file Web3TypeTest.cpp
 * @author: kyonGuo
 * @date 2024/4/9
 */

#include "../common/RPCFixture.h"
#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-rpc/web3jsonrpc/model/Web3Transaction.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>

using namespace bcos;
using namespace bcos::rpc;
using namespace bcos::codec::rlp;
namespace bcos::test
{
static const std::vector<AccessListEntry> s_accessList{
    {Address("0xde0b295669a9fd93d5f28d9ec85e40f4cb697bae"),
        {
            HashType("0x0000000000000000000000000000000000000000000000000000000000000003"),
            HashType("0x0000000000000000000000000000000000000000000000000000000000000007"),
        }},
    {Address("0xbb9bc244d798123fde783fcc1c72d3bb8c189413"), {}},
};
BOOST_FIXTURE_TEST_SUITE(testWeb3Type, RPCFixture)
BOOST_AUTO_TEST_CASE(testLegacyTransactionDecode)
{
    // clang-format off
    constexpr std::string_view  rawTx = "0xf89b0c8504a817c80082520894727fc6a68321b754475c668a6abfb6e9e71c169a888ac7230489e80000afa9059cbb000000000213ed0f886efd100b67c7e4ec0a85a7d20dc971600000000000000000000015af1d78b58c400026a0be67e0a07db67da8d446f76add590e54b6e92cb6b8f9835aeb67540579a27717a02d690516512020171c1ec870f6ff45398cc8609250326be89915fb538e7bd718";
    // constexpr std::string_view  rawTx = "0xf8ac82017c8504a817c800835fefd89409d07ecb4d6f32e91503c04b192e3bdeb7f857f480b8442c7128d700000000000000000000000000000000000000000000000000009bc24e89949a00000000000000000000000000000000000000000000000000000000000000001ba0cd372eb41b6b4e9e576233bb29c1492e0329fac1331f492a69e4a1b586a1a28ba032950cc4184ca8b0d45b24d13345157b4153d7ccc0d187dbab018be07726d186";
    // clang-format on
    auto bytes = fromHexWithPrefix(rawTx);
    auto bRef = bcos::ref(bytes);
    Web3Transaction tx{};
    auto e = codec::rlp::decode(bRef, tx);
    BOOST_CHECK(!e);
    BOOST_CHECK(tx.type == rpc::TransactionType::Legacy);
    BOOST_CHECK(tx.chainId.has_value());
    BOOST_CHECK_EQUAL(tx.chainId.value(), 1);
    BOOST_CHECK_EQUAL(tx.nonce, 12);
    BOOST_CHECK_EQUAL(tx.nonce, 12);
    BOOST_CHECK_EQUAL(tx.maxPriorityFeePerGas, 20000000000);
    BOOST_CHECK_EQUAL(tx.maxFeePerGas, 20000000000);
    BOOST_CHECK_EQUAL(tx.gasLimit, 21000);
    BOOST_CHECK_EQUAL(tx.to, Address("0x727fc6a68321b754475c668a6abfb6e9e71c169a"));
    BOOST_CHECK_EQUAL(tx.value, 10000000000000000000ull);
    BOOST_CHECK_EQUAL(toHex(tx.data),
        "a9059cbb000000000213ed0f886efd100b67c7e4ec0a85a7d20dc9"
        "71600000000000000000000015af1d78b58c4000");
    BOOST_CHECK_EQUAL(
        toHex(tx.signatureR), "be67e0a07db67da8d446f76add590e54b6e92cb6b8f9835aeb67540579a27717");
    BOOST_CHECK_EQUAL(
        toHex(tx.signatureS), "2d690516512020171c1ec870f6ff45398cc8609250326be89915fb538e7bd718");
    BOOST_CHECK_EQUAL(tx.getSignatureV(), tx.chainId.value() * 2 + 35 + 1);

    auto hash = tx.txHash().hexPrefixed();
    BOOST_CHECK_EQUAL(hash, bcos::crypto::keccak256Hash(ref(bytes)).hexPrefixed());
    bcos::bytes encoded{};
    codec::rlp::encode(encoded, tx);
    auto rawTx2 = toHexStringWithPrefix(encoded);
    BOOST_CHECK_EQUAL(rawTx, rawTx2);
}

BOOST_AUTO_TEST_CASE(testEIP2930Transaction)
{
    // clang-format off
    constexpr std::string_view rawTx = "0x01f8f205078506fc23ac008357b58494811a752c8cd697e3cb27279c330ed1ada745a8d7881bc16d674ec80000906ebaf477f83e051589c1188bcc6ddccdf872f85994de0b295669a9fd93d5f28d9ec85e40f4cb697baef842a00000000000000000000000000000000000000000000000000000000000000003a00000000000000000000000000000000000000000000000000000000000000007d694bb9bc244d798123fde783fcc1c72d3bb8c189413c080a036b241b061a36a32ab7fe86c7aa9eb592dd59018cd0443adc0903590c16b02b0a05edcc541b4741c5cc6dd347c5ed9577ef293a62787b4510465fadbfe39ee4094";
    // clang-format on
    auto bytes = fromHexWithPrefix(rawTx);
    auto bRef = bcos::ref(bytes);
    Web3Transaction tx{};
    auto e = codec::rlp::decode(bRef, tx);
    BOOST_CHECK(e == nullptr);
    BOOST_CHECK(tx.type == rpc::TransactionType::EIP2930);
    BOOST_CHECK(tx.chainId.has_value());
    BOOST_CHECK_EQUAL(tx.chainId.value(), 5);
    BOOST_CHECK_EQUAL(tx.nonce, 7);
    BOOST_CHECK_EQUAL(tx.maxPriorityFeePerGas, 30000000000);
    BOOST_CHECK_EQUAL(tx.maxFeePerGas, 30000000000);
    BOOST_CHECK_EQUAL(tx.gasLimit, 5748100);
    BOOST_CHECK_EQUAL(tx.to, Address("0x811a752c8cd697e3cb27279c330ed1ada745a8d7"));
    BOOST_CHECK_EQUAL(tx.value, 2000000000000000000ull);
    BOOST_CHECK_EQUAL(toHex(tx.data), "6ebaf477f83e051589c1188bcc6ddccd");
    BOOST_CHECK_EQUAL(tx.getSignatureV(), tx.chainId.value() * 2 + 35);
    BOOST_CHECK_EQUAL(
        toHex(tx.signatureR), "36b241b061a36a32ab7fe86c7aa9eb592dd59018cd0443adc0903590c16b02b0");
    BOOST_CHECK_EQUAL(
        toHex(tx.signatureS), "5edcc541b4741c5cc6dd347c5ed9577ef293a62787b4510465fadbfe39ee4094");
    BOOST_CHECK(tx.accessList == s_accessList);

    bcos::bytes encoded{};
    codec::rlp::encode(encoded, tx);
    auto rawTx2 = toHexStringWithPrefix(encoded);
    BOOST_CHECK_EQUAL(rawTx, rawTx2);
}

BOOST_AUTO_TEST_CASE(testEIP1559Transaction)
{
    // clang-format off
    constexpr std::string_view rawTx = "0x02f8f805078502540be4008506fc23ac008357b58494811a752c8cd697e3cb27279c330ed1ada745a8d7881bc16d674ec80000906ebaf477f83e051589c1188bcc6ddccdf872f85994de0b295669a9fd93d5f28d9ec85e40f4cb697baef842a00000000000000000000000000000000000000000000000000000000000000003a00000000000000000000000000000000000000000000000000000000000000007d694bb9bc244d798123fde783fcc1c72d3bb8c189413c080a036b241b061a36a32ab7fe86c7aa9eb592dd59018cd0443adc0903590c16b02b0a05edcc541b4741c5cc6dd347c5ed9577ef293a62787b4510465fadbfe39ee4094";
    // clang-format on
    auto bytes = fromHexWithPrefix(rawTx);
    auto bRef = bcos::ref(bytes);
    Web3Transaction tx{};
    auto e = codec::rlp::decode(bRef, tx);
    BOOST_CHECK(e == nullptr);
    BOOST_CHECK(tx.type == rpc::TransactionType::EIP1559);
    BOOST_CHECK(tx.chainId.has_value());
    BOOST_CHECK_EQUAL(tx.chainId.value(), 5);
    BOOST_CHECK_EQUAL(tx.nonce, 7);
    BOOST_CHECK_EQUAL(tx.maxPriorityFeePerGas, 10000000000);
    BOOST_CHECK_EQUAL(tx.maxFeePerGas, 30000000000);
    BOOST_CHECK_EQUAL(tx.gasLimit, 5748100);
    BOOST_CHECK_EQUAL(tx.to, Address("0x811a752c8cd697e3cb27279c330ed1ada745a8d7"));
    BOOST_CHECK_EQUAL(tx.value, 2000000000000000000ull);
    BOOST_CHECK_EQUAL(toHex(tx.data), "6ebaf477f83e051589c1188bcc6ddccd");
    BOOST_CHECK_EQUAL(tx.getSignatureV(), tx.chainId.value() * 2 + 35);
    BOOST_CHECK_EQUAL(
        toHex(tx.signatureR), "36b241b061a36a32ab7fe86c7aa9eb592dd59018cd0443adc0903590c16b02b0");
    BOOST_CHECK_EQUAL(
        toHex(tx.signatureS), "5edcc541b4741c5cc6dd347c5ed9577ef293a62787b4510465fadbfe39ee4094");
    BOOST_CHECK(tx.accessList == s_accessList);

    bcos::bytes encoded{};
    codec::rlp::encode(encoded, tx);
    auto rawTx2 = toHexStringWithPrefix(encoded);
    BOOST_CHECK_EQUAL(rawTx, rawTx2);
}

BOOST_AUTO_TEST_CASE(testEIP4844Transaction)
{
    // clang-format off
    constexpr std::string_view rawTx = "0x03f9012705078502540be4008506fc23ac008357b58494811a752c8cd697e3cb27279c330ed1ada745a8d7808204f7f872f85994de0b295669a9fd93d5f28d9ec85e40f4cb697baef842a00000000000000000000000000000000000000000000000000000000000000003a00000000000000000000000000000000000000000000000000000000000000007d694bb9bc244d798123fde783fcc1c72d3bb8c189413c07bf842a0c6bdd1de713471bd6cfa62dd8b5a5b42969ed09e26212d3377f3f8426d8ec210a08aaeccaf3873d07cef005aca28c39f8a9f8bdb1ec8d79ffc25afc0a4fa2ab73601a036b241b061a36a32ab7fe86c7aa9eb592dd59018cd0443adc0903590c16b02b0a05edcc541b4741c5cc6dd347c5ed9577ef293a62787b4510465fadbfe39ee4094";
    // clang-format on
    auto bytes = fromHexWithPrefix(rawTx);
    auto bRef = bcos::ref(bytes);
    Web3Transaction tx{};
    auto e = codec::rlp::decode(bRef, tx);
    BOOST_CHECK(e == nullptr);
    BOOST_CHECK(tx.type == rpc::TransactionType::EIP4844);
    BOOST_CHECK(tx.chainId.has_value());
    BOOST_CHECK_EQUAL(tx.chainId.value(), 5);
    BOOST_CHECK_EQUAL(tx.nonce, 7);
    BOOST_CHECK_EQUAL(tx.maxPriorityFeePerGas, 10000000000);
    BOOST_CHECK_EQUAL(tx.maxFeePerGas, 30000000000);
    BOOST_CHECK_EQUAL(tx.gasLimit, 5748100);
    BOOST_CHECK_EQUAL(tx.to, Address("0x811a752c8cd697e3cb27279c330ed1ada745a8d7"));
    BOOST_CHECK_EQUAL(tx.value, 0);
    BOOST_CHECK_EQUAL(toHex(tx.data), "04f7");
    BOOST_CHECK_EQUAL(tx.maxFeePerBlobGas, 123);
    BOOST_CHECK_EQUAL(tx.blobVersionedHashes.size(), 2);
    BOOST_CHECK_EQUAL(toHex(tx.blobVersionedHashes[0]),
        "c6bdd1de713471bd6cfa62dd8b5a5b42969ed09e26212d3377f3f8426d8ec210");
    BOOST_CHECK_EQUAL(toHex(tx.blobVersionedHashes[1]),
        "8aaeccaf3873d07cef005aca28c39f8a9f8bdb1ec8d79ffc25afc0a4fa2ab736");
    BOOST_CHECK_EQUAL(tx.getSignatureV(), tx.chainId.value() * 2 + 35 + 1);
    BOOST_CHECK_EQUAL(
        toHex(tx.signatureR), "36b241b061a36a32ab7fe86c7aa9eb592dd59018cd0443adc0903590c16b02b0");
    BOOST_CHECK_EQUAL(
        toHex(tx.signatureS), "5edcc541b4741c5cc6dd347c5ed9577ef293a62787b4510465fadbfe39ee4094");
    BOOST_CHECK(tx.accessList == s_accessList);

    bcos::bytes encoded{};
    codec::rlp::encode(encoded, tx);
    auto rawTx2 = toHexStringWithPrefix(encoded);
    BOOST_CHECK_EQUAL(rawTx, rawTx2);
}

BOOST_AUTO_TEST_CASE(recoverAddress)
{
    // clang-format off
    constexpr std::string_view  rawTx = "0xf8ac82017c8504a817c800835fefd89409d07ecb4d6f32e91503c04b192e3bdeb7f857f480b8442c7128d700000000000000000000000000000000000000000000000000009bc24e89949a00000000000000000000000000000000000000000000000000000000000000001ba0cd372eb41b6b4e9e576233bb29c1492e0329fac1331f492a69e4a1b586a1a28ba032950cc4184ca8b0d45b24d13345157b4153d7ccc0d187dbab018be07726d186";
    // clang-format on
    auto bytes = fromHexWithPrefix(rawTx);
    auto bRef = bcos::ref(bytes);
    Web3Transaction tx{};
    auto e = codec::rlp::decode(bRef, tx);
    BOOST_CHECK(!e);
    BOOST_CHECK(tx.type == rpc::TransactionType::Legacy);
    bcos::bytes encoded{};
    codec::rlp::encode(encoded, tx);
    auto rawTx2 = toHexStringWithPrefix(encoded);
    BOOST_CHECK_EQUAL(rawTx, rawTx2);

    auto encodeForSign = tx.encodeForSign();
    bcos::bytes sign{};
    sign.insert(sign.end(), tx.signatureR.begin(), tx.signatureR.end());
    sign.insert(sign.end(), tx.signatureS.begin(), tx.signatureS.end());
    sign.push_back(tx.signatureV);
    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    auto hash = bcos::crypto::keccak256Hash(ref(encodeForSign));
    auto signatureImpl = std::make_shared<bcos::crypto::Secp256k1Crypto>();
    auto [re, addr] = signatureImpl->recoverAddress(*hashImpl, hash, ref(sign));
    BOOST_CHECK(re);
    auto address = toHexStringWithPrefix(addr);
    BOOST_CHECK_EQUAL(address, "0xec5e7dec9d2d6bfa1f2221ace01ae3deb6906fb0");
}

BOOST_AUTO_TEST_CASE(EIP1559Recover)
{
    // https://etherscan.io/tx/0xcf6b53ec88659fc86e854af2e8453fa519ca261f949ef291e33c5f44ead870dc
    // clang-format off
    constexpr std::string_view rawTx = "0x02f8720183015b148085089a36ae8682520894e10f39a0dfb9e380b6d176eb7183af32b68028d78806e9ba3bd88b600080c080a032ab966d1c9cc2be6952713a1599a95a14f6e92c9f62d7fa40aa62d8b764ffcfa060bdbe7b8e66a0c681a90d4da0c7c0a4ba9321d49fc5c65bfddb847539e35d56";
    // clang-format on
    auto bytes = fromHexWithPrefix(rawTx);
    auto bRef = bcos::ref(bytes);
    Web3Transaction tx{};
    auto e = codec::rlp::decode(bRef, tx);
    BOOST_CHECK(e == nullptr);
    BOOST_CHECK(tx.type == rpc::TransactionType::EIP1559);
    BOOST_CHECK(tx.chainId.has_value());
    BOOST_CHECK_EQUAL(tx.chainId.value(), 1);
    BOOST_CHECK_EQUAL(tx.nonce, 88852);
    BOOST_CHECK_EQUAL(tx.maxPriorityFeePerGas, 0);
    BOOST_CHECK_EQUAL(tx.maxFeePerGas, 36947013254u);
    BOOST_CHECK_EQUAL(tx.gasLimit, 21000);
    BOOST_CHECK_EQUAL(tx.to.hexPrefixed(), "0xe10f39a0dfb9e380b6d176eb7183af32b68028d7");
    BOOST_CHECK_EQUAL(tx.value, 498134000000000000ull);
    BOOST_CHECK_EQUAL(toHex(tx.data), "");
    BOOST_CHECK_EQUAL(tx.getSignatureV(), tx.chainId.value() * 2 + 35);
    BOOST_CHECK_EQUAL(tx.txHash().hexPrefixed(),
        "0xcf6b53ec88659fc86e854af2e8453fa519ca261f949ef291e33c5f44ead870dc");
    auto txHash = bcos::crypto::keccak256Hash(ref(bytes)).hexPrefixed();
    BOOST_CHECK_EQUAL(txHash, "0xcf6b53ec88659fc86e854af2e8453fa519ca261f949ef291e33c5f44ead870dc");
    bcos::bytes encoded{};
    codec::rlp::encode(encoded, tx);
    auto rawTx2 = toHexStringWithPrefix(encoded);
    BOOST_CHECK_EQUAL(rawTx, rawTx2);

    auto encodeForSign = tx.encodeForSign();
    bcos::bytes sign{};
    sign.insert(sign.end(), tx.signatureR.begin(), tx.signatureR.end());
    sign.insert(sign.end(), tx.signatureS.begin(), tx.signatureS.end());
    sign.push_back(tx.signatureV);
    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    auto hash = bcos::crypto::keccak256Hash(ref(encodeForSign));
    auto signatureImpl = std::make_shared<bcos::crypto::Secp256k1Crypto>();
    auto [re, addr] = signatureImpl->recoverAddress(*hashImpl, hash, ref(sign));
    BOOST_CHECK(re);
    auto address = toHexStringWithPrefix(addr);
    BOOST_CHECK_EQUAL(address, "0x595063172c85b1e8ac2fe74fcb6b7dc26844cc2d");
}

BOOST_AUTO_TEST_CASE(EIP4844Recover)
{
    // https://etherscan.io/tx/0x8bb97c1480b533396b0940a0f94ef5974c4989954f52d928e06e38d363bbd560
    // clang-format off
    constexpr std::string_view rawTx = "0x03f9049f0183082ef8843b9aca008537942bfdb083036a2b941c479675ad559dc151f6ec7ed3fbf8cee79582b680b8a43e5aa082000000000000000000000000000000000000000000000000000000000008f7060000000000000000000000000000000000000000000000000000000000168763000000000000000000000000e64a54e2533fd126c2e452c5fab544d80e2e4eb5000000000000000000000000000000000000000000000000000000000a8cc7c7000000000000000000000000000000000000000000000000000000000a8ccabef902c0f8dd941c479675ad559dc151f6ec7ed3fbf8cee79582b6f8c6a00000000000000000000000000000000000000000000000000000000000000000a00000000000000000000000000000000000000000000000000000000000000001a0000000000000000000000000000000000000000000000000000000000000000aa0b53127684a568b3173ae13b9f8a6016e243e63b6e8ee1178d6a717850b5d6103a0360894a13ba1a3210667c828492db98dca3e2076cc3735a920a3ca505d382bbca0a10aa54071443520884ed767b0684edf43acec528b7da83ab38ce60126562660f90141948315177ab297ba92a06054ce80a67ed4dbd7ed3af90129a00000000000000000000000000000000000000000000000000000000000000006a00000000000000000000000000000000000000000000000000000000000000007a00000000000000000000000000000000000000000000000000000000000000009a0000000000000000000000000000000000000000000000000000000000000000aa0b53127684a568b3173ae13b9f8a6016e243e63b6e8ee1178d6a717850b5d6103a0360894a13ba1a3210667c828492db98dca3e2076cc3735a920a3ca505d382bbca0a66cc928b5edb82af9bd49922954155ab7b0942694bea4ce44661d9a873fbd8da0a66cc928b5edb82af9bd49922954155ab7b0942694bea4ce44661d9a873fbd8ea0f652222313e28459528d920b65115c16c04f3efc82aaedc97be59f3f379294a1f89b94e64a54e2533fd126c2e452c5fab544d80e2e4eb5f884a00000000000000000000000000000000000000000000000000000000000000004a00000000000000000000000000000000000000000000000000000000000000005a0e85fd79f89ff278fc57d40aecb7947873df9f0beac531c8f71a98f630e1eab62a07686888b19bb7b75e46bb1aa328b65150743f4899443d722f0adf8e252ccda410af8c6a001f8198b33db3461035e1621dd12498e57cf26efe9578b39054fbe5efdf83032a00152295a881b358db5dcf58b54661ee60f595de7f57eb93030a5d9e57bcae30ea0014ea3a3d4fc547ccb6974c5c4deb7778b755b0b3d56be88c54ef3a39d209b4ca001b378a4a2a4a3806740ec38b5672d213c78bbcae34550d014a265fc262fe06ea001b83eca80127748b71bcaa6a8c9edbfd5a9fb47933032891c27e07668f48867a001904e6186ecd84f6897659777846d5510bfbeb2863a93d8432f0fcf89c3e2c901a028bc2660c742d25de1f9af5550bfb734ac81c1e0d703c285447684872430635aa01788719406012ded6dd859a3a0218cb0acccd3f30a93da6796abc19ba3192fcf";
    // clang-format on
    auto bytes = fromHexWithPrefix(rawTx);
    auto bRef = bcos::ref(bytes);
    Web3Transaction tx{};
    auto e = codec::rlp::decode(bRef, tx);
    BOOST_CHECK(e == nullptr);
    BOOST_CHECK(tx.type == rpc::TransactionType::EIP4844);
    BOOST_CHECK(tx.chainId.has_value());
    BOOST_CHECK_EQUAL(tx.chainId.value(), 1);
    BOOST_CHECK_EQUAL(tx.nonce, 536312);
    BOOST_CHECK_EQUAL(tx.maxPriorityFeePerGas, 1000000000);
    BOOST_CHECK_EQUAL(tx.maxFeePerGas, 238709112240);
    BOOST_CHECK_EQUAL(tx.gasLimit, 223787);
    BOOST_CHECK_EQUAL(tx.to.hexPrefixed(), "0x1c479675ad559dc151f6ec7ed3fbf8cee79582b6");
    BOOST_CHECK_EQUAL(tx.value, 0);
    // clang-format off
    BOOST_CHECK_EQUAL(toHex(tx.data), "3e5aa082000000000000000000000000000000000000000000000000000000000008f7060000000000000000000000000000000000000000000000000000000000168763000000000000000000000000e64a54e2533fd126c2e452c5fab544d80e2e4eb5000000000000000000000000000000000000000000000000000000000a8cc7c7000000000000000000000000000000000000000000000000000000000a8ccabe");
    // clang-format on
    BOOST_CHECK_EQUAL(tx.getSignatureV(), tx.chainId.value() * 2 + 36);
    BOOST_CHECK_EQUAL(tx.txHash().hexPrefixed(),
        "0x8bb97c1480b533396b0940a0f94ef5974c4989954f52d928e06e38d363bbd560");

    BOOST_CHECK_EQUAL(tx.blobVersionedHashes.size(), 6);
    BOOST_CHECK_EQUAL(toHex(tx.blobVersionedHashes[0]),
        "01f8198b33db3461035e1621dd12498e57cf26efe9578b39054fbe5efdf83032");
    BOOST_CHECK_EQUAL(toHex(tx.blobVersionedHashes[1]),
        "0152295a881b358db5dcf58b54661ee60f595de7f57eb93030a5d9e57bcae30e");
    BOOST_CHECK_EQUAL(toHex(tx.blobVersionedHashes[2]),
        "014ea3a3d4fc547ccb6974c5c4deb7778b755b0b3d56be88c54ef3a39d209b4c");
    BOOST_CHECK_EQUAL(toHex(tx.blobVersionedHashes[3]),
        "01b378a4a2a4a3806740ec38b5672d213c78bbcae34550d014a265fc262fe06e");
    BOOST_CHECK_EQUAL(toHex(tx.blobVersionedHashes[4]),
        "01b83eca80127748b71bcaa6a8c9edbfd5a9fb47933032891c27e07668f48867");
    BOOST_CHECK_EQUAL(toHex(tx.blobVersionedHashes[5]),
        "01904e6186ecd84f6897659777846d5510bfbeb2863a93d8432f0fcf89c3e2c9");

    BOOST_CHECK_EQUAL(tx.accessList.size(), 3);
    BOOST_CHECK_EQUAL(
        tx.accessList[0].account.hexPrefixed(), "0x1c479675ad559dc151f6ec7ed3fbf8cee79582b6");
    BOOST_CHECK_EQUAL(tx.accessList[0].storageKeys.size(), 6);
    BOOST_CHECK_EQUAL(toHex(tx.accessList[0].storageKeys[5]),
        "a10aa54071443520884ed767b0684edf43acec528b7da83ab38ce60126562660");
    BOOST_CHECK_EQUAL(
        tx.accessList[1].account.hexPrefixed(), "0x8315177ab297ba92a06054ce80a67ed4dbd7ed3a");
    BOOST_CHECK_EQUAL(tx.accessList[1].storageKeys.size(), 9);
    BOOST_CHECK_EQUAL(
        tx.accessList[2].account.hexPrefixed(), "0xe64a54e2533fd126c2e452c5fab544d80e2e4eb5");
    BOOST_CHECK_EQUAL(tx.accessList[2].storageKeys.size(), 4);

    bcos::bytes encoded{};
    codec::rlp::encode(encoded, tx);
    auto rawTx2 = toHexStringWithPrefix(encoded);
    BOOST_CHECK_EQUAL(rawTx, rawTx2);

    auto encodeForSign = tx.encodeForSign();
    bcos::bytes sign{};
    sign.insert(sign.end(), tx.signatureR.begin(), tx.signatureR.end());
    sign.insert(sign.end(), tx.signatureS.begin(), tx.signatureS.end());
    sign.push_back(tx.signatureV);
    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    auto hash = bcos::crypto::keccak256Hash(ref(encodeForSign));
    auto signatureImpl = std::make_shared<bcos::crypto::Secp256k1Crypto>();
    auto [re, addr] = signatureImpl->recoverAddress(*hashImpl, hash, ref(sign));
    BOOST_CHECK(re);
    auto address = toHexStringWithPrefix(addr);
    BOOST_CHECK_EQUAL(address, "0xc1b634853cb333d3ad8663715b08f41a3aec47cc");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test