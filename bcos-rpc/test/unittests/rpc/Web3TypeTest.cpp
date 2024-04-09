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
    BOOST_CHECK(tx.type == TransactionType::Legacy);
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
    BOOST_CHECK(tx.type == TransactionType::EIP2930);
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
    BOOST_CHECK(tx.type == TransactionType::EIP1559);
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
    BOOST_CHECK(tx.type == TransactionType::EIP4844);
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
BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test