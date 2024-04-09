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
BOOST_FIXTURE_TEST_SUITE(testWeb3Type, RPCFixture)
BOOST_AUTO_TEST_CASE(testTransactionDecode)
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
BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test