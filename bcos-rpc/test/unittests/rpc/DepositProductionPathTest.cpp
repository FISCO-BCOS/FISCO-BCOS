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
 * @file DepositProductionPathTest.cpp
 * @brief Drive a 0x7e envelope through the PRODUCTION deposit entry points — the
 *        receipt overload of combineTxResponse and the gated full-tx fill inside
 *        combineBlockResponse (review F6: the corpus suite pins the renderers
 *        against the goldens, but nothing drove the two live paths before).
 */

#include "../common/RPCFixture.h"
#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-rlp-protocol/Web3Transaction.h>
#include <bcos-rpc/web3jsonrpc/model/BlockResponse.h>
#include <bcos-rpc/web3jsonrpc/model/DepositTransaction.h>
#include <bcos-rpc/web3jsonrpc/model/TransactionResponse.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>

#include <memory>
#include <optional>

namespace
{

/// A valid 0x7e envelope (the wire shape decodeDepositTransaction expects):
/// type byte + RLP list [sourceHash, from, to, mint, value, gas, isSystemTx, input].
bcos::bytes buildDepositEnvelope(bcos::u256 const& value, bcos::u256 const& mintValue)
{
    bcos::bytes body;
    bcos::h256 const sourceHash(std::string(64, '5'));
    bcos::codec::rlp::encode(body, sourceHash);
    bcos::h160 const from(std::string(40, 'd'));
    bcos::codec::rlp::encode(body, from);
    bcos::codec::rlp::encode(body, bcos::h160(std::string(40, 'e')));  // to
    bcos::codec::rlp::encode(body, mintValue);
    bcos::codec::rlp::encode(body, value);
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(21'000));
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(0));  // isSystemTx
    bcos::bytes const input;
    bcos::codec::rlp::encode(body, input);

    bcos::bytes raw;
    raw.push_back(bcos::rpc::c_depositTxType);
    bcos::codec::rlp::encodeHeader(
        raw, bcos::codec::rlp::Header{.isList = true, .payloadLength = body.size()});
    raw.insert(raw.end(), body.begin(), body.end());
    return raw;
}

BOOST_AUTO_TEST_CASE(DecodeDepositEnvelopeYieldsTheConstructedValues)
{
    // review F21: the value-level oracle the corpus suites lack — they hand-fill the
    // struct from the golden JSON and pin only the renderer, so a field-order swap or
    // the doc-admitted uint64 truncation in decodeDepositTransaction shipped green.
    // Here the production decoder runs over real wire bytes and every decoded field
    // is asserted against the values buildDepositEnvelope constructed them from.
    auto const envelope = buildDepositEnvelope(/*value=*/bcos::u256(7), /*mint=*/bcos::u256(11));
    bcos::bytesRef cursor(const_cast<bcos::byte*>(envelope.data()), envelope.size());
    bcos::rpc::DepositTransaction decoded;
    auto const error = bcos::rpc::decodeDepositTransaction(cursor, decoded);
    BOOST_REQUIRE(error == nullptr);
    BOOST_CHECK(decoded.sourceHash == bcos::h256(std::string(64, '5')));
    BOOST_CHECK(decoded.from == bcos::h160(std::string(40, 'd')));
    BOOST_CHECK(decoded.to == bcos::h160(std::string(40, 'e')));
    BOOST_CHECK(decoded.mint == bcos::u256(11));
    BOOST_CHECK(decoded.value == bcos::u256(7));
    BOOST_CHECK_EQUAL(decoded.gas, 21'000u);
    BOOST_CHECK_EQUAL(decoded.isSystemTx, false);
    BOOST_CHECK(decoded.input.empty());
    // type byte + exactly one RLP list: the decoder consumes the whole envelope.
    BOOST_CHECK_EQUAL(cursor.size(), 0u);
}

bcos::protocol::TransactionReceipt::Ptr makeDepositReceipt(
    bcos::protocol::BlockFactory::Ptr const& factory, uint64_t depositNonce)
{
    auto receipt = factory->receiptFactory()->createReceipt(bcos::u256(21'000),
        "0x0000000000000000000000000000000000000000", {}, 1, bcos::bytesConstRef{}, 7);
    bcos::protocol::OpStackReceiptMeta meta;
    meta.deposit_nonce = depositNonce;
    meta.deposit_receipt_version = 1;
    receipt->setOpStackMeta(meta);
    return receipt;
}

bcos::protocol::Transaction::Ptr makeDepositTarsTx(
    bcos::bytes const& envelope, bcos::crypto::Hash const& hashImpl)
{
    // A Web3Transaction gives the tars record its consensus fields; the deposit
    // envelope rides extraTransactionBytes — the exact lane the production gate
    // (Transaction::isDepositTx, the tars override reading the same byte) reads.
    bcos::rpc::Web3Transaction web3Tx;
    web3Tx.type = bcos::rpc::TransactionType::Deposit;
    web3Tx.chainId = 1;
    web3Tx.nonce = 0;
    web3Tx.gasLimit = 21'000;
    web3Tx.to.emplace(bcos::Address(std::string(40, 'e')));
    web3Tx.value = bcos::u256(0);
    auto tarsTx = web3Tx.takeToTarsTransaction();
    tarsTx.extraTransactionBytes.assign(envelope.begin(), envelope.end());
    auto const envelopeHash = hashImpl.hash(bcos::bytesConstRef(envelope.data(), envelope.size()));
    tarsTx.extraTransactionHash.assign(envelopeHash.begin(), envelopeHash.end());
    return std::make_shared<bcostars::protocol::TransactionImpl>(
        [tarsTx = std::move(tarsTx)]() mutable { return &tarsTx; });
}
}  // namespace

BOOST_FIXTURE_TEST_SUITE(DepositProductionPathTest, bcos::test::RPCFixture)

// review F6: the deposit JSON parity is anchored by the golden corpus, but that
// suite calls the renderers directly. This case drives the two PRODUCTION entry
// points end to end — the envelope gate, the decode, and the positional receipt
// pairing — on the same lane eth_getTransactionByHash / eth_getBlockByHash serve.
BOOST_AUTO_TEST_CASE(ReceiptOverloadAndFullTxFillCarryTheDepositBranch)
{
    auto const envelope = buildDepositEnvelope(/*value=*/7, /*mint=*/11);
    auto const tx = makeDepositTarsTx(envelope, *hashImpl);
    auto const receipt = makeDepositReceipt(m_blockFactory, /*depositNonce=*/0xbb9);
    auto const blockHash = bcos::h256(std::string(64, 'a'));

    // (1) The receipt overload every eth_getTransaction* caller with a receipt uses.
    Json::Value txResult = Json::objectValue;
    bcos::rpc::combineTxResponse(txResult, *tx, *receipt, blockHash);
    BOOST_CHECK_EQUAL(txResult["type"].asString(), "0x7e");
    BOOST_REQUIRE_MESSAGE(txResult.isMember("mint"), "the deposit branch ran (mint emitted)");
    // The receipt's deposit nonce lands on `nonce` (deposits carry no account
    // nonce; op-geth overwrites the zero placeholder once the receipt is in scope).
    BOOST_REQUIRE_MESSAGE(txResult["nonce"].asString() == "0xbb9",
        "the receipt overload stamped the deposit nonce over `nonce`, got: "
            << txResult["nonce"].asString());
    BOOST_CHECK(txResult.isMember("depositReceiptVersion"));

    // (2) The gated full-tx fill inside combineBlockResponse: a two-entry block
    // (deposit first) proves the positional receipt pairing, and the non-deposit
    // second entry proves the fill's gate (its nonce stays the account nonce).
    auto block = m_blockFactory->createBlock();
    auto header = m_blockFactory->blockHeaderFactory()->createBlockHeader();
    header->setNumber(7);
    header->setGasUsed(bcos::u256(21'000));
    header->setTimestamp(1'700'000'000'000);
    header->calculateHash(*hashImpl);
    block->setBlockHeader(header);
    block->appendTransaction(tx);
    bcos::rpc::Web3Transaction plain;
    plain.type = bcos::rpc::TransactionType::Legacy;
    plain.chainId = 1;
    plain.nonce = 42;
    plain.gasLimit = 21'000;
    plain.to.emplace(bcos::Address(std::string(40, 'f')));
    plain.value = bcos::u256(0);
    auto plainTars = plain.takeToTarsTransaction();
    auto const plainEnvelope = plain.encode();
    plainTars.extraTransactionBytes.assign(plainEnvelope.begin(), plainEnvelope.end());
    auto const plainHash =
        hashImpl->hash(bcos::bytesConstRef(plainEnvelope.data(), plainEnvelope.size()));
    plainTars.extraTransactionHash.assign(plainHash.begin(), plainHash.end());
    block->appendTransaction(std::make_shared<bcostars::protocol::TransactionImpl>(
        [plainTars = std::move(plainTars)]() mutable { return &plainTars; }));
    block->appendReceipt(receipt);
    auto plainReceipt = makeDepositReceipt(m_blockFactory, 0);
    plainReceipt->setTransactionIndex(1);
    block->appendReceipt(plainReceipt);

    Json::Value blockResult = Json::objectValue;
    bcos::rpc::combineBlockResponse(blockResult, *block, /*fullTxs=*/true);
    BOOST_REQUIRE(blockResult["transactions"].isArray());
    BOOST_REQUIRE_EQUAL(blockResult["transactions"].size(), 2U);
    auto const& deposit = blockResult["transactions"][0u];
    BOOST_CHECK_EQUAL(deposit["type"].asString(), "0x7e");
    BOOST_REQUIRE_MESSAGE(deposit.isMember("mint"), "the block path ran the deposit branch");
    BOOST_REQUIRE_MESSAGE(deposit["nonce"].asString() == "0xbb9",
        "the gated receipt fill stamped the deposit nonce over `nonce`, got: "
            << deposit["nonce"].asString());
    auto const& plainRendered = blockResult["transactions"][1u];
    BOOST_CHECK_MESSAGE(plainRendered["nonce"].asString() == "0x2a",
        "the fill must be gated: a non-deposit tx keeps its account nonce (0x2a), got: "
            << plainRendered["nonce"].asString());
}

BOOST_AUTO_TEST_SUITE_END()
