/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-cpp-sdk/rpc/JsonRpcRequest.h>
#include <boost/test/unit_test.hpp>

using namespace bcos::cppsdk::jsonrpc;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(JsonRpcRequestTest)

BOOST_AUTO_TEST_CASE(jsonRpcExceptionCarriesCodeAndMessage)
{
    JsonRpcException e(JsonRpcError::InvalidParams, "bad params");
    BOOST_CHECK_EQUAL(e.code(), JsonRpcError::InvalidParams);
    BOOST_CHECK_EQUAL(e.msg(), "bad params");
    BOOST_CHECK_EQUAL(std::string(e.what()), "bad params");
}

BOOST_AUTO_TEST_CASE(defaultFieldsHaveSpecCompliantValues)
{
    JsonRpcRequest req;
    BOOST_CHECK_EQUAL(req.jsonrpc(), "2.0");
    BOOST_CHECK_EQUAL(req.id(), 1);
    BOOST_CHECK(req.method().empty());
}

BOOST_AUTO_TEST_CASE(settersRoundTrip)
{
    JsonRpcRequest req;
    req.setJsonrpc("2.0");
    req.setMethod("eth_blockNumber");
    req.setId(42);
    Json::Value params(Json::arrayValue);
    params.append("foo");
    req.setParams(params);

    BOOST_CHECK_EQUAL(req.jsonrpc(), "2.0");
    BOOST_CHECK_EQUAL(req.method(), "eth_blockNumber");
    BOOST_CHECK_EQUAL(req.id(), 42);
    BOOST_REQUIRE(req.params().isArray());
    BOOST_CHECK_EQUAL(req.params()[0].asString(), "foo");
}

BOOST_AUTO_TEST_CASE(toJsonProducesParseableEnvelope)
{
    JsonRpcRequest req;
    req.setMethod("getBlockNumber");
    req.setId(7);
    Json::Value params(Json::arrayValue);
    params.append("group0");
    req.setParams(params);

    std::string out = req.toJson();
    Json::Reader reader;
    Json::Value root;
    BOOST_REQUIRE(reader.parse(out, root));
    BOOST_CHECK_EQUAL(root["jsonrpc"].asString(), "2.0");
    BOOST_CHECK_EQUAL(root["method"].asString(), "getBlockNumber");
    BOOST_CHECK_EQUAL(root["id"].asInt64(), 7);
    BOOST_REQUIRE(root["params"].isArray());
    BOOST_CHECK_EQUAL(root["params"][0].asString(), "group0");
}

BOOST_AUTO_TEST_CASE(fromJsonHappyPathPopulatesAllFields)
{
    JsonRpcRequest req;
    std::string raw = R"({"jsonrpc":"2.0","method":"foo","id":99,"params":["a","b"]})";
    BOOST_REQUIRE_NO_THROW(req.fromJson(raw));
    BOOST_CHECK_EQUAL(req.jsonrpc(), "2.0");
    BOOST_CHECK_EQUAL(req.method(), "foo");
    BOOST_CHECK_EQUAL(req.id(), 99);
    BOOST_REQUIRE_EQUAL(req.params().size(), 2U);
    BOOST_CHECK_EQUAL(req.params()[0].asString(), "a");
}

BOOST_AUTO_TEST_CASE(fromJsonAcceptsRequestWithoutId)
{
    JsonRpcRequest req;
    std::string raw = R"({"jsonrpc":"2.0","method":"notify","params":[]})";
    BOOST_REQUIRE_NO_THROW(req.fromJson(raw));
    BOOST_CHECK_EQUAL(req.method(), "notify");
    BOOST_CHECK_EQUAL(req.id(), 0);  // default when "id" absent
}

BOOST_AUTO_TEST_CASE(fromJsonMissingJsonrpcThrowsInvalidRequest)
{
    JsonRpcRequest req;
    std::string raw = R"({"method":"foo","params":[]})";
    BOOST_CHECK_EXCEPTION(req.fromJson(raw), JsonRpcException,
        [](const JsonRpcException& e) { return e.code() == JsonRpcError::InvalidRequest; });
}

BOOST_AUTO_TEST_CASE(fromJsonMissingMethodThrowsInvalidRequest)
{
    JsonRpcRequest req;
    std::string raw = R"({"jsonrpc":"2.0","params":[]})";
    BOOST_CHECK_EXCEPTION(req.fromJson(raw), JsonRpcException,
        [](const JsonRpcException& e) { return e.code() == JsonRpcError::InvalidRequest; });
}

BOOST_AUTO_TEST_CASE(fromJsonMissingParamsThrowsInvalidRequest)
{
    JsonRpcRequest req;
    std::string raw = R"({"jsonrpc":"2.0","method":"foo"})";
    BOOST_CHECK_EXCEPTION(req.fromJson(raw), JsonRpcException,
        [](const JsonRpcException& e) { return e.code() == JsonRpcError::InvalidRequest; });
}

BOOST_AUTO_TEST_CASE(fromJsonParamsNotArrayThrowsInvalidRequest)
{
    JsonRpcRequest req;
    std::string raw = R"({"jsonrpc":"2.0","method":"foo","params":{"k":"v"}})";
    BOOST_CHECK_EXCEPTION(req.fromJson(raw), JsonRpcException,
        [](const JsonRpcException& e) { return e.code() == JsonRpcError::InvalidRequest; });
}

// Unparseable input: Json::Reader::parse() returns false, reported as
// InvalidRequest (the do/while break path).
BOOST_AUTO_TEST_CASE(fromJsonUnparseableReportsInvalidRequest)
{
    JsonRpcRequest req;
    BOOST_CHECK_EXCEPTION(req.fromJson("not json"), JsonRpcException,
        [](const JsonRpcException& e) { return e.code() == JsonRpcError::InvalidRequest; });
}

// Parses cleanly but a field has the wrong type: id is a string, so asInt64()
// throws during extraction and is caught and reported as ParseError -- a
// different branch from the parse-false InvalidRequest path above.
BOOST_AUTO_TEST_CASE(fromJsonFieldTypeErrorReportsParseError)
{
    JsonRpcRequest req;
    std::string raw = R"({"jsonrpc":"2.0","method":"foo","id":"not-an-int"})";
    BOOST_CHECK_EXCEPTION(req.fromJson(raw), JsonRpcException,
        [](const JsonRpcException& e) { return e.code() == JsonRpcError::ParseError; });
}

BOOST_AUTO_TEST_CASE(factoryAssignsMonotonicallyIncreasingIds)
{
    JsonRpcRequestFactory factory;
    auto r1 = factory.buildRequest();
    auto r2 = factory.buildRequest();
    auto r3 = factory.buildRequest("doStuff", Json::Value(Json::arrayValue));
    BOOST_REQUIRE(r1);
    BOOST_REQUIRE(r2);
    BOOST_REQUIRE(r3);
    BOOST_CHECK_LT(r1->id(), r2->id());
    BOOST_CHECK_LT(r2->id(), r3->id());
    BOOST_CHECK_EQUAL(r3->method(), "doStuff");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
