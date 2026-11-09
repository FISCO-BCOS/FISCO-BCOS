/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/protocol/LogEntry.h>
#include <bcos-rpc/event/EventSubMatcher.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::event;

namespace bcos::test
{
namespace
{
// matches(params, logEntry) is protected in EventSubMatcher; expose it.
class TestMatcher : public EventSubMatcher
{
public:
    using EventSubMatcher::matches;
};

protocol::LogEntry makeLog(const std::string& address, const std::vector<h256>& topics)
{
    return protocol::LogEntry(bcos::bytes(address.begin(), address.end()),
        bcos::h256s(topics.begin(), topics.end()), bcos::bytes{});
}
}  // namespace

BOOST_AUTO_TEST_SUITE(EventSubMatcherTest)

BOOST_AUTO_TEST_CASE(emptyParamsMatchAnything)
{
    TestMatcher matcher;
    auto params = std::make_shared<EventSubParams>();
    auto log = makeLog("addr1", {h256(1)});
    BOOST_CHECK(matcher.matches(params, log));
}

BOOST_AUTO_TEST_CASE(addressFilter)
{
    TestMatcher matcher;
    auto params = std::make_shared<EventSubParams>();
    params->addAddress("addr1");

    BOOST_CHECK(matcher.matches(params, makeLog("addr1", {})));   // address in set
    BOOST_CHECK(!matcher.matches(params, makeLog("addr2", {})));  // address not in set
}

BOOST_AUTO_TEST_CASE(topicFilter)
{
    TestMatcher matcher;
    auto params = std::make_shared<EventSubParams>();
    h256 wanted(0x42);
    params->addTopic(0, wanted.hex());

    // log with matching topic[0]
    BOOST_CHECK(matcher.matches(params, makeLog("addr", {wanted})));
    // log with a different topic[0]
    BOOST_CHECK(!matcher.matches(params, makeLog("addr", {h256(0x99)})));
    // log with no topics → cannot satisfy a topic[0] filter
    BOOST_CHECK(!matcher.matches(params, makeLog("addr", {})));
}

// The BLOCK-level overload, which no test in the tree covered. It zips Block::transactions() with
// Block::receipts() and dereferences each transaction, so it depends on the block having been read
// with TRANSACTIONS (bodies). A block read with TRANSACTIONS_HASH fills only the metadata list, and
// the zip then truncates to zero -- the channel matches nothing, silently, with no error.
//
// That is not hypothetical: it is exactly the outage introduced when this call site's read flag was
// "optimized" to TRANSACTIONS_HASH. This case pins the dependency from the matcher's side, so the
// next such edit fails here instead of in production.
BOOST_AUTO_TEST_CASE(blockOverloadNeedsTransactionBodies)
{
    auto suite = std::make_shared<crypto::CryptoSuite>(std::make_shared<crypto::Keccak256>(),
        std::make_shared<crypto::Secp256k1Crypto>(), nullptr);
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory(suite);
    bcostars::protocol::TransactionFactoryImpl txFactory(suite);
    bcostars::protocol::BlockFactoryImpl blockFactory(suite,
        std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(suite),
        std::make_shared<bcostars::protocol::TransactionFactoryImpl>(suite),
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(suite));

    // One log per transaction, so a body-carrying block yields one match each.
    auto const makeReceipt = [&]() {
        std::vector<protocol::LogEntry> logs{makeLog(std::string("addr"), {h256(1)})};
        bcos::bytes out;
        return receiptFactory.createReceipt(u256(21000), "", logs, 0, bcos::ref(out), 7);
    };

    // The shape a TRANSACTIONS read produces: bodies AND receipts.
    auto withBodies = std::make_shared<bcostars::protocol::BlockImpl>();
    for (uint64_t i = 0; i < 2; ++i)
    {
        withBodies->appendTransaction(txFactory.createTransaction(
            i, "to", bcos::bytes{0x01}, std::to_string(i), 100, "chain0", "group0", 0));
        withBodies->appendReceipt(makeReceipt());
    }
    BOOST_REQUIRE_EQUAL(withBodies->transactionsSize(), 2U);
    BOOST_REQUIRE_EQUAL(withBodies->receiptsSize(), 2U);

    TestMatcher matcher;
    auto params = std::make_shared<EventSubParams>();  // no filter: everything matches
    Json::Value result(Json::arrayValue);
    BOOST_CHECK_EQUAL(matcher.matches(params, protocol::Block::ConstPtr{withBodies}, result), 2U);

    // The shape a TRANSACTIONS_HASH read produces: metadata and receipts, no bodies. The zip is
    // empty, so nothing matches -- silently, with no error. This is what the read flag must not do.
    auto bodyless = std::make_shared<bcostars::protocol::BlockImpl>();
    for (uint64_t i = 0; i < 2; ++i)
    {
        bodyless->appendTransactionMetaData(blockFactory.createTransactionMetaData());
        bodyless->appendReceipt(makeReceipt());
    }
    BOOST_REQUIRE_EQUAL(bodyless->transactionsSize(), 0U);
    BOOST_REQUIRE_EQUAL(bodyless->transactionsMetaDataSize(), 2U);
    BOOST_REQUIRE_EQUAL(bodyless->receiptsSize(), 2U);
    Json::Value bodylessResult(Json::arrayValue);
    BOOST_CHECK_EQUAL(
        matcher.matches(params, protocol::Block::ConstPtr{bodyless}, bodylessResult), 0U);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
