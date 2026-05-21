/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @file Web3Eip7702TransactionTest.cpp
 * @brief EIP-7702 (type-4) Web3Transaction RLP and Tars bridging tests (spec §9.1).
 */

#include "../common/RPCFixture.h"
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-rpc/web3jsonrpc/model/Web3Transaction.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::rpc;
using namespace bcos::codec::rlp;

namespace bcos::test
{
namespace
{
Web3Transaction makeRoundTripEip7702Tx()
{
    Web3Transaction tx;
    tx.type = rpc::TransactionType::EIP7702;
    tx.chainId = 1;
    tx.nonce = 3;
    tx.maxPriorityFeePerGas = 2;
    tx.maxFeePerGas = 3;
    tx.gasLimit = 21000;
    tx.to = Address("0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    tx.value = 0;
    tx.data = bytes{0xde, 0xad};

    AuthorizationListEntry auth;
    auth.chainId = 1;
    auth.address = Address("0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    auth.nonce = 5;
    auth.yParity = 1;
    auth.r = h256(fromHex("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    auth.s = h256(fromHex("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
    tx.authorizationList.push_back(auth);

    tx.signatureV = 1;
    tx.signatureR = bytes(32, 0x11);
    tx.signatureS = bytes(32, 0x22);
    return tx;
}
}  // namespace

BOOST_FIXTURE_TEST_SUITE(testWeb3Eip7702, RPCFixture)

BOOST_AUTO_TEST_CASE(decodeEncodeEip7702_roundTrip)
{
    auto const original = makeRoundTripEip7702Tx();
    bytes encoded;
    encode(encoded, original);

    auto input = ref(encoded);
    Web3Transaction decoded;
    auto const err = decode(input, decoded);
    BOOST_REQUIRE(err == nullptr);
    BOOST_CHECK(decoded.type == rpc::TransactionType::EIP7702);
    BOOST_CHECK_EQUAL(decoded.chainId.value_or(0), 1U);
    BOOST_CHECK_EQUAL(decoded.nonce, 3U);
    BOOST_CHECK_EQUAL(decoded.gasLimit, 21000U);
    BOOST_CHECK_EQUAL(decoded.to.value(), original.to.value());
    BOOST_CHECK_EQUAL(decoded.data, original.data);
    BOOST_REQUIRE_EQUAL(decoded.authorizationList.size(), 1U);
    BOOST_CHECK(decoded.authorizationList[0] == original.authorizationList[0]);

    bytes reencoded;
    encode(reencoded, decoded);
    BOOST_CHECK_EQUAL(toHexStringWithPrefix(encoded), toHexStringWithPrefix(reencoded));
}

BOOST_AUTO_TEST_CASE(decodeTransaction_rejectsEmptyAuthorizationList)
{
    auto tx = makeRoundTripEip7702Tx();
    tx.authorizationList.clear();
    bytes encoded;
    encode(encoded, tx);

    auto input = ref(encoded);
    Web3Transaction decoded;
    auto const err = decode(input, decoded);
    BOOST_REQUIRE(err != nullptr);
    BOOST_CHECK(
        err->errorMessage().find("authorization_list must not be empty") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(takeToTarsTransaction_preservesAuthorizationList)
{
    auto w3 = makeRoundTripEip7702Tx();
    auto tarsTx = w3.takeToTarsTransaction();

    BOOST_CHECK_EQUAL(static_cast<uint8_t>(tarsTx.web3TypedTxKind),
        static_cast<uint8_t>(rpc::TransactionType::EIP7702));
    BOOST_REQUIRE_EQUAL(tarsTx.data.authorizationList.size(), 1U);
    auto const& entry = tarsTx.data.authorizationList[0];
    BOOST_CHECK_EQUAL(entry.chainId, "1");
    BOOST_CHECK_EQUAL(entry.nonce, "5");
    BOOST_CHECK_EQUAL(entry.address, w3.authorizationList[0].address.hex());
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(entry.yParity), w3.authorizationList[0].yParity);
    BOOST_CHECK_EQUAL(toHex(entry.r), w3.authorizationList[0].r.hex());
    BOOST_CHECK_EQUAL(toHex(entry.s), w3.authorizationList[0].s.hex());
    BOOST_REQUIRE(!tarsTx.extraTransactionBytes.empty());

    auto holder = std::make_shared<bcostars::Transaction>(std::move(tarsTx));
    bcostars::protocol::TransactionImpl txImpl(
        std::function<bcostars::Transaction*()>([holder]() { return holder.get(); }));
    auto const& authList = txImpl.web3AuthorizationList();
    BOOST_REQUIRE_EQUAL(authList.size(), 1U);
    BOOST_CHECK_EQUAL(authList[0].chainIdDec, "1");
    BOOST_CHECK_EQUAL(authList[0].nonceDec, "5");
    BOOST_CHECK_EQUAL(authList[0].addressHex, w3.authorizationList[0].address.hex());

    auto keccak = std::make_shared<crypto::Keccak256>();
    auto secp = std::make_shared<crypto::Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<crypto::CryptoSuite>(keccak, secp, nullptr);
    bcostars::protocol::TransactionFactoryImpl factory(cryptoSuite);

    bytes buffer;
    txImpl.encode(buffer);
    auto decoded = factory.createTransaction(ref(buffer), false, false, false);
    BOOST_REQUIRE_EQUAL(decoded->web3AuthorizationList().size(), 1U);
    BOOST_CHECK_EQUAL(decoded->web3AuthorizationList()[0].chainIdDec, "1");
    BOOST_CHECK_EQUAL(decoded->web3AuthorizationList()[0].nonceDec, "5");
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
