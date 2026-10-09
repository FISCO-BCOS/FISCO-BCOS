/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-ops/OpsError.h>
#include <bcos-ops/log/TxLookup.h>
#include <boost/test/unit_test.hpp>

namespace bcos::ops::test
{
BOOST_AUTO_TEST_SUITE(TxLookupTest)

BOOST_AUTO_TEST_CASE(receiptWithBlockNumberIsFound)
{
    std::string seenMethod;
    Json::Value seenParams;
    RpcCall call = [&](std::string_view _method, Json::Value const& _params) {
        seenMethod = _method;
        seenParams = _params;
        Json::Value receipt;
        receipt["blockNumber"] = 129;
        receipt["status"] = 0;
        receipt["transactionHash"] = "0x9c0ffee0";
        return receipt;
    };
    auto lookup = lookupTxOnChain(call, "attach", "group0", "0x9c0ffee0");
    BOOST_CHECK_EQUAL(seenMethod, "getTransactionReceipt");
    BOOST_REQUIRE_EQUAL(seenParams.size(), 4U);
    BOOST_CHECK_EQUAL(seenParams[0].asString(), "group0");
    BOOST_CHECK_EQUAL(seenParams[2].asString(), "0x9c0ffee0");
    BOOST_CHECK(seenParams[3].isBool() && !seenParams[3].asBool());
    BOOST_CHECK(lookup.kind == TxLookup::Kind::Found);
    BOOST_CHECK_EQUAL(lookup.blockNumber, 129);
    BOOST_CHECK_EQUAL(lookup.status, 0);
    BOOST_CHECK_EQUAL(lookup.line(), "attach: included in block 129, status 0");
    auto json = lookup.toJson();
    BOOST_CHECK_EQUAL(json["kind"].asString(), "found");
    BOOST_CHECK_EQUAL(json["source"].asString(), "attach");
    BOOST_CHECK_EQUAL(json["blockNumber"].asInt64(), 129);
}

BOOST_AUTO_TEST_CASE(rpcErrorMeansNotOnChainNotUnavailable)
{
    // the node answered: the hash is simply not there. This must read differently from a
    // connect failure, or the operator restarts a healthy node
    RpcCall call = [](std::string_view, Json::Value const&) -> Json::Value {
        throw OpsError(c_exitUsage, "getTransactionReceipt: TransactionReceiptNotFound");
    };
    auto lookup = lookupTxOnChain(call, "rpc", "group0", "0xdeadbeef");
    BOOST_CHECK(lookup.kind == TxLookup::Kind::NotOnChain);
    BOOST_CHECK_EQUAL(
        lookup.line(), "rpc: not on chain (getTransactionReceipt: TransactionReceiptNotFound)");
    BOOST_CHECK_EQUAL(lookup.toJson()["kind"].asString(), "not_on_chain");

    RpcCall nullResult = [](std::string_view, Json::Value const&) { return Json::Value(); };
    auto empty = lookupTxOnChain(nullResult, "rpc", "group0", "0xdeadbeef");
    BOOST_CHECK(empty.kind == TxLookup::Kind::NotOnChain);
    BOOST_CHECK_EQUAL(empty.line(), "rpc: not on chain (no receipt for this hash)");
}

BOOST_AUTO_TEST_CASE(connectFailureIsUnavailable)
{
    auto lookup = TxLookup::unavailable("connect to socket /x/fisco-bcos.ipc failed: refused");
    BOOST_CHECK(lookup.kind == TxLookup::Kind::Unavailable);
    BOOST_CHECK_EQUAL(
        lookup.line(), "rpc: unavailable (connect to socket /x/fisco-bcos.ipc failed: refused)");
    auto json = lookup.toJson();
    BOOST_CHECK_EQUAL(json["kind"].asString(), "unavailable");
    BOOST_CHECK(!json.isMember("source"));
    BOOST_CHECK(!json.isMember("blockNumber"));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
