/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * @brief Regression test for issue #5553: eth_getLogs (LogMatcher) must report the same
 *        block-wide logIndex as eth_getTransactionReceipt (ReceiptResponse: receipt.logIndex() + i)
 *        once the receipt carries a non-zero logIndex.
 */
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-rpc/filter/LogMatcher.h>
#include <bcos-rpc/web3jsonrpc/model/Web3FilterRequest.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::rpc;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(LogIndexParityTest)

BOOST_AUTO_TEST_CASE(logMatcherUsesReceiptLogIndexBase)
{
    auto suite =
        std::make_shared<bcos::crypto::CryptoSuite>(std::make_shared<bcos::crypto::Keccak256>(),
            std::make_shared<bcos::crypto::Secp256k1Crypto>(), nullptr);
    bcostars::protocol::TransactionReceiptFactoryImpl factory(suite);

    bytes address(20, 0xab);
    std::vector<protocol::LogEntry> logs;
    logs.emplace_back(address, h256s{h256(1U)}, bytes{});
    logs.emplace_back(address, h256s{h256(2U)}, bytes{});
    bytes out;
    auto receipt = factory.createReceipt(u256(21000), "", logs, 0, ref(out), 7);
    receipt->setLogIndex(5);  // second tx in the block, first tx emitted 5 logs

    LogMatcher matcher;
    auto params = std::make_shared<Web3FilterRequest>();  // no address/topic filter: match all
    Json::Value result(Json::arrayValue);
    auto count = matcher.matches(params, h256(9U), *receipt, h256(8U), 1, result);
    BOOST_REQUIRE_EQUAL(count, 2U);
    BOOST_REQUIRE_EQUAL(result.size(), 2U);
    // same formula as ReceiptResponse.cpp: toQuantity(receipt.logIndex() + i)
    BOOST_CHECK_EQUAL(result[0]["logIndex"].asString(), "0x5");
    BOOST_CHECK_EQUAL(result[1]["logIndex"].asString(), "0x6");
}

// The address filter must normalize through logEntryAddressHex like the JSON producers: the OP
// lane stores LogEntry::address as the raw 20 bytes (OpTransition's mapOpLogAddress) while the
// FISCO / eth-mode lane stores the ASCII hex text, and a client always requests "0x"-prefixed
// hex. Concatenating "0x" with the raw form never equals a requested address, so every
// address-filtered query on the OP lane returned an empty result set.
BOOST_AUTO_TEST_CASE(opLaneRawAddressFormMatchesHexFilter)
{
    const bytes rawAddress = {0x7f, 0xa9, 0x68, 0x5f, 0x76, 0x42, 0x3c, 0x1c, 0x27, 0x26, 0x81,
        0xc4, 0x8a, 0x4c, 0xd4, 0x37, 0x7f, 0x68, 0x59, 0x1d};
    const std::string textAddress = "abababababababababababababababababababab";  // eth-mode form

    LogMatcher matcher;

    auto rawParams = std::make_shared<Web3FilterRequest>();
    rawParams->addAddress("0x7fa9685f76423c1c272681c48a4cd4377f68591d");
    BOOST_CHECK(matcher.matches(rawParams, protocol::LogEntry(rawAddress, {}, bytes{0x01})));

    auto textParams = std::make_shared<Web3FilterRequest>();
    textParams->addAddress("0x" + textAddress);
    BOOST_CHECK(matcher.matches(
        textParams, protocol::LogEntry(bytes(textAddress.begin(), textAddress.end()), {}, {})));

    // a different requested address still filters the log out on either lane form
    auto otherParams = std::make_shared<Web3FilterRequest>();
    otherParams->addAddress("0xcccccccccccccccccccccccccccccccccccccccc");
    BOOST_CHECK(!matcher.matches(otherParams, protocol::LogEntry(rawAddress, {}, bytes{0x01})));
    BOOST_CHECK(!matcher.matches(
        otherParams, protocol::LogEntry(bytes(textAddress.begin(), textAddress.end()), {}, {})));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
