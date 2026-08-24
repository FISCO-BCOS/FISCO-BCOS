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
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>  // c_secp256k1n* + Secp256k1Crypto
#include <bcos-rlp-protocol/Web3TxEnvelope.h>  // web3ChainIdFromEnvelope (F4 unit tests)
#include <bcos-rpc/web3jsonrpc/model/Web3Transaction.h>
#include <bcos-rpc/web3jsonrpc/model/Web3TxHandler.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>

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
    BOOST_CHECK_EQUAL(tx.to.value(), Address("0x727fc6a68321b754475c668a6abfb6e9e71c169a"));
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

// kyonRay R3 #1: codec::rlp::encode(out, Web3Transaction) must APPEND to a non-empty buffer
// (the accumulate-into-buffer contract every other encode(out, x) overload provides; part-5
// rawTransactions list encoding depends on it). A regression to overwrite semantics would
// silently drop the first element.
BOOST_AUTO_TEST_CASE(testEncodeAppendsToNonEmptyBuffer)
{
    namespace rlp = bcos::codec::rlp;
    auto makeTx = [&](uint64_t nonce) {
        Web3Transaction tx;
        tx.type = rpc::TransactionType::Legacy;
        tx.chainId = 1;
        tx.nonce = nonce;
        tx.maxPriorityFeePerGas = bcos::u256(10);
        tx.maxFeePerGas = bcos::u256(10);
        tx.gasLimit = 21000;
        tx.to.emplace(Address("0x0100000000000000000000000000000000000001"));
        tx.value = bcos::u256(0);
        return tx;
    };
    auto a = makeTx(0);
    auto b = makeTx(1);

    bcos::bytes out;
    rlp::encode(out, a);
    auto aLen = out.size();
    rlp::encode(out, b);  // must append after a

    bcos::bytes aloneA;
    rlp::encode(aloneA, a);
    BOOST_REQUIRE_EQUAL(aLen, aloneA.size());
    // The second encode must not clobber the first element's bytes.
    BOOST_CHECK_EQUAL_COLLECTIONS(out.begin(), out.begin() + aLen, aloneA.begin(), aloneA.end());
    // And both elements are present (length is the sum).
    bcos::bytes aloneB;
    rlp::encode(aloneB, b);
    BOOST_CHECK_EQUAL(out.size(), aloneA.size() + aloneB.size());
}

// Legacy v values outside the legal set must be rejected with InvalidVInSignature:
// v=0/1 (previously silently accepted as a no-op) and v=29-34 (not 27/28, not >=35).
BOOST_AUTO_TEST_CASE(testLegacyInvalidVRejected)
{
    namespace rlp = bcos::codec::rlp;
    auto makeLegacy = [&](uint64_t v) {
        bcos::bytes items;
        rlp::encode(items, static_cast<uint64_t>(1));
        rlp::encode(items, static_cast<uint64_t>(10));
        rlp::encode(items, static_cast<uint64_t>(21000));
        rlp::encode(items, Address("0x0100000000000000000000000000000000000001"));
        rlp::encode(items, static_cast<uint64_t>(0));
        rlp::encode(items, bcos::bytes{});
        rlp::encode(items, v);
        rlp::encode(items, bcos::bytes(32, 0x11));  // r
        rlp::encode(items, bcos::bytes(32, 0x22));  // s
        bcos::bytes env;
        rlp::encodeHeader(env, {.isList = true, .payloadLength = items.size()});
        env.insert(env.end(), items.begin(), items.end());
        return env;
    };
    for (uint64_t v : {0ull, 1ull, 29ull, 30ull, 34ull})
    {
        auto bytes = makeLegacy(v);
        auto bRef = bcos::ref(bytes);
        Web3Transaction tx{};
        auto e = rlp::decode(bRef, tx);
        BOOST_REQUIRE(e != nullptr);
        BOOST_CHECK_EQUAL(
            e->errorCode(), static_cast<int64_t>(rlp::DecodingError::InvalidVInSignature));
    }
    // Control: v=27 (pre-EIP-155) and v=35 (chainId 0) decode fine.
    for (uint64_t v : {27ull, 35ull})
    {
        auto bytes = makeLegacy(v);
        auto bRef = bcos::ref(bytes);
        Web3Transaction tx{};
        auto e = rlp::decode(bRef, tx);
        BOOST_CHECK(e == nullptr);
    }
}

BOOST_AUTO_TEST_CASE(testConstructTx)
{
    Web3Transaction testTx;
    testTx.value = 1000000000000000000;
    testTx.type = rpc::TransactionType::Legacy;
    testTx.data = {};
    testTx.to = Address("0x1e58529dAA467406645d0f4B63dec96CA0b87d70");
    testTx.nonce = 19;
    testTx.gasLimit = 210000;
    testTx.maxFeePerGas = 20000000000;
    testTx.maxPriorityFeePerGas = 20000000000;
    testTx.chainId = 31337;

    auto signData = testTx.encodeForSign();
    std::string priv = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
    auto key = std::make_shared<KeyImpl>(fromHex(priv));
    auto newHash = crypto::keccak256Hash(ref(signData));
    auto signatureImpl = bcos::crypto::Secp256k1Crypto();
    auto keyPair = std::make_unique<Secp256k1KeyPair>(key);

    auto signature = signatureImpl.sign(*keyPair, newHash, false);
    auto [_, addr] = signatureImpl.recoverAddress(*hashImpl, newHash, ref(*signature));
    auto newAddr = toHex(addr);
    BOOST_CHECK_EQUAL(newAddr, Address("C96aAa54E2d44c299564da76e1cD3184A2386B8D").hex());
    testTx.signatureR = {signature->begin(), signature->begin() + 32};
    testTx.signatureS = {signature->begin() + 32, signature->begin() + 64};
    testTx.signatureV = signature->back();
    bcos::bytes toData;
    bcos::codec::rlp::encode(toData, testTx);
    auto const newTx = toHexStringWithPrefix(toData);
    BOOST_CHECK(!newTx.empty());
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
    BOOST_CHECK_EQUAL(tx.to.value(), Address("0x811a752c8cd697e3cb27279c330ed1ada745a8d7"));
    BOOST_CHECK_EQUAL(tx.value, 2000000000000000000ull);
    BOOST_CHECK_EQUAL(toHex(tx.data), "6ebaf477f83e051589c1188bcc6ddccd");
    BOOST_CHECK_EQUAL(tx.getSignatureV(), tx.chainId.value() * 2 + 35);
    // Typed-tx signatureV is the raw y_parity (0/1). This
    // EIP-2930 sample carries yParity=0.
    BOOST_CHECK_EQUAL(tx.signatureV, 0u);
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
    BOOST_CHECK_EQUAL(tx.to.value(), Address("0x811a752c8cd697e3cb27279c330ed1ada745a8d7"));
    BOOST_CHECK_EQUAL(tx.value, 2000000000000000000ull);
    BOOST_CHECK_EQUAL(toHex(tx.data), "6ebaf477f83e051589c1188bcc6ddccd");
    BOOST_CHECK_EQUAL(tx.getSignatureV(), tx.chainId.value() * 2 + 35);
    // Typed-tx signatureV is the raw y_parity (0/1). This
    // EIP-1559 sample carries yParity=0.
    BOOST_CHECK_EQUAL(tx.signatureV, 0u);
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
BOOST_AUTO_TEST_CASE(testEIP1559Transaction2)
{
    // clang-format off
    constexpr std::string_view rawTx = "0x02f90129824ee8090a82012c84014fb1808080b8d46080604052348015600e575f5ffd5b506040517fcf16a92280c1bbb43f72d31126b724d508df2877835849e8744017ab36a9b47f905f90a160928060425f395ff3fe6080604052348015600e575f5ffd5b50600436106026575f3560e01c80637b0cb83914602a575b5f5ffd5b60306032565b005b6040517fcf16a92280c1bbb43f72d31126b724d508df2877835849e8744017ab36a9b47f905f90a156fea2646970667358221220b26bf8d47ffaa4c5ffecf6303ac218970d8ab50724943980b859fc2ac8e384e164736f6c634300081c0033c080a0f0643ec9f740e363dfa8d0f902a643dd4828a04cd80dd733822f2dd636fb6693a053ad48b39f2bbb3b3a5e073074bcb1dd43d908b292ad9078d4478dc42f1195bd";
    // clang-format on
    auto bytes = fromHexWithPrefix(rawTx);
    auto bRef = bcos::ref(bytes);
    Web3Transaction tx{};
    auto e = codec::rlp::decode(bRef, tx);
    BOOST_CHECK(e == nullptr);
    BOOST_CHECK(tx.type == rpc::TransactionType::EIP1559);
    BOOST_CHECK(tx.chainId.has_value());
    BOOST_CHECK_EQUAL(tx.chainId.value(), 20200);
    BOOST_CHECK_EQUAL(tx.nonce, 9);
    BOOST_CHECK_EQUAL(tx.maxPriorityFeePerGas, 10);
    BOOST_CHECK_EQUAL(tx.maxFeePerGas, 300);
    BOOST_CHECK_EQUAL(tx.gasLimit, 22000000);
    BOOST_CHECK_EQUAL(tx.to.has_value(), false);
    BOOST_CHECK_EQUAL(tx.value, 0);
    BOOST_CHECK_EQUAL(toHex(tx.data),
        "6080604052348015600e575f5ffd5b506040517fcf16a92280c1bbb43f72d31126b724d508df2877835849e874"
        "4017ab36a9b47f905f90a160928060425f395ff3fe6080604052348015600e575f5ffd5b50600436106026575f"
        "3560e01c80637b0cb83914602a575b5f5ffd5b60306032565b005b6040517fcf16a92280c1bbb43f72d31126b7"
        "24d508df2877835849e8744017ab36a9b47f905f90a156fea2646970667358221220b26bf8d47ffaa4c5ffecf6"
        "303ac218970d8ab50724943980b859fc2ac8e384e164736f6c634300081c0033");
    BOOST_CHECK_EQUAL(tx.getSignatureV(), tx.chainId.value() * 2 + 35);
    BOOST_CHECK_EQUAL(
        toHex(tx.signatureR), "f0643ec9f740e363dfa8d0f902a643dd4828a04cd80dd733822f2dd636fb6693");
    BOOST_CHECK_EQUAL(
        toHex(tx.signatureS), "53ad48b39f2bbb3b3a5e073074bcb1dd43d908b292ad9078d4478dc42f1195bd");

    BOOST_CHECK_EQUAL(tx.sender(), "0x2a09be8823b80f337170650802d1a0f8a99fe2d8");
    BOOST_CHECK_EQUAL(tx.txHash().hexPrefixed(),
        "0x1c4af2f7b65cc5c589aced8a9e0965183636d718f6fcdab3322b538710d22995");
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
    BOOST_CHECK_EQUAL(tx.to.value(), Address("0x811a752c8cd697e3cb27279c330ed1ada745a8d7"));
    BOOST_CHECK_EQUAL(tx.value, 0);
    BOOST_CHECK_EQUAL(toHex(tx.data), "04f7");
    BOOST_CHECK_EQUAL(tx.maxFeePerBlobGas, 123);
    BOOST_CHECK_EQUAL(tx.blobVersionedHashes.size(), 2);
    BOOST_CHECK_EQUAL(toHex(tx.blobVersionedHashes[0]),
        "c6bdd1de713471bd6cfa62dd8b5a5b42969ed09e26212d3377f3f8426d8ec210");
    BOOST_CHECK_EQUAL(toHex(tx.blobVersionedHashes[1]),
        "8aaeccaf3873d07cef005aca28c39f8a9f8bdb1ec8d79ffc25afc0a4fa2ab736");
    BOOST_CHECK_EQUAL(tx.getSignatureV(), tx.chainId.value() * 2 + 35 + 1);
    // Typed-tx signatureV is the raw y_parity (0/1). This
    // EIP-4844 sample carries yParity=1 (hence the +1 in getSignatureV above).
    BOOST_CHECK_EQUAL(tx.signatureV, 1u);
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

// Typed-tx yParity is restricted to 0/1 (Web3TxHandler rejects signatureV > 1 with
// InvalidVInSignature). Flip the yParity wire byte of each legal typed-tx sample to 0x02
// (single-byte self-encoding — same wire length, outer RLP prefix unaffected) and assert the
// decoder rejects it. Wire offsets are RLP-decoding-verified: EIP-2930 field[8] at 178,
// EIP-1559 field[9] at 184, EIP-4844 field[11] at 232.
BOOST_AUTO_TEST_CASE(typedTxYParityOverOneRejected)
{
    auto byteAt = [](std::string_view hex, std::size_t byteOffset) -> int {
        auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9')
            {
                return c - '0';
            }
            if (c >= 'a' && c <= 'f')
            {
                return c - 'a' + 10;
            }
            return c - 'A' + 10;
        };
        auto pos = 2 + byteOffset * 2;  // skip "0x"
        return nibble(hex[pos]) * 16 + nibble(hex[pos + 1]);
    };
    auto flipByteToTwo = [](std::string_view hex, std::size_t byteOffset) {
        std::string out(hex);
        auto pos = 2 + byteOffset * 2;  // skip "0x"
        out[pos] = '0';
        out[pos + 1] = '2';
        return out;
    };

    // clang-format off
    constexpr std::string_view kEIP2930RawTx = "0x01f8f205078506fc23ac008357b58494811a752c8cd697e3cb27279c330ed1ada745a8d7881bc16d674ec80000906ebaf477f83e051589c1188bcc6ddccdf872f85994de0b295669a9fd93d5f28d9ec85e40f4cb697baef842a00000000000000000000000000000000000000000000000000000000000000003a00000000000000000000000000000000000000000000000000000000000000007d694bb9bc244d798123fde783fcc1c72d3bb8c189413c080a036b241b061a36a32ab7fe86c7aa9eb592dd59018cd0443adc0903590c16b02b0a05edcc541b4741c5cc6dd347c5ed9577ef293a62787b4510465fadbfe39ee4094";
    constexpr std::string_view kEIP1559RawTx = "0x02f8f805078502540be4008506fc23ac008357b58494811a752c8cd697e3cb27279c330ed1ada745a8d7881bc16d674ec80000906ebaf477f83e051589c1188bcc6ddccdf872f85994de0b295669a9fd93d5f28d9ec85e40f4cb697baef842a00000000000000000000000000000000000000000000000000000000000000003a00000000000000000000000000000000000000000000000000000000000000007d694bb9bc244d798123fde783fcc1c72d3bb8c189413c080a036b241b061a36a32ab7fe86c7aa9eb592dd59018cd0443adc0903590c16b02b0a05edcc541b4741c5cc6dd347c5ed9577ef293a62787b4510465fadbfe39ee4094";
    constexpr std::string_view kEIP4844RawTx = "0x03f9012705078502540be4008506fc23ac008357b58494811a752c8cd697e3cb27279c330ed1ada745a8d7808204f7f872f85994de0b295669a9fd93d5f28d9ec85e40f4cb697baef842a00000000000000000000000000000000000000000000000000000000000000003a00000000000000000000000000000000000000000000000000000000000000007d694bb9bc244d798123fde783fcc1c72d3bb8c189413c07bf842a0c6bdd1de713471bd6cfa62dd8b5a5b42969ed09e26212d3377f3f8426d8ec210a08aaeccaf3873d07cef005aca28c39f8a9f8bdb1ec8d79ffc25afc0a4fa2ab73601a036b241b061a36a32ab7fe86c7aa9eb592dd59018cd0443adc0903590c16b02b0a05edcc541b4741c5cc6dd347c5ed9577ef293a62787b4510465fadbfe39ee4094";
    // clang-format on

    struct Sample
    {
        std::string_view rawTx;
        std::size_t yParityOffset;
        int expectedYParityByte;  // the RLP-encoded yParity currently at the offset
    };
    const Sample samples[] = {
        {kEIP2930RawTx, 178, 0x80},
        {kEIP1559RawTx, 184, 0x80},
        {kEIP4844RawTx, 232, 0x01},
    };
    for (const auto& sample : samples)
    {
        // Guard: fail loudly if a wire offset is stale — never silently test the wrong byte.
        BOOST_CHECK_MESSAGE(
            byteAt(sample.rawTx, sample.yParityOffset) == sample.expectedYParityByte,
            "yParity wire byte mismatch at offset " << sample.yParityOffset);

        auto bytes = fromHexWithPrefix(flipByteToTwo(sample.rawTx, sample.yParityOffset));
        auto bRef = bcos::ref(bytes);
        Web3Transaction tx{};
        auto e = codec::rlp::decode(bRef, tx);
        BOOST_REQUIRE_MESSAGE(
            e != nullptr, "yParity=2 must be rejected (offset " << sample.yParityOffset << ')');
        if (e != nullptr)
        {
            BOOST_CHECK_EQUAL(e->errorCode(),
                static_cast<int64_t>(codec::rlp::DecodingError::InvalidVInSignature));
        }
    }
}

// EIP-2 canonical-s: a malleable (high-s) signature must be rejected at decode — the shared
// funnel for eth_sendRawTransaction (EthEndpoint); the engine newPayload path decodes through
// the same funnel once part 5 wires it. Flipping s -> n - s recovers the same sender and would
// otherwise execute byte-for-byte identically.
BOOST_AUTO_TEST_CASE(highSsignatureRejectedAtDecode)
{
    Web3Transaction tx;
    tx.value = 1000000000000000000;
    tx.type = rpc::TransactionType::Legacy;
    tx.data = {};
    tx.to = Address("0x1e58529dAA467406645d0f4B63dec96CA0b87d70");
    tx.nonce = 19;
    tx.gasLimit = 210000;
    tx.maxFeePerGas = 20000000000;
    tx.maxPriorityFeePerGas = 20000000000;
    tx.chainId = 31337;

    auto signData = tx.encodeForSign();
    std::string priv = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
    auto key = std::make_shared<KeyImpl>(fromHex(priv));
    auto newHash = crypto::keccak256Hash(ref(signData));
    auto signatureImpl = bcos::crypto::Secp256k1Crypto();
    auto keyPair = std::make_unique<Secp256k1KeyPair>(key);
    auto signature = signatureImpl.sign(*keyPair, newHash, false);
    tx.signatureR = {signature->begin(), signature->begin() + 32};
    tx.signatureS = {signature->begin() + 32, signature->begin() + 64};
    tx.signatureV = signature->back();

    // Guard: libsecp256k1 signs canonical low-s — confirm before flipping, so a future
    // libsecp256k1 behavior change fails loudly instead of silently weakening the test.
    const u256 s = u256("0x" + toHex(tx.signatureS));
    BOOST_REQUIRE(s <= bcos::crypto::c_secp256k1nOver2);

    // Malleability flip: s' = n - s is high-s but recovers the same sender.
    tx.signatureS = toBigEndian(bcos::crypto::c_secp256k1n - s);
    const u256 flipped = u256("0x" + toHex(tx.signatureS));
    BOOST_REQUIRE(flipped > bcos::crypto::c_secp256k1nOver2);

    bcos::bytes encoded;
    codec::rlp::encode(encoded, tx);
    auto bRef = bcos::ref(encoded);
    Web3Transaction decoded;
    auto e = codec::rlp::decode(bRef, decoded);
    BOOST_REQUIRE(e != nullptr);
    BOOST_CHECK_EQUAL(
        e->errorCode(), static_cast<int64_t>(codec::rlp::DecodingError::InvalidVInSignature));
}

BOOST_AUTO_TEST_CASE(testEIP7702Transaction)
{
    // clang-format off
    constexpr std::string_view rawTx = "0x04f8fd824ee88080078401000001947050718520e6e10e77224126e185e63a87e88af68080f838f7947050718520e6e10e77224126e185e63a87e88af6e1a00000000000000000000000000000000000000000000000000000000000000000f85cf85a809400000000000000000000000000000000000000008080a05817035c2e62f46823f5385b1d8b81d119ba4c6233929476f4fccbeaef0333e1a058434bf27df5285158b805967729a7a200891c29d2385ee324f481de9a4758f801a0f446323d6852d7c33ae025557ddc82fad0a466d932d59055dad01a9c9962aa4aa055cf0699636222c56fc0ad8695c8edef3af0b881616b7811b403755f0ea463eb";
    // clang-format on
    auto bytes = fromHexWithPrefix(rawTx);
    auto bRef = bcos::ref(bytes);
    Web3Transaction tx{};
    auto e = codec::rlp::decode(bRef, tx);
    if (e != nullptr)
    {
        std::cerr << "[EIP7702] decode error: " << e->errorMessage() << std::endl;
    }
    BOOST_CHECK(e == nullptr);
    BOOST_CHECK(tx.type == rpc::TransactionType::EIP7702);
    BOOST_CHECK(tx.chainId.has_value());
    BOOST_CHECK_EQUAL(tx.chainId.value(), 20200);
    BOOST_CHECK_EQUAL(tx.maxFeePerGas, 7);
    BOOST_CHECK_EQUAL(tx.authorizationList.size(), 1);
    if (!tx.authorizationList.empty())
    {
        auto const& a = tx.authorizationList[0];
        BOOST_CHECK_EQUAL(a.chainId, 0);
        BOOST_CHECK_EQUAL(a.nonce, 0);
        BOOST_CHECK_EQUAL(a.yParity, 0);
        BOOST_CHECK_EQUAL(a.r.str(0, std::ios_base::hex),
            "5817035c2e62f46823f5385b1d8b81d119ba4c6233929476f4fccbeaef0333e1");
        BOOST_CHECK_EQUAL(a.s.str(0, std::ios_base::hex),
            "58434bf27df5285158b805967729a7a200891c29d2385ee324f481de9a4758f8");
    }
    // round-trip
    bcos::bytes encoded{};
    codec::rlp::encode(encoded, tx);
    auto rawTx2 = toHexStringWithPrefix(encoded);
    BOOST_CHECK_EQUAL(rawTx, rawTx2);

    // encodeForSign must include the authorization list (it is part of the
    // EIP-7702 signing payload); otherwise signature recovery fails.
    auto signPayload = tx.encodeForSign();
    BOOST_CHECK(signPayload.size() > 0);
    auto signHash = bcos::crypto::keccak256Hash(bcos::ref(signPayload)).hexPrefixed();
    // keccak256 of the type||rlp([...fields...]) preimage — recompute via rlp
    // and compare the recovered sender to the fixture's sender below.
    BOOST_CHECK_EQUAL(signHash.size(), 66);
    BOOST_CHECK_EQUAL(tx.sender(), "0x1a2e20b2fb1346f5751ec4d05f1964042f06c072");
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
    BOOST_CHECK_EQUAL(tx.to.value().hexPrefixed(), "0xe10f39a0dfb9e380b6d176eb7183af32b68028d7");
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
    BOOST_CHECK_EQUAL(tx.to.value().hexPrefixed(), "0x1c479675ad559dc151f6ec7ed3fbf8cee79582b6");
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

// Deposit encode→decode roundtrip locks the uint32_t isSystemTransaction workaround
// (see Web3TxHandler.h header comment) — when the underlying ODR defect is fixed and
// the field switches back to uint8_t, this test ensures the encoded byte does not
// silently regress.
BOOST_AUTO_TEST_CASE(depositRoundtrip)
{
    Web3Transaction deposit;
    deposit.type = rpc::TransactionType::Deposit;
    deposit.sourceHash = h256("6ab967dfdd3aa359031bef6965cca32ed9a21ea969f7aeee2e58817142a645d7");
    deposit.from = Address("0xdead000000000000000000000000000000000011");
    deposit.to.emplace(Address("0x4200000000000000000000000000000000000022"));
    deposit.mint = u256("0x16345785d8a0000");
    deposit.value = u256(0);
    deposit.gasLimit = 1000000;
    deposit.isSystemTx = true;
    deposit.data = bcos::bytes{};

    // Encode → decode roundtrip
    auto encoded = deposit.encode();
    auto ref = bcos::ref(encoded);
    Web3Transaction decoded;
    auto err = decoded.decode(ref, false);  // withSig=false, deposit has no signature
    BOOST_REQUIRE(err == nullptr);

    BOOST_CHECK(decoded.type == rpc::TransactionType::Deposit);
    BOOST_CHECK(decoded.isSystemTx);
    BOOST_CHECK_EQUAL(decoded.sourceHash.hex(), deposit.sourceHash.hex());
    BOOST_CHECK_EQUAL(decoded.from.hexPrefixed(), deposit.from.hexPrefixed());
    BOOST_CHECK(decoded.to.has_value());
    BOOST_CHECK_EQUAL(decoded.to->hexPrefixed(), deposit.to->hexPrefixed());
    BOOST_CHECK_EQUAL(decoded.mint, deposit.mint);
    BOOST_CHECK_EQUAL(decoded.value, deposit.value);
    BOOST_CHECK_EQUAL(decoded.gasLimit, deposit.gasLimit);

    // Also round-trip the system-tx=false case
    Web3Transaction nonSysDeposit;
    nonSysDeposit.type = rpc::TransactionType::Deposit;
    nonSysDeposit.sourceHash =
        h256("7bc967dfdd3aa359031bef6965cca32ed9a21ea969f7aeee2e58817142a645d8");
    nonSysDeposit.from = Address("0xdead000000000000000000000000000000000011");
    nonSysDeposit.to.emplace(Address("0x4200000000000000000000000000000000000022"));
    nonSysDeposit.mint = u256(100);
    nonSysDeposit.value = u256(0);
    nonSysDeposit.gasLimit = 500000;
    nonSysDeposit.isSystemTx = false;
    nonSysDeposit.data = bcos::bytes{};

    auto encoded2 = nonSysDeposit.encode();
    auto ref2 = bcos::ref(encoded2);
    Web3Transaction decoded2;
    err = decoded2.decode(ref2, false);
    BOOST_REQUIRE(err == nullptr);

    BOOST_CHECK(decoded2.type == rpc::TransactionType::Deposit);
    BOOST_CHECK(!decoded2.isSystemTx);
    BOOST_CHECK_EQUAL(decoded2.mint, nonSysDeposit.mint);
}

// The sendRawTransaction guard in EthEndpoint.cpp checks web3Tx.type == Deposit after decoding the
// raw bytes. The guard itself is a simple `if` + throw; the decode → type-detection path is the
// test that verifies a valid 0x7E envelope is correctly identified. An RPC-level test would need
// the full EthEndpoint fixture (future work).
BOOST_AUTO_TEST_CASE(decodeDepositIdentifiesTypeForSendRawGuard)
{
    Web3Transaction deposit;
    deposit.type = rpc::TransactionType::Deposit;
    deposit.sourceHash = h256("0xabcd000000000000000000000000000000000000000000000000000000000000");
    deposit.from = Address("0xdead000000000000000000000000000000000099");
    deposit.to.emplace(Address("0x4200000000000000000000000000000000000011"));
    deposit.mint = u256(1);
    deposit.value = u256(0);
    deposit.gasLimit = 21000;
    deposit.isSystemTx = false;
    deposit.data = bcos::bytes{};

    auto encoded = deposit.encode();
    BOOST_REQUIRE(!encoded.empty());

    // The raw envelope must start with 0x7E (EIP-2718 type byte)
    BOOST_CHECK_EQUAL(
        static_cast<uint8_t>(encoded[0]), static_cast<uint8_t>(rpc::TransactionType::Deposit));

    // Decode through the public codec entry point — this is the path EthEndpoint calls
    // (codec::rlp::decode → Web3Transaction::decode) before the type-guard check.
    auto ref = bcos::ref(encoded);
    Web3Transaction decoded;
    auto err = bcos::codec::rlp::decode(ref, decoded);
    BOOST_REQUIRE(err == nullptr);
    BOOST_CHECK(decoded.type == rpc::TransactionType::Deposit);
    // A real sendRawTransaction call would now hit: if (web3Tx.type == Deposit) → throw
    // InvalidParams
}

// Golden-vector deposit encoding: encode known deposit txs and compare byte-for-byte
// against independently-verified RLP output (cross-validated against the OP Stack deposit
// spec: 0x7E || rlp([sourceHash, from, to, mint, value, gas, isSystemTransaction, data])).
// The golden hex strings were verified against an independent oracle and op-geth-shaped
// vectors. If a future change alters the deposit encoding, this test breaks — that's
// intentional. The 0x80 vs 0x01 isSystemTx distinction is the op-geth convention the
// uint32_t workaround (see Web3TxHandler.h header comment) exists to preserve.
BOOST_AUTO_TEST_CASE(depositGoldenEncoding)
{
    // isSystemTx=true: 0x7E || rlp([33b sourceHash, 21b from, 21b to, 9b mint,
    //                          1b value(0), 4b gas, 1b isSystemTx(0x01), 1b data(empty)])
    // gas = 0x0f4240 = 1000000
    {
        Web3Transaction deposit;
        deposit.type = rpc::TransactionType::Deposit;
        deposit.sourceHash =
            h256("6ab967dfdd3aa359031bef6965cca32ed9a21ea969f7aeee2e58817142a645d7");
        deposit.from = Address("0xdead000000000000000000000000000000000011");
        deposit.to.emplace(Address("0x4200000000000000000000000000000000000022"));
        deposit.mint = u256("0x16345785d8a0000");
        deposit.value = u256(0);
        deposit.gasLimit = 1000000;
        deposit.isSystemTx = true;
        deposit.data = bcos::bytes{};

        auto encoded = deposit.encode();
        BOOST_CHECK_EQUAL(toHex(encoded),
            "7ef85ba06ab967dfdd3aa359031bef6965cca32ed9a21ea969f7aeee2e58817142a645d7"
            "94dead000000000000000000000000000000000011"
            "944200000000000000000000000000000000000022"
            "88016345785d8a0000"
            "80"
            "830f4240"
            "01"
            "80");
    }

    // isSystemTx=false: 0x7E || rlp([33b sourceHash, 21b from, 21b to, 1b mint(0x64=100),
    //                              1b value(0), 4b gas, 1b isSystemTx(0x80), 1b data(empty)])
    // gas = 0x07a120 = 500000, list header 0xf853 (shorter: mint is 1 byte vs 9)
    {
        Web3Transaction deposit;
        deposit.type = rpc::TransactionType::Deposit;
        deposit.sourceHash =
            h256("7bc967dfdd3aa359031bef6965cca32ed9a21ea969f7aeee2e58817142a645d8");
        deposit.from = Address("0xdead000000000000000000000000000000000011");
        deposit.to.emplace(Address("0x4200000000000000000000000000000000000022"));
        deposit.mint = u256(100);
        deposit.value = u256(0);
        deposit.gasLimit = 500000;
        deposit.isSystemTx = false;
        deposit.data = bcos::bytes{};

        auto encoded = deposit.encode();
        BOOST_CHECK_EQUAL(toHex(encoded),
            "7ef853a07bc967dfdd3aa359031bef6965cca32ed9a21ea969f7aeee2e58817142a645d8"
            "94dead000000000000000000000000000000000011"
            "944200000000000000000000000000000000000022"
            "64"
            "80"
            "8307a120"
            "80"
            "80");
    }
}

// The six codecs must enforce the declared RLP list payload boundary (op-geth List/ListEnd
// parity): a list header that under-declares its payload must not let field decoding cross
// into the trailing bytes and read them as fields. Build a valid legacy envelope, then shrink
// the outer list header's declared length so the fields overflow it — decode must fail.
BOOST_AUTO_TEST_CASE(testRejectPayloadLengthOverflow)
{
    // A valid legacy pre-EIP-155 envelope (same raw tx as testLegacyTransactionDecode). The
    // outer list header 0xf8 0x9b declares a 0x9b-byte payload that exactly covers the 9 items.
    auto bytes = fromHexWithPrefix(
        "0xf89b0c8504a817c80082520894727fc6a68321b754475c668a6abfb6e9e71c169a888ac7230489e80000afa9"
        "059cbb000000000213ed0f886efd100b67c7e4ec0a85a7d20dc971600000000000000000000015af1d78b58c40"
        "0026a0be67e0a07db67da8d446f76add590e54b6e92cb6b8f9835aeb67540579a27717a02d690516512020171c"
        "1ec870f6ff45398cc8609250326be89915fb538e7bd718");
    auto bRef = bcos::ref(bytes);
    Web3Transaction tx{};
    BOOST_REQUIRE(codec::rlp::decode(bRef, tx) == nullptr);

    // Corrupt the declared payload length to 0x9a (one byte short): the nine fields' bytes now
    // overflow the declared boundary by one byte. The decoder must reject (fields cross the
    // declared payload — op-geth ListEnd errNotAtEOL), not silently read the trailing byte as
    // part of a field.
    bytes[1] = static_cast<byte>(0x9a);
    auto badRef = bcos::ref(bytes);
    Web3Transaction badTx{};
    auto e = codec::rlp::decode(badRef, badTx);
    BOOST_REQUIRE(e != nullptr);
    // Anchor the classification: fields cross the declared list boundary (op-geth ListEnd
    // errNotAtEOL), not a generic decode failure.
    BOOST_CHECK_EQUAL(
        e->errorCode(), static_cast<int64_t>(codec::rlp::DecodingError::UnexpectedListElements));
}

// Same payload-length overflow check, but exercising an EIP-1559 typed tx codec.
BOOST_AUTO_TEST_CASE(testRejectPayloadLengthOverflowEIP1559)
{
    namespace rlp = bcos::codec::rlp;
    // Build a minimal valid EIP-1559 envelope.
    bcos::bytes items;
    rlp::encode(items, static_cast<uint64_t>(1));           // chainId
    rlp::encode(items, static_cast<uint64_t>(0));           // nonce
    rlp::encode(items, static_cast<uint64_t>(1000000000));  // maxPriorityFeePerGas
    rlp::encode(items, static_cast<uint64_t>(2000000000));  // maxFeePerGas
    rlp::encode(items, static_cast<uint64_t>(21000));       // gasLimit
    rlp::encode(items, bcos::Address("0x1111111111111111111111111111111111111111"));
    rlp::encode(items, static_cast<uint64_t>(0));  // value
    rlp::encode(items, bcos::bytes{});             // data
    items.push_back(rlp::LIST_HEAD_BASE);          // empty accessList
    rlp::encode(items, static_cast<uint64_t>(0));  // yParity
    rlp::encode(items, bcos::bytes(32, 0x11));     // r
    rlp::encode(items, bcos::bytes(32, 0x22));     // s

    bcos::bytes envelope;
    envelope.push_back(static_cast<byte>(bcos::rpc::TransactionType::EIP1559));
    rlp::encodeHeader(envelope, {.isList = true, .payloadLength = items.size()});
    envelope.insert(envelope.end(), items.begin(), items.end());

    // Verify it decodes cleanly first.
    {
        auto ref = bcos::ref(envelope);
        Web3Transaction tx{};
        BOOST_REQUIRE(rlp::decode(ref, tx) == nullptr);
    }
    // Shrink the declared payload length by 1: fields now overflow by one byte.
    // byte 0 = type (0x02), byte 1 = long-list mode byte (0xf8), byte 2 = the actual payload
    // length. Corrupting byte 2 (NOT byte 1 — decrementing the mode byte 0xf8 -> 0xf7 would
    // flip the header to short-list mode and misalign the whole field stream, firing the
    // Address width gate instead) keeps the long-list header intact: all fields decode
    // cleanly and the ListEnd parity check rejects, mirroring the legacy case.
    envelope[2] = envelope[2] - 1;
    auto badRef = bcos::ref(envelope);
    Web3Transaction badTx{};
    auto e = rlp::decode(badRef, badTx);
    BOOST_REQUIRE(e != nullptr);
    // Fields consume one byte more than the declared payload — the EIP-1559 ListEnd parity
    // (same op-geth errNotAtEOL parity as the legacy test above).
    BOOST_CHECK_EQUAL(
        e->errorCode(), static_cast<int64_t>(codec::rlp::DecodingError::UnexpectedListElements));
}

// RLPDecode.h must reject integers wider than the target type (op-geth rlp uint overflow /
// op-reth Error::Overflow parity) instead of truncating via fromBigEndian: a 9-byte uint64
// or a 33-byte u256 is malformed input, never silently narrowed.
BOOST_AUTO_TEST_CASE(testRejectOverwideInteger)
{
    namespace rlp = bcos::codec::rlp;

    // uint64 with 9 payload bytes: 0x89 + 9 bytes.
    {
        bcos::bytes raw{0x89, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        auto ref = bcos::ref(raw);
        uint64_t v = 0;
        auto e = rlp::decode(ref, v);
        BOOST_CHECK(e != nullptr);
    }
    // u256 with 33 payload bytes (header 0xa1 + 33B): must be rejected, not truncated.
    {
        bcos::bytes raw(34, 0x00);
        raw[0] = 0xa1;  // long-string header, 33-byte payload
        raw[1] = 0x01;  // leading non-zero so it is not a canonical-size violation
        auto ref = bcos::ref(raw);
        bcos::u256 v = 0;
        auto e = rlp::decode(ref, v);
        BOOST_CHECK(e != nullptr);
    }
    // A canonical 8-byte uint64 still decodes fine (no regression).
    {
        bcos::bytes raw{0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2a};  // 42
        auto ref = bcos::ref(raw);
        uint64_t v = 0;
        BOOST_REQUIRE(rlp::decode(ref, v) == nullptr);
        BOOST_CHECK_EQUAL(v, 42);
    }
    // A canonical 32-byte u256 still decodes fine (guards the maxBytes formula against a
    // sizeof(T)-based regression: sizeof(u256) == 48, which would reject legal 32-byte values).
    {
        bcos::bytes raw(33, 0x00);
        raw[0] = 0xa0;   // long-string header, 32-byte payload
        raw[32] = 0x01;  // least-significant payload byte -> value 1
        auto ref = bcos::ref(raw);
        bcos::u256 v = 0;
        BOOST_REQUIRE(rlp::decode(ref, v) == nullptr);
        BOOST_CHECK(v == 1);
    }
    // Narrow integers: 5-byte uint32 rejected, canonical 4-byte accepted.
    {
        bcos::bytes raw{0x85, 0x01, 0x00, 0x00, 0x00, 0x00};
        auto ref = bcos::ref(raw);
        uint32_t v = 0;
        BOOST_CHECK(rlp::decode(ref, v) != nullptr);
    }
    {
        bcos::bytes raw{0x84, 0x00, 0x00, 0x00, 0x2a};  // 42
        auto ref = bcos::ref(raw);
        uint32_t v = 0;
        BOOST_REQUIRE(rlp::decode(ref, v) == nullptr);
        BOOST_CHECK_EQUAL(v, 42);
    }
    // 3-byte uint16 rejected, canonical 2-byte accepted.
    {
        bcos::bytes raw{0x83, 0x01, 0x00, 0x00};
        auto ref = bcos::ref(raw);
        uint16_t v = 0;
        BOOST_CHECK(rlp::decode(ref, v) != nullptr);
    }
    {
        bcos::bytes raw{0x82, 0x00, 0x2a};  // 42
        auto ref = bcos::ref(raw);
        uint16_t v = 0;
        BOOST_REQUIRE(rlp::decode(ref, v) == nullptr);
        BOOST_CHECK_EQUAL(v, 42);
    }
}

// Legacy signing-preimage tail strictness (Web3TxHandler legacy handler, withSig=false):
// op-geth preimages are exactly 6 fields (pre-EIP-155) or 9 fields (EIP-155 chainId,0,0 tail).
// A 7- or 8-field preimage (chainId without both placeholders) is malformed — no
// "placeholders optional" leniency. Regression pin for the unconditional placeholder
// consumption at Web3TxHandler.cpp (the N2 tightening).
BOOST_AUTO_TEST_CASE(testLegacyTailFieldCountRejected)
{
    namespace rlp = bcos::codec::rlp;
    // Each variant encodes ONE RLP list with the given field count (the variadic rlp::encode
    // emits a list header + all items in a single call — splitting the call would append items
    // OUTSIDE the list).
    auto make9 = [] {
        bcos::bytes legacy;
        rlp::encode(legacy, static_cast<uint64_t>(0), static_cast<uint64_t>(1),
            static_cast<uint64_t>(21000),
            bcos::Address("0xdead000000000000000000000000000000000011"), static_cast<uint64_t>(0),
            bcos::bytes{}, static_cast<uint64_t>(123), static_cast<uint64_t>(0),
            static_cast<uint64_t>(0));  // [..6 fields, chainId, 0, 0]
        return legacy;
    };
    auto make8 = [] {
        bcos::bytes legacy;
        rlp::encode(legacy, static_cast<uint64_t>(0), static_cast<uint64_t>(1),
            static_cast<uint64_t>(21000),
            bcos::Address("0xdead000000000000000000000000000000000011"), static_cast<uint64_t>(0),
            bcos::bytes{}, static_cast<uint64_t>(123), static_cast<uint64_t>(0));  // chainId, 0
        return legacy;
    };
    auto make7 = [] {
        bcos::bytes legacy;
        rlp::encode(legacy, static_cast<uint64_t>(0), static_cast<uint64_t>(1),
            static_cast<uint64_t>(21000),
            bcos::Address("0xdead000000000000000000000000000000000011"), static_cast<uint64_t>(0),
            bcos::bytes{}, static_cast<uint64_t>(123));  // chainId only
        return legacy;
    };
    auto make6 = [] {
        bcos::bytes legacy;
        rlp::encode(legacy, static_cast<uint64_t>(0), static_cast<uint64_t>(1),
            static_cast<uint64_t>(21000),
            bcos::Address("0xdead000000000000000000000000000000000011"), static_cast<uint64_t>(0),
            bcos::bytes{});
        return legacy;
    };
    // 9 fields (chainId,0,0): accepted, chainId recovered.
    {
        auto bytes = make9();
        auto bRef = bcos::ref(bytes);
        Web3Transaction tx{};
        BOOST_REQUIRE(rlp::decodeFromPayload(bRef, tx) == nullptr);
        BOOST_CHECK(tx.type == rpc::TransactionType::Legacy);
        BOOST_REQUIRE(tx.chainId.has_value());
        BOOST_CHECK_EQUAL(tx.chainId.value(), 123);
    }
    // 8 fields (chainId, 0, missing second placeholder): rejected.
    // The tail gate consumes exactly two placeholders; the second decode runs out of
    // input, so the error class is InputTooShort — anchored so a refactor that drops the
    // placeholder consumption (accepting 7/8-field preimages) cannot hide behind a
    // coincidental parse error.
    {
        auto bytes = make8();
        auto bRef = bcos::ref(bytes);
        Web3Transaction tx{};
        auto err = rlp::decodeFromPayload(bRef, tx);
        BOOST_REQUIRE(err != nullptr);
        BOOST_CHECK(err->errorCode() == static_cast<int>(rlp::DecodingError::InputTooShort));
    }
    // 7 fields (chainId only): rejected, same InputTooShort class (placeholder 1 of 2).
    {
        auto bytes = make7();
        auto bRef = bcos::ref(bytes);
        Web3Transaction tx{};
        auto err = rlp::decodeFromPayload(bRef, tx);
        BOOST_REQUIRE(err != nullptr);
        BOOST_CHECK(err->errorCode() == static_cast<int>(rlp::DecodingError::InputTooShort));
    }
    // 6 fields (no tail): accepted as pre-EIP-155 (chainId nullopt) — the exemption boundary.
    {
        auto bytes = make6();
        auto bRef = bcos::ref(bytes);
        Web3Transaction tx{};
        BOOST_REQUIRE(rlp::decodeFromPayload(bRef, tx) == nullptr);
        BOOST_CHECK(!tx.chainId.has_value());
    }
}

// EIP-2 width gate: a 33-byte 0x00||r / 0x00||s signature (the truncation-bypass shape) must be
// rejected at the decode funnel (checkEip2Signature, Web3Transaction.cpp:50-54). padSignature
// only zero-pads shorter input and fromBigEndian truncates wider — without the explicit >32
// gate, 0x00||r would narrow to a legal r and pass. Pinned so removing the gate turns this red.
BOOST_AUTO_TEST_CASE(testWideSignatureRejectedAtDecode)
{
    namespace rlp = bcos::codec::rlp;
    // EIP-2930 envelope with a 33-byte r (leading zero). Fields: chainId, nonce, gasPrice,
    // gasLimit, to, value, data, accessList(empty), then yParity/r/s. Signature field r = 33
    // bytes. Built as bare items + one list header (the variadic rlp::encode would wrap each
    // call in its own list); the empty accessList is the RLP empty-list byte 0xc0.
    bcos::bytes items;
    rlp::encode(items, static_cast<uint64_t>(1));            // chainId
    rlp::encode(items, static_cast<uint64_t>(0));            // nonce
    rlp::encode(items, static_cast<uint64_t>(30000000000));  // gasPrice
    rlp::encode(items, static_cast<uint64_t>(5000000));      // gasLimit
    rlp::encode(items, bcos::Address("0x811a752c8cd697e3cb27279c330ed1ada745a8d7"));
    rlp::encode(items, static_cast<uint64_t>(0));  // value
    rlp::encode(items, bcos::bytes{});             // data (empty)
    items.push_back(codec::rlp::LIST_HEAD_BASE);   // 0xc0: empty accessList
    rlp::encode(items, static_cast<uint64_t>(0));  // yParity
    bcos::bytes wideR(33, 0x00);
    wideR[32] = 0x01;  // 0x00 || 0x...01 — would truncate to r=1 (legal) without the width gate.
    rlp::encode(items, wideR);
    rlp::encode(items, bcos::bytes(32, 0x02));  // s

    bcos::bytes envelope;
    envelope.push_back(static_cast<byte>(bcos::rpc::TransactionType::EIP2930));
    codec::rlp::encodeHeader(
        envelope, codec::rlp::Header{.isList = true, .payloadLength = items.size()});
    envelope.insert(envelope.end(), items.begin(), items.end());

    auto bRef = bcos::ref(envelope);
    Web3Transaction tx{};
    auto err = rlp::decode(bRef, tx);
    BOOST_REQUIRE(err != nullptr);
    // The width gate reports the EIP-2 signature error class, not a generic decode failure.
    BOOST_CHECK(err->errorCode() == static_cast<int>(rlp::DecodingError::InvalidVInSignature));
}

// Typed tx with chainId=0 must decode with chainId present (not nullopt).
// A nullopt here would let the executor's chainId gate pass the tx unchecked (op-geth
// modernSigner rejects chainId 0 != node-config; the decode layer must preserve the value).
BOOST_AUTO_TEST_CASE(testTypedTxChainIdZeroDecodes)
{
    namespace rlp = bcos::codec::rlp;
    // EIP-1559 envelope: type 0x02 || rlp([chainId=0, nonce, maxPriorityFee, maxFee, gasLimit,
    // to, value, data, accessList, yParity=0, r, s]). ChainId field is explicitly zero.
    bcos::bytes items;
    rlp::encode(items, static_cast<uint64_t>(0));           // chainId = 0
    rlp::encode(items, static_cast<uint64_t>(0));           // nonce
    rlp::encode(items, static_cast<uint64_t>(1000000000));  // maxPriorityFeePerGas
    rlp::encode(items, static_cast<uint64_t>(1000000000));  // maxFeePerGas
    rlp::encode(items, static_cast<uint64_t>(21000));       // gasLimit
    rlp::encode(items, bcos::Address("0x1111111111111111111111111111111111111111"));
    rlp::encode(items, static_cast<uint64_t>(0));  // value
    rlp::encode(items, bcos::bytes{});             // data
    items.push_back(rlp::LIST_HEAD_BASE);          // empty accessList (0xc0)
    rlp::encode(items, static_cast<uint64_t>(0));  // yParity
    rlp::encode(items, bcos::bytes(32, 0x11));     // r
    rlp::encode(items, bcos::bytes(32, 0x22));     // s

    // Wrap in an RLP list header (the handler expects type-byte + list-header + fields).
    rlp::Header listHeader{.isList = true, .payloadLength = items.size()};
    bcos::bytes envelope;
    envelope.push_back(static_cast<byte>(bcos::rpc::TransactionType::EIP1559));
    rlp::encodeHeader(envelope, listHeader);
    envelope.insert(envelope.end(), items.begin(), items.end());

    auto bRef = bcos::ref(envelope);
    Web3Transaction tx{};
    auto err = rlp::decode(bRef, tx);
    BOOST_REQUIRE(err == nullptr);
    BOOST_CHECK(tx.type == rpc::TransactionType::EIP1559);
    // chainId=0 is explicitly decoded (not nullopt).
    BOOST_REQUIRE(tx.chainId.has_value());
    BOOST_CHECK_EQUAL(tx.chainId.value(), 0u);
    BOOST_CHECK_EQUAL(tx.signatureV, 0u);
}

// F7: Trailing bytes after a valid RLP envelope must be rejected by Web3Transaction::decode.
// The outer guard in decode() checks !in.empty() after the handler returns success.
BOOST_AUTO_TEST_CASE(testTrailingBytesAfterEnvelopeRejected)
{
    namespace rlp = bcos::codec::rlp;
    // Build a valid EIP-1559 envelope (same as testTypedTxChainIdZeroDecodes).
    bcos::bytes items;
    rlp::encode(items, static_cast<uint64_t>(1));
    rlp::encode(items, static_cast<uint64_t>(0));
    rlp::encode(items, static_cast<uint64_t>(1000000000));
    rlp::encode(items, static_cast<uint64_t>(1000000000));
    rlp::encode(items, static_cast<uint64_t>(21000));
    rlp::encode(items, bcos::Address("0x1111111111111111111111111111111111111111"));
    rlp::encode(items, static_cast<uint64_t>(0));
    rlp::encode(items, bcos::bytes{});
    items.push_back(rlp::LIST_HEAD_BASE);
    rlp::encode(items, static_cast<uint64_t>(0));
    rlp::encode(items, bcos::bytes(32, 0x11));
    rlp::encode(items, bcos::bytes(32, 0x22));

    bcos::bytes envelope;
    envelope.push_back(static_cast<byte>(bcos::rpc::TransactionType::EIP1559));
    rlp::encodeHeader(envelope, {.isList = true, .payloadLength = items.size()});
    envelope.insert(envelope.end(), items.begin(), items.end());

    // Append a trailing byte — the handler will consume the envelope but leave 0x00 unconsumed.
    envelope.push_back(0x00);
    auto bRef = bcos::ref(envelope);
    Web3Transaction tx{};
    auto err = rlp::decode(bRef, tx);
    BOOST_REQUIRE(err != nullptr);
    BOOST_CHECK(err->errorCode() == static_cast<int>(rlp::DecodingError::InputTooLong));
}

// F5: EIP-7702 negative decode tests — yParity > 1, truncated input, empty input.
BOOST_AUTO_TEST_CASE(testEIP7702NegativeDecodes)
{
    namespace rlp = bcos::codec::rlp;
    // Valid EIP-7702 envelope (from testEIP7702Transaction) — reuse for yParity=2 mutation.
    constexpr std::string_view rawTx =
        "0x04f8fd824ee88080078401000001947050718520e6e10e77224126e185e63a87e88af68080f838f794"
        "7050718520e6e10e77224126e185e63a87e88af6e1a00000000000000000000000000000000000000000"
        "000000000000000000000000f85cf85a809400000000000000000000000000000000000000008080a05817"
        "035c2e62f46823f5385b1d8b81d119ba4c6233929476f4fccbeaef0333e1a058434bf27df5285158b8059"
        "67729a7a200891c29d2385ee324f481de9a4758f801a0f446323d6852d7c33ae025557ddc82fad0a466d9"
        "32d59055dad01a9c9962aa4aa055cf0699636222c56fc0ad8695c8edef3af0b881616b7811b403755f0ea"
        "463eb";
    auto bytes = fromHexWithPrefix(rawTx);

    // (a) yParity=2: build a minimal EIP-7702 envelope with yParity=2 (invalid).
    // EIP-7702 field order: chainId, nonce, maxPrio, maxFee, gasLimit, to, value, data,
    // accessList, authorizationList, yParity, r, s.
    {
        bcos::bytes items2;
        rlp::encode(items2, static_cast<uint64_t>(1));
        rlp::encode(items2, static_cast<uint64_t>(0));
        rlp::encode(items2, static_cast<uint64_t>(1000000000));
        rlp::encode(items2, static_cast<uint64_t>(1000000000));
        rlp::encode(items2, static_cast<uint64_t>(21000));
        rlp::encode(items2, bcos::Address("0x1111111111111111111111111111111111111111"));
        rlp::encode(items2, static_cast<uint64_t>(0));
        rlp::encode(items2, bcos::bytes{});
        items2.push_back(rlp::LIST_HEAD_BASE);          // empty accessList (0xc0)
        items2.push_back(rlp::LIST_HEAD_BASE);          // empty authorizationList (0xc0)
        rlp::encode(items2, static_cast<uint64_t>(2));  // yParity = 2 (invalid)
        rlp::encode(items2, bcos::bytes(32, 0x11));
        rlp::encode(items2, bcos::bytes(32, 0x22));
        bcos::bytes env;
        env.push_back(static_cast<byte>(bcos::rpc::TransactionType::EIP7702));
        rlp::encodeHeader(env, {.isList = true, .payloadLength = items2.size()});
        env.insert(env.end(), items2.begin(), items2.end());
        auto bRef = bcos::ref(env);
        Web3Transaction tx{};
        auto err = rlp::decode(bRef, tx);
        BOOST_REQUIRE(err != nullptr);
        BOOST_CHECK(err->errorCode() == static_cast<int>(rlp::DecodingError::InvalidVInSignature));
    }

    // (b) Empty input for EIP-7702 handler.
    {
        bcos::bytes empty{static_cast<byte>(bcos::rpc::TransactionType::EIP7702)};
        auto bRef = bcos::ref(empty);
        Web3Transaction tx{};
        auto err = rlp::decode(bRef, tx);
        BOOST_REQUIRE(err != nullptr);
        BOOST_CHECK(err->errorCode() == static_cast<int>(rlp::DecodingError::InputTooShort));
    }

    // (c) Truncated EIP-7702 envelope (type byte + partial list).
    {
        bcos::bytes trunc{static_cast<byte>(bcos::rpc::TransactionType::EIP7702), 0xc2, 0x01, 0x02};
        auto bRef = bcos::ref(trunc);
        Web3Transaction tx{};
        auto err = rlp::decode(bRef, tx);
        BOOST_REQUIRE(err != nullptr);
    }
}

// F4: web3ChainIdFromEnvelope — dedicated unit tests for all envelope forms.
// This function is called in TxValidator and EthEndpoint admission paths; regressions here
// could silently break the chainId gate. Test all legacy forms (6/7/8/9-field) plus typed.
BOOST_AUTO_TEST_CASE(testWeb3ChainIdFromEnvelope)
{
    namespace rlp = bcos::codec::rlp;
    namespace envelope = bcos::rlp::protocol;

    // (a) Typed EIP-1559 envelope: chainId is field 0 of inner list.
    {
        bcos::bytes items;
        rlp::encode(items, static_cast<uint64_t>(42));
        rlp::encode(items, static_cast<uint64_t>(0));
        rlp::encode(items, static_cast<uint64_t>(1000000000));
        rlp::encode(items, static_cast<uint64_t>(1000000000));
        rlp::encode(items, static_cast<uint64_t>(21000));
        rlp::encode(items, bcos::Address("0x1111111111111111111111111111111111111111"));
        rlp::encode(items, static_cast<uint64_t>(0));
        rlp::encode(items, bcos::bytes{});
        items.push_back(rlp::LIST_HEAD_BASE);
        rlp::encode(items, static_cast<uint64_t>(0));
        rlp::encode(items, bcos::bytes(32, 0x11));
        rlp::encode(items, bcos::bytes(32, 0x22));

        bcos::bytes env;
        env.push_back(static_cast<byte>(bcos::rpc::TransactionType::EIP1559));
        rlp::encodeHeader(env, {.isList = true, .payloadLength = items.size()});
        env.insert(env.end(), items.begin(), items.end());
        auto result = envelope::web3ChainIdFromEnvelope(bcos::ref(env));
        BOOST_REQUIRE(result.has_value());
        BOOST_CHECK_EQUAL(result.value(), 42u);
    }

    // Legacy envelope builders — preimage [6 fields, chainId, 0, 0] (the admission path
    // stores encodeForSign()), full-envelope [6 fields, v, r, s] (raw signed envelope),
    // and the 6-field pre-EIP-155 preimage.
    auto makePreimage = [&](uint64_t chainId) {
        bcos::bytes items;
        rlp::encode(items, static_cast<uint64_t>(1));   // nonce
        rlp::encode(items, static_cast<uint64_t>(10));  // gasPrice
        rlp::encode(items, static_cast<uint64_t>(21000));
        rlp::encode(items, bcos::Address("0x0100000000000000000000000000000000000001"));
        rlp::encode(items, static_cast<uint64_t>(0));  // value
        rlp::encode(items, bcos::bytes{});             // data
        rlp::encode(items, chainId);                   // field 7 = EIP-155 chainId
        rlp::encode(items, static_cast<uint64_t>(0));  // field 8 = 0 placeholder
        rlp::encode(items, static_cast<uint64_t>(0));  // field 9 = 0 placeholder
        bcos::bytes env;
        rlp::encodeHeader(env, {.isList = true, .payloadLength = items.size()});
        env.insert(env.end(), items.begin(), items.end());
        return env;
    };
    auto makeFullEnvelope = [&](uint64_t v) {
        bcos::bytes items;
        rlp::encode(items, static_cast<uint64_t>(1));
        rlp::encode(items, static_cast<uint64_t>(10));
        rlp::encode(items, static_cast<uint64_t>(21000));
        rlp::encode(items, bcos::Address("0x0100000000000000000000000000000000000001"));
        rlp::encode(items, static_cast<uint64_t>(0));
        rlp::encode(items, bcos::bytes{});
        rlp::encode(items, v);                         // field 7 = v
        rlp::encode(items, static_cast<uint64_t>(1));  // field 8 = r (non-empty)
        rlp::encode(items, static_cast<uint64_t>(1));  // field 9 = s (non-empty)
        bcos::bytes env;
        rlp::encodeHeader(env, {.isList = true, .payloadLength = items.size()});
        env.insert(env.end(), items.begin(), items.end());
        return env;
    };
    auto makeBareSixField = [&]() {
        bcos::bytes items;
        rlp::encode(items, static_cast<uint64_t>(1));
        rlp::encode(items, static_cast<uint64_t>(10));
        rlp::encode(items, static_cast<uint64_t>(21000));
        rlp::encode(items, bcos::Address("0x0100000000000000000000000000000000000001"));
        rlp::encode(items, static_cast<uint64_t>(0));
        rlp::encode(items, bcos::bytes{});
        bcos::bytes env;
        rlp::encodeHeader(env, {.isList = true, .payloadLength = items.size()});
        env.insert(env.end(), items.begin(), items.end());
        return env;
    };

    // (b) Preimage, single-byte chainId.
    {
        auto env = makePreimage(42);
        auto result = envelope::web3ChainIdFromEnvelope(bcos::ref(env));
        BOOST_REQUIRE(result.has_value());
        BOOST_CHECK_EQUAL(result.value(), 42u);
    }
    // (c) Preimage, multi-byte chainId — regression pin for the field-7 re-parse bug
    // (payload was decoded as a fresh RLP item; chainId 8453 mis-read as 33).
    {
        auto env = makePreimage(8453);
        auto result = envelope::web3ChainIdFromEnvelope(bcos::ref(env));
        BOOST_REQUIRE(result.has_value());
        BOOST_CHECK_EQUAL(result.value(), 8453u);
    }
    // (d) Preimage, chainId whose payload first byte is a list header (200 = 0xC8) —
    // previously decode-failed to nullopt, a bogus "unprotected" exemption.
    {
        auto env = makePreimage(200);
        auto result = envelope::web3ChainIdFromEnvelope(bcos::ref(env));
        BOOST_REQUIRE(result.has_value());
        BOOST_CHECK_EQUAL(result.value(), 200u);
    }
    // (d2) Preimage, chainId 255 (0x81 0xFF): payload first byte is a LONG-list header —
    // a distinct pre-fix misclassification branch (lenOfLen path), same regression family.
    {
        auto env = makePreimage(255);
        auto result = envelope::web3ChainIdFromEnvelope(bcos::ref(env));
        BOOST_REQUIRE(result.has_value());
        BOOST_CHECK_EQUAL(result.value(), 255u);
    }
    // (e) 6-field pre-EIP-155 preimage (no chainId) → nullopt (unprotected exemption).
    {
        auto env = makeBareSixField();
        auto result = envelope::web3ChainIdFromEnvelope(bcos::ref(env));
        BOOST_CHECK(!result.has_value());
    }
    // (f) Full envelope v=27/28 (unprotected) → nullopt. Both arms of the exemption.
    {
        auto env = makeFullEnvelope(27);
        auto result = envelope::web3ChainIdFromEnvelope(bcos::ref(env));
        BOOST_CHECK(!result.has_value());
    }
    {
        auto env = makeFullEnvelope(28);
        auto result = envelope::web3ChainIdFromEnvelope(bcos::ref(env));
        BOOST_CHECK(!result.has_value());
    }
    // (g) Full envelope v=37 → chainId 1.
    {
        auto env = makeFullEnvelope(37);
        auto result = envelope::web3ChainIdFromEnvelope(bcos::ref(env));
        BOOST_REQUIRE(result.has_value());
        BOOST_CHECK_EQUAL(result.value(), 1u);
    }
    // (h) Full envelope v=235 (multi-byte) → chainId 100.
    {
        auto env = makeFullEnvelope(235);
        auto result = envelope::web3ChainIdFromEnvelope(bcos::ref(env));
        BOOST_REQUIRE(result.has_value());
        BOOST_CHECK_EQUAL(result.value(), 100u);
    }
    // (i) Malformed v=30 → nullopt.
    {
        auto env = makeFullEnvelope(30);
        auto result = envelope::web3ChainIdFromEnvelope(bcos::ref(env));
        BOOST_CHECK(!result.has_value());
    }

    // (j) Empty input → nullopt.
    {
        bcos::bytes empty;
        auto result = envelope::web3ChainIdFromEnvelope(bcos::ref(empty));
        BOOST_CHECK(!result.has_value());
    }

    // (k) Deposit (0x7E) → nullopt (no chainId field; field 0 is sourceHash).
    {
        bcos::bytes deposit;
        deposit.push_back(static_cast<byte>(bcos::rpc::TransactionType::Deposit));
        auto result = envelope::web3ChainIdFromEnvelope(bcos::ref(deposit));
        BOOST_CHECK(!result.has_value());
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
