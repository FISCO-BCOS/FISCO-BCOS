// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
// ReceiptResponseTest.cpp — op-geth parity for the receipt JSON emitted by
// combineReceiptResponseFromWeb3: the OP extension fields decoded from opReceiptMeta (l1GasUsed,
// operatorFee) and the effectiveGasPrice verbatim passthrough. The tx envelope is a real EIP-2930
// signed tx, decoded exactly as EthEndpoint's OP fallback path does (tryResolveOpReceipt).
//
// Boost.Test style — test-bcos-rpc is a Boost unit_test_framework binary (unittests/main/main.cpp
// defines BOOST_TEST_MAIN); gtest is not available in this target.

#include "bcos-rpc/web3jsonrpc/model/ReceiptResponse.h"
#include "bcos-codec/rlp/OpReceiptMetaCodec.h"
#include "bcos-codec/rlp/RLPDecode.h"
#include "bcos-rpc/web3jsonrpc/model/Web3Transaction.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <json/json.h>
#include <boost/test/unit_test.hpp>
#include <string_view>

using namespace bcos;

// A real EIP-2930 signed envelope. Same header as the opstack-executor fixture
// (OpstackExecutorTest.cpp:54-60: chainId=5, nonce=7, gasPrice=0x06fc23ac00, to
// 0x811a752c8cd697e3cb27279c330ed1ada745a8d7, access list [de0b...7bae->[3,7], bb9b...9413->[]]),
// but re-signed with a valid secp256k1 signature — the fixture's original r/s/v is not a valid
// ECDSA signature (r is not a curve point), which throws InvalidSignature from tx.sender() inside
// combineReceiptResponseFromWeb3. Sender of this envelope:
// 0xe80f05d47864eaaa8382155a5a51b91175231241.
constexpr std::string_view kRawEip2930Tx =
    "0x01f8f205078506fc23ac008357b58494811a752c8cd697e3cb27279c330ed1ada745a8d7881bc16d674ec8000090"
    "6eba"
    "f477f83e051589c1188bcc6ddccdf872f85994de0b295669a9fd93d5f28d9ec85e40f4cb697baef842a00000000000"
    "00"
    "0000000000000000000000000000000000000000000000000003a00000000000000000000000000000000000000000"
    "00"
    "0000000000000000000007d694bb9bc244d798123fde783fcc1c72d3bb8c189413c001a01059e1dc6d85120527dfcb"
    "1e"
    "7cb1b8ad9d6e487e0e37c7f97c8d083629d4a5ada0253c9b60382a6b73a9e6ca1f1a06735d4cb73f75fc90bc84e88a"
    "06"
    "f0edc8f157";

BOOST_AUTO_TEST_CASE(EmitsOpExtensionFieldsFromOpReceiptMeta)
{
    // 1. Blob carrying the two fields this branch adds.
    codec::rlp::OpReceiptMetaFields fields;
    fields.l1_gas_used = 100;
    fields.operator_fee = bytes{0x03, 0xe8};  // 0x3e8 = 1000
    auto blob = codec::rlp::encodeOpReceiptMeta(fields);

    // 2. Receipt: basic fields via the factory, then the blob + a non-empty effectiveGasPrice.
    auto cryptoSuite = std::make_shared<crypto::CryptoSuite>(
        std::make_shared<crypto::Keccak256>(), nullptr, nullptr);
    bcostars::protocol::TransactionReceiptFactoryImpl factory{cryptoSuite};
    auto receipt = factory.createReceipt(
        u256{21000}, std::string{}, protocol::LogEntries{}, 1, bytesConstRef{}, 1);
    receipt->setOpReceiptMeta(std::string(blob.begin(), blob.end()));
    receipt->setCumulativeGasUsed("0x1");
    receipt->setTransactionIndex(0);
    receipt->setLogIndex(0);
    receipt->setEffectiveGasPrice("0x2d37aabb");  // G3 output passes through verbatim

    // 3. Decode a real EIP-2930 envelope into a Web3Transaction (EthEndpoint fallback path).
    auto txBytes = fromHexWithPrefix(std::string(kRawEip2930Tx));
    auto bRef = ref(txBytes);
    rpc::Web3Transaction w3{};
    BOOST_REQUIRE_EQUAL(codec::rlp::decode(bRef, w3), nullptr);

    // 4. Combine.
    Json::Value result(Json::objectValue);
    rpc::combineReceiptResponseFromWeb3(result, w3, *receipt, crypto::HashType{});

    // 5. OP extension fields + effectiveGasPrice verbatim.
    BOOST_CHECK_EQUAL(result["l1GasUsed"].asString(), "0x64");
    BOOST_CHECK_EQUAL(result["operatorFee"].asString(), "0x3e8");
    BOOST_CHECK_EQUAL(result["effectiveGasPrice"].asString(), "0x2d37aabb");
}

BOOST_AUTO_TEST_CASE(MissingMetaEmitsNoOpFields)
{
    // A receipt with empty opReceiptMeta (legacy path) must not emit l1GasUsed/operatorFee.
    auto cryptoSuite = std::make_shared<crypto::CryptoSuite>(
        std::make_shared<crypto::Keccak256>(), nullptr, nullptr);
    bcostars::protocol::TransactionReceiptFactoryImpl factory{cryptoSuite};
    auto receipt = factory.createReceipt(
        u256{21000}, std::string{}, protocol::LogEntries{}, 1, bytesConstRef{}, 1);

    auto txBytes = fromHexWithPrefix(std::string(kRawEip2930Tx));
    auto bRef = ref(txBytes);
    rpc::Web3Transaction w3{};
    BOOST_REQUIRE_EQUAL(codec::rlp::decode(bRef, w3), nullptr);

    Json::Value result(Json::objectValue);
    rpc::combineReceiptResponseFromWeb3(result, w3, *receipt, crypto::HashType{});

    BOOST_CHECK(!result.isMember("l1GasUsed"));
    BOOST_CHECK(!result.isMember("operatorFee"));
    BOOST_CHECK_EQUAL(result["effectiveGasPrice"].asString(), "0x0");  // empty -> fallback
}
