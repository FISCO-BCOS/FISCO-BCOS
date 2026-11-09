/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
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
 * @brief Two execution rules the EEST tests@v20.0.2 sweep found missing, pinned at the
 *        executor's own entry points: a transaction whose signed envelope binds another chain
 *        id fails validation with INVALID_CHAIN_ID (block import has no admission stage to
 *        refuse it), and a top-level CREATE from a sender at nonce 2^64-2 succeeds (EIP-2681
 *        limits the creator's nonce, which the transition has already bumped to 2^64-1).
 * @file TestEthereumValidationRules.cpp
 */
#include "bcos-codec/rlp/RLPEncode.h"
#include "bcos-framework/testutils/faker/FakeBlock.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-task/Wait.h"
#include "ethereum-executor/EthExecutionPolicy.h"
#include "ethereum-executor/EthereumState.h"
#include "ethereum-executor/EthereumTransition.h"
#include "ethereum-executor/tests/TestMemoryStorage.h"
#include <evmone/evmone.h>
#include <boost/test/unit_test.hpp>
#include <evmc/evmc.hpp>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>

using namespace bcos;
using namespace bcos::executor_v1;

namespace
{
class ValidationTestTransactionImpl : public bcostars::protocol::TransactionImpl
{
public:
    void markClean() { setTainted(false); }
};

evmc::address senderAddress()
{
    evmc::address addr{};
    addr.bytes[19] = 0x77;
    return addr;
}

/// An EIP-1559 envelope signed for @p chainId: `to` empty = contract creation with empty
/// initcode, otherwise a value transfer. The signature bytes are shape-only (the sender is
/// forced), which is all validateTransaction / runTransaction read.
std::shared_ptr<ValidationTestTransactionImpl> makeTx(
    uint64_t chainId, std::optional<evmc::address> to, uint64_t nonce)
{
    auto tx = std::make_shared<ValidationTestTransactionImpl>();
    auto& inner = tx->mutableInner();
    inner.data.version = 1;
    inner.data.to =
        to ? bcos::toHexStringWithPrefix(bcos::bytes(std::begin(to->bytes), std::end(to->bytes))) :
             std::string{};
    inner.data.blockLimit = 1000;
    inner.data.chainID = "0x" + [&] {
        std::ostringstream oss;
        oss << std::hex << chainId;
        return oss.str();
    }();
    inner.data.nonce = [&] {  // a web3 quantity: hex, like the envelope field
        std::ostringstream oss;
        oss << "0x" << std::hex << nonce;
        return oss.str();
    }();
    inner.data.value = "0x0";
    inner.data.gasPrice = "0x0";
    inner.data.gasLimit = 100000;
    inner.data.maxFeePerGas = "0x3b9aca00";
    inner.data.maxPriorityFeePerGas = "0x0";
    inner.type = static_cast<int>(bcos::protocol::TransactionType::Web3Transaction);
    inner.web3TypedTxKind = 2;

    bcos::bytes body;
    bcos::codec::rlp::encode(body, chainId);
    bcos::codec::rlp::encode(body, nonce);
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(0));
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(1000000000));
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(100000));
    if (to)
    {
        bcos::codec::rlp::encode(
            body, bcos::Address(bcos::bytesConstRef(to->bytes, sizeof(to->bytes))));
    }
    else
    {
        bcos::codec::rlp::encode(body, bcos::bytes{});  // empty `to` = creation
    }
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(0));  // value
    bcos::codec::rlp::encode(body, bcos::bytes{});             // data
    body.push_back(bcos::codec::rlp::LIST_HEAD_BASE);          // empty accessList
    bcos::bytes payload;
    payload.push_back(0x02);
    bcos::codec::rlp::encodeHeader(
        payload, bcos::codec::rlp::Header{.isList = true, .payloadLength = body.size()});
    payload.insert(payload.end(), body.begin(), body.end());
    inner.extraTransactionBytes.assign(payload.begin(), payload.end());

    bcos::bytes signature(65, 0);
    signature[31] = 0x12;
    signature[63] = 0x34;
    signature[64] = 0x01;
    inner.signature.assign(signature.begin(), signature.end());

    auto const sender = senderAddress();
    tx->forceSender(bcos::bytes(std::begin(sender.bytes), std::end(sender.bytes)));
    tx->calculateHash(*bcos::test::createNormalCryptoSuite()->hashImpl());
    tx->markClean();
    return tx;
}

eth::EthBlockInfo londonBlock()
{
    eth::EthBlockInfo block;
    block.number = 1;
    block.timestamp = 1000;
    block.gas_limit = 30000000;
    block.base_fee = 1000000000;
    return block;
}

eth::EthereumState<MutableStorage>& fundSender(
    eth::EthereumState<MutableStorage>& state, uint64_t nonce)
{
    auto& acc = state.get_or_insert(senderAddress());
    acc.balance = bcos::u256(1) << 100;
    acc.nonce = nonce;
    return state;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(EthereumValidationRulesTest)

BOOST_AUTO_TEST_CASE(transactionForAnotherChainIsInvalid)
{
    auto const recipient = evmc::address{};
    auto const tx = makeTx(/*chainId=*/1, recipient, /*nonce=*/0);
    auto validate = [&](std::optional<uint64_t> nodeChainId) {
        MutableStorage storage;
        eth::EthereumState<MutableStorage> state{storage};
        fundSender(state, 0);
        return eth::validateTransaction(state, londonBlock(), *tx, EVMC_LONDON, 30000000, 0,
            eth::EthCallParams{}, eth::EthL1Policy{}, nodeChainId);
    };

    // Positive controls: the node's chain, and a node with no chain id configured.
    BOOST_CHECK(std::holds_alternative<eth::EthTxProperties>(validate(1)));
    BOOST_CHECK(std::holds_alternative<eth::EthTxProperties>(validate(std::nullopt)));
    BOOST_CHECK(std::holds_alternative<eth::EthTxProperties>(validate(0)));

    // The discriminating case: signed for chain 1, executed by chain 2.
    auto result = validate(2);
    BOOST_REQUIRE(std::holds_alternative<std::error_code>(result));
    BOOST_CHECK_EQUAL(std::get<std::error_code>(result).value(),
        static_cast<int>(eth::evm::ErrorCode::INVALID_CHAIN_ID));
}

BOOST_AUTO_TEST_CASE(topLevelCreateFromNonceMaxMinusOneSucceeds)
{
    constexpr uint64_t kNonceMaxMinusOne = std::numeric_limits<uint64_t>::max() - 1;
    auto const tx = makeTx(/*chainId=*/1, std::nullopt, kNonceMaxMinusOne);
    MutableStorage storage;
    eth::EthereumState<MutableStorage> state{storage};
    fundSender(state, kNonceMaxMinusOne);

    auto validated = eth::validateTransaction(state, londonBlock(), *tx, EVMC_LONDON, 30000000, 0,
        eth::EthCallParams{}, eth::EthL1Policy{}, 1);
    BOOST_REQUIRE_MESSAGE(std::holds_alternative<eth::EthTxProperties>(validated),
        "validation refused the creation: " +
            (std::holds_alternative<std::error_code>(validated) ?
                    std::get<std::error_code>(validated).message() :
                    std::string("?")));

    evmc::VM vm{evmc_create_evmone()};
    auto cryptoSuite = bcos::test::createNormalCryptoSuite();
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory(cryptoSuite);
    auto receipt = task::syncWait(eth::runTransaction(state, londonBlock(), {}, *tx, EVMC_LONDON,
        vm, std::get<eth::EthTxProperties>(validated), /*chainId=*/1, eth::EthCallParams{},
        receiptFactory, /*blockNumber=*/1));
    BOOST_REQUIRE(receipt);
    BOOST_CHECK_EQUAL(receipt->status(), 0);

    // The contract lives at CREATE(sender, 2^64-2) with the post-Spurious-Dragon nonce 1, and
    // the sender now sits at the limit. Before the fix prepare_message refused the creation
    // because it tested the already-bumped sender nonce against the limit.
    auto const created = eth::evm::compute_create_address(senderAddress(), kNonceMaxMinusOne);
    auto const* contract = state.find(created);
    BOOST_REQUIRE(contract != nullptr);
    BOOST_CHECK_EQUAL(contract->nonce, 1u);
    BOOST_CHECK_EQUAL(state.get(senderAddress()).nonce, std::numeric_limits<uint64_t>::max());
}

BOOST_AUTO_TEST_SUITE_END()
