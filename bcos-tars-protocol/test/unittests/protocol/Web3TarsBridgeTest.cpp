/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file Web3TarsBridgeTest.cpp
 * @brief Round-trip tests for decodeWeb3RawTransaction (protocol/Web3TarsBridge.cpp):
 *        a real signed EIP-1559 envelope and a 0x7e deposit envelope are decoded into
 *        TransactionImpl; fields, the recovered sender and the canonical txHash must
 *        come back intact, and malformed input must throw std::invalid_argument.
 */

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1KeyPair.h>
#include <bcos-rlp-protocol/Web3Transaction.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <memory>

using namespace bcos;
using namespace bcos::crypto;

namespace bcos::test
{
namespace
{
// Fixed key (0xdead...beef) so the expected sender is stable across runs.
std::shared_ptr<Secp256k1KeyPair> bridgeTestKeyPair()
{
    auto secret = std::make_shared<KeyImpl>(
        h256("0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef").asBytes());
    return std::make_shared<Secp256k1KeyPair>(secret);
}
}  // namespace

BOOST_AUTO_TEST_SUITE(Web3TarsBridgeTest)

BOOST_AUTO_TEST_CASE(decodeEip1559RoundTrip)
{
    Keccak256 hashImpl;
    auto keyPair = bridgeTestKeyPair();
    auto const expectedSender = keyPair->address(std::make_shared<Keccak256>());

    rpc::Web3Transaction tx;
    tx.type = rpc::TransactionType::EIP1559;
    tx.chainId = 1;
    tx.nonce = 7;
    tx.maxPriorityFeePerGas = u256(1000000000);
    tx.maxFeePerGas = u256(2000000000);
    tx.gasLimit = 21000;
    tx.to = Address("0x727fc6e68397b6f1234567890abcdef123456789");
    tx.value = u256("1000000000000000");
    tx.data = fromHex("0xdeadbeef");

    auto signature = secp256k1Sign(*keyPair, tx.hashForSign());
    BOOST_REQUIRE_EQUAL(signature->size(), 65u);
    tx.signatureR.assign(signature->begin(), signature->begin() + 32);
    tx.signatureS.assign(signature->begin() + 32, signature->begin() + 64);
    tx.signatureV = (*signature)[64];

    auto raw = tx.encode();
    BOOST_REQUIRE_EQUAL(raw.front(), static_cast<byte>(0x02));

    auto decoded = rpc::decodeWeb3RawTransaction(bytesConstRef(raw.data(), raw.size()), hashImpl);
    BOOST_REQUIRE(decoded);

    BOOST_CHECK_EQUAL(decoded->web3TypedTxKind(), 2u);
    BOOST_CHECK_EQUAL(decoded->nonce(), std::string_view("0x7"));
    BOOST_CHECK_EQUAL(
        decoded->to(), std::string_view("0x727fc6e68397b6f1234567890abcdef123456789"));
    BOOST_CHECK_EQUAL(decoded->value(), u256("1000000000000000"));
    BOOST_CHECK_EQUAL(decoded->gasLimit(), 21000);
    BOOST_REQUIRE(decoded->maxFeePerGas().has_value());
    BOOST_CHECK_EQUAL(*decoded->maxFeePerGas(), u256(2000000000));

    // The sender is recovered from the signature and must be the signing key's address.
    auto decodedSender = decoded->sender();
    BOOST_CHECK_EQUAL(bytes(decodedSender.begin(), decodedSender.end()), expectedSender.asBytes());

    // hash() is the canonical txHash = keccak256 of the raw signed envelope.
    BOOST_CHECK_EQUAL(decoded->hash(), keccak256Hash(bytesConstRef(raw.data(), raw.size())));
}

BOOST_AUTO_TEST_CASE(decodeDepositRoundTrip)
{
    Keccak256 hashImpl;

    rpc::Web3Transaction tx;
    tx.type = rpc::TransactionType::Deposit;
    tx.sourceHash = h256("0x1234abcd1234abcd1234abcd1234abcd1234abcd1234abcd1234abcd1234abcd");
    tx.from = Address("0xdeaddeaddeaddeaddeaddeaddeaddeaddeaddead");
    tx.to = Address("0x727fc6e68397b6f1234567890abcdef123456789");
    tx.mint = u256(0);
    tx.value = u256(5);
    tx.gasLimit = 21000;
    tx.data = {};

    auto raw = tx.encode();
    BOOST_REQUIRE_EQUAL(raw.front(), static_cast<byte>(0x7e));

    auto decoded = rpc::decodeWeb3RawTransaction(bytesConstRef(raw.data(), raw.size()), hashImpl);
    BOOST_REQUIRE(decoded);

    BOOST_CHECK_EQUAL(decoded->web3TypedTxKind(), 0x7eu);
    // Deposit is unsigned: the sender comes from the from field, not EC recovery.
    auto decodedSender = decoded->sender();
    BOOST_CHECK_EQUAL(bytes(decodedSender.begin(), decodedSender.end()), tx.from.asBytes());
    BOOST_CHECK_EQUAL(decoded->hash(), keccak256Hash(bytesConstRef(raw.data(), raw.size())));
}

BOOST_AUTO_TEST_CASE(decodeMalformedThrows)
{
    Keccak256 hashImpl;
    auto garbage = fromHex("0x02ff00");
    BOOST_CHECK_THROW(
        rpc::decodeWeb3RawTransaction(bytesConstRef(garbage.data(), garbage.size()), hashImpl),
        std::invalid_argument);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
