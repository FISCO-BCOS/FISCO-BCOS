/**
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file JsonValidatorTest.cpp
 * @author: kyonGuo
 * @date 2024/3/28
 */

#include <bcos-rpc/validator/JsonValidator.h>
#include <boost/test/unit_test.hpp>
#include <json/json.h>

using namespace bcos;
using namespace bcos::rpc;

namespace bcos::test
{

class JsonValidatorFixture
{
public:
    JsonValidatorFixture() = default;

    Json::Value createValidRequest()
    {
        Json::Value request;
        request["jsonrpc"] = "2.0";
        request["method"] = "eth_blockNumber";
        request["params"] = Json::arrayValue;
        request["id"] = Json::UInt64(1);
        return request;
    }
};

BOOST_FIXTURE_TEST_SUITE(testJsonValidator, JsonValidatorFixture)

BOOST_AUTO_TEST_CASE(testValidRequest)
{
    auto request = createValidRequest();
    auto [valid, errorMsg] = JsonValidator::validate(request);
    BOOST_CHECK(valid);
    BOOST_CHECK(errorMsg.empty());
}

BOOST_AUTO_TEST_CASE(testValidRequestWithStringId)
{
    auto request = createValidRequest();
    request["id"] = "test-id-123";
    auto [valid, errorMsg] = JsonValidator::validate(request);
    BOOST_CHECK(valid);
    BOOST_CHECK(errorMsg.empty());
}

BOOST_AUTO_TEST_CASE(testValidRequestWithUInt64Id)
{
    auto request = createValidRequest();
    request["id"] = Json::UInt64(12345);
    auto [valid, errorMsg] = JsonValidator::validate(request);
    BOOST_CHECK(valid);
    BOOST_CHECK(errorMsg.empty());
}

BOOST_AUTO_TEST_CASE(testValidRequestWithParams)
{
    auto request = createValidRequest();
    Json::Value params;
    params.append("0x1");
    params.append(true);
    request["params"] = params;
    auto [valid, errorMsg] = JsonValidator::validate(request);
    BOOST_CHECK(valid);
    BOOST_CHECK(errorMsg.empty());
}

BOOST_AUTO_TEST_CASE(testMissingJsonRpcField)
{
    Json::Value request;
    request["method"] = "eth_blockNumber";
    request["params"] = Json::arrayValue;
    request["id"] = Json::UInt64(1);
    
    auto [valid, errorMsg] = JsonValidator::validate(request);
    BOOST_CHECK(!valid);
    BOOST_CHECK(errorMsg == "Request not valid, required fields: jsonrpc, method, params, id");
}

BOOST_AUTO_TEST_CASE(testMissingMethodField)
{
    Json::Value request;
    request["jsonrpc"] = "2.0";
    request["params"] = Json::arrayValue;
    request["id"] = Json::UInt64(1);
    
    auto [valid, errorMsg] = JsonValidator::validate(request);
    BOOST_CHECK(!valid);
    BOOST_CHECK(errorMsg == "Request not valid, required fields: jsonrpc, method, params, id");
}

BOOST_AUTO_TEST_CASE(testMissingIdField)
{
    Json::Value request;
    request["jsonrpc"] = "2.0";
    request["method"] = "eth_blockNumber";
    request["params"] = Json::arrayValue;
    
    auto [valid, errorMsg] = JsonValidator::validate(request);
    BOOST_CHECK(!valid);
    BOOST_CHECK(errorMsg == "Request not valid, required fields: jsonrpc, method, params, id");
}

BOOST_AUTO_TEST_CASE(testInvalidJsonRpcType)
{
    Json::Value request;
    request["jsonrpc"] = 2.0;  // should be string, not number
    request["method"] = "eth_blockNumber";
    request["params"] = Json::arrayValue;
    request["id"] = Json::UInt64(1);
    
    auto [valid, errorMsg] = JsonValidator::validate(request);
    BOOST_CHECK(!valid);
    BOOST_CHECK(errorMsg == "Invalid field: jsonrpc");
}

BOOST_AUTO_TEST_CASE(testInvalidMethodType)
{
    Json::Value request;
    request["jsonrpc"] = "2.0";
    request["method"] = 123;  // should be string, not number
    request["params"] = Json::arrayValue;
    request["id"] = Json::UInt64(1);
    
    auto [valid, errorMsg] = JsonValidator::validate(request);
    BOOST_CHECK(!valid);
    BOOST_CHECK(errorMsg == "Invalid field: method");
}

BOOST_AUTO_TEST_CASE(testInvalidParamsType)
{
    Json::Value request;
    request["jsonrpc"] = "2.0";
    request["method"] = "eth_blockNumber";
    request["params"] = "invalid";  // should be array, not string
    request["id"] = Json::UInt64(1);
    
    auto [valid, errorMsg] = JsonValidator::validate(request);
    BOOST_CHECK(!valid);
    BOOST_CHECK(errorMsg == "Invalid field: params");
}

BOOST_AUTO_TEST_CASE(testInvalidIdType)
{
    Json::Value request;
    request["jsonrpc"] = "2.0";
    request["method"] = "eth_blockNumber";
    request["params"] = Json::arrayValue;
    request["id"] = true;  // should be string or uint64, not boolean
    
    auto [valid, errorMsg] = JsonValidator::validate(request);
    BOOST_CHECK(!valid);
    BOOST_CHECK(errorMsg == "Invalid field: id");
}

BOOST_AUTO_TEST_CASE(testExtraInvalidField)
{
    auto request = createValidRequest();
    request["extraField"] = "should not be here";
    
    auto [valid, errorMsg] = JsonValidator::validate(request);
    BOOST_CHECK(!valid);
    BOOST_CHECK(errorMsg == "Invalid field: extraField");
}

BOOST_AUTO_TEST_CASE(testCheckRequestFieldsValid)
{
    auto request = createValidRequest();
    auto [valid, errorMsg] = JsonValidator::checkRequestFields(request);
    BOOST_CHECK(valid);
    BOOST_CHECK(errorMsg.empty());
}

BOOST_AUTO_TEST_CASE(testCheckRequestFieldsInvalid)
{
    Json::Value request;
    request["jsonrpc"] = "2.0";
    request["method"] = "eth_blockNumber";
    request["params"] = Json::arrayValue;
    // missing id field
    
    auto [valid, errorMsg] = JsonValidator::checkRequestFields(request);
    BOOST_CHECK(!valid);
    BOOST_CHECK(errorMsg == "Request not valid, required fields: jsonrpc, method, params, id");
}

BOOST_AUTO_TEST_CASE(testValidRequestWithoutParams)
{
    Json::Value request;
    request["jsonrpc"] = "2.0";
    request["method"] = "eth_blockNumber";
    request["id"] = Json::UInt64(1);
    // params is optional based on the flag logic
    
    auto [valid, errorMsg] = JsonValidator::validate(request);
    BOOST_CHECK(valid);
    BOOST_CHECK(errorMsg.empty());
}

BOOST_AUTO_TEST_CASE(testEmptyRequest)
{
    Json::Value request;  // empty request object
    
    auto [valid, errorMsg] = JsonValidator::validate(request);
    BOOST_CHECK(!valid);
    BOOST_CHECK(errorMsg == "Request not valid, required fields: jsonrpc, method, params, id");
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
