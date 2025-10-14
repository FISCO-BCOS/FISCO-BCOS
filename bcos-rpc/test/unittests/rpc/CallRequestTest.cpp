/**
 *  Copyright (C) 2025 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "bcos-rpc/web3jsonrpc/model/CallRequest.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <json/json.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::rpc;

BOOST_AUTO_TEST_SUITE(testCallRequest)

BOOST_AUTO_TEST_CASE(decode_non_object_root)
{
    // Given a non-object JSON value
    Json::Value root(Json::arrayValue);
    auto [ok, req] = decodeCallRequest(root);

    // Then decode should fail and request stays default-initialized
    BOOST_CHECK(!ok);
    BOOST_CHECK(req.to.empty());
    BOOST_CHECK(req.data.empty());
    BOOST_CHECK(!req.from.has_value());
    BOOST_CHECK(!req.gas.has_value());
    BOOST_CHECK(!req.gasPrice.has_value());
    BOOST_CHECK(!req.value.has_value());
    BOOST_CHECK(!req.maxFeePerGas.has_value());
    BOOST_CHECK(!req.maxPriorityFeePerGas.has_value());
}

BOOST_AUTO_TEST_CASE(decode_data_and_input_precedence)
{
    // Given both "data" and "input" present, "data" should take precedence
    Json::Value root(Json::objectValue);
    root["data"] = "0xdeadbeef";
    root["input"] = "0x1122";
    root["to"] = "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

    auto [ok, req] = decodeCallRequest(root);
    BOOST_CHECK(ok);
    constexpr std::string_view hexData = "0xdeadbeef";
    auto expected = fromHexWithPrefix(hexData);
    BOOST_CHECK(req.data == expected);
    BOOST_CHECK_EQUAL(req.to, "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
}

BOOST_AUTO_TEST_CASE(decode_all_fields)
{
    // Given a full call object with all supported fields
    Json::Value root(Json::objectValue);
    root["from"] = "0x1234567890abcdef1234567890abcdef12345678";
    root["to"] = "0xabcdefabcdefabcdefabcdefabcdefabcdefabcd";
    root["data"] = "0x010203";
    root["gas"] = "0x5208";  // 21000
    root["gasPrice"] = "0x1";
    root["value"] = "0x2";
    root["maxPriorityFeePerGas"] = "0x3";
    root["maxFeePerGas"] = "0x4";

    auto [ok, req] = decodeCallRequest(root);
    BOOST_CHECK(ok);

    BOOST_CHECK(req.from.has_value());
    BOOST_CHECK_EQUAL(req.from.value(), "0x1234567890abcdef1234567890abcdef12345678");
    BOOST_CHECK_EQUAL(req.to, "0xabcdefabcdefabcdefabcdefabcdefabcdefabcd");
    constexpr std::string_view dataHex = "0x010203";
    BOOST_CHECK(req.data == fromHexWithPrefix(dataHex));

    BOOST_CHECK(req.gas.has_value());
    BOOST_CHECK_EQUAL(req.gas.value(), 21000);

    BOOST_CHECK(req.gasPrice.has_value());
    BOOST_CHECK_EQUAL(req.gasPrice.value(), "0x1");

    BOOST_CHECK(req.value.has_value());
    BOOST_CHECK_EQUAL(req.value.value(), "0x2");

    BOOST_CHECK(req.maxPriorityFeePerGas.has_value());
    BOOST_CHECK_EQUAL(req.maxPriorityFeePerGas.value(), "0x3");

    BOOST_CHECK(req.maxFeePerGas.has_value());
    BOOST_CHECK_EQUAL(req.maxFeePerGas.value(), "0x4");
}

BOOST_AUTO_TEST_CASE(decode_invalid_hex_data_is_ignored)
{
    // Given invalid hex in data, it should be ignored (kept empty) and not throw
    Json::Value root(Json::objectValue);
    root["data"] = "0xzzzz";  // invalid hex
    root["to"] = "0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

    auto [ok, req] = decodeCallRequest(root);
    BOOST_CHECK(ok);
    BOOST_CHECK(req.data.empty());
    BOOST_CHECK_EQUAL(req.to, "0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
}

BOOST_AUTO_TEST_CASE(decode_uses_input_when_data_missing)
{
    Json::Value root(Json::objectValue);
    root["input"] = "0xabcdef";
    auto [ok, req] = decodeCallRequest(root);
    BOOST_CHECK(ok);
    constexpr std::string_view inputHex = "0xabcdef";
    BOOST_CHECK(req.data == fromHexWithPrefix(inputHex));
}

BOOST_AUTO_TEST_CASE(decode_invalid_gas_string)
{
    Json::Value root(Json::objectValue);
    root["gas"] = "zz";  // invalid hex with no leading valid digit -> stoull throws -> terminate
    // Call function; expected to abort. If not aborted, exit(0) to mark failure in parent.
    BOOST_CHECK_THROW(auto resultTuple = decodeCallRequest(root), std::invalid_argument);
    int status = 0;
}

BOOST_AUTO_TEST_CASE(decode_numeric_gas_treated_as_hex)
{
    // Given gas as a numeric JSON value, asString() yields decimal text
    // but fromQuantity parses with base-16; verify current behavior is hex parse
    constexpr long long kGasDecimal = 21000;
    constexpr unsigned kExpectedHexParsed = 135168U;  // 0x21000
    Json::Value root(Json::objectValue);
    root["gas"] = Json::Int64(kGasDecimal);  // asString => "21000"; hex 0x21000 == 135168
    auto [ok, req] = decodeCallRequest(root);
    BOOST_CHECK(ok);
    BOOST_CHECK(req.gas.has_value());
    BOOST_CHECK_EQUAL(req.gas.value(), kExpectedHexParsed);
}

BOOST_AUTO_TEST_CASE(decode_invalid_from_does_not_fail)
{
    // from is passed through without validation at decode stage
    Json::Value root(Json::objectValue);
    root["from"] = "not_an_address";
    auto [ok, req] = decodeCallRequest(root);
    BOOST_CHECK(ok);
    BOOST_CHECK(req.from.has_value());
    BOOST_CHECK_EQUAL(req.from.value(), "not_an_address");
}

BOOST_AUTO_TEST_CASE(decode_invalid_fee_fields_pass_through)
{
    // gasPrice/value/max* are kept as-is; invalid strings are not parsed here
    Json::Value root(Json::objectValue);
    root["gasPrice"] = "0xzz";
    root["value"] = "-123";
    root["maxPriorityFeePerGas"] = "abc";
    root["maxFeePerGas"] = "\t";
    auto [ok, req] = decodeCallRequest(root);
    BOOST_CHECK(ok);
    BOOST_CHECK(req.gasPrice.has_value());
    BOOST_CHECK_EQUAL(req.gasPrice.value(), "0xzz");
    BOOST_CHECK(req.value.has_value());
    BOOST_CHECK_EQUAL(req.value.value(), "-123");
    BOOST_CHECK(req.maxPriorityFeePerGas.has_value());
    BOOST_CHECK_EQUAL(req.maxPriorityFeePerGas.value(), "abc");
    BOOST_CHECK(req.maxFeePerGas.has_value());
    BOOST_CHECK_EQUAL(req.maxFeePerGas.value(), "\t");
}

BOOST_AUTO_TEST_SUITE_END()
