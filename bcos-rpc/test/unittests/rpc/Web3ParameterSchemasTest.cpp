// Copyright (C) 2026 FISCO BCOS.
// SPDX-License-Identifier: Apache-2.0

#include <bcos-rpc/validator/JsonValidator.h>
#include <boost/test/unit_test.hpp>
#include <array>
#include <sstream>
#include <thread>
#include <valijson/valijson_jsoncpp_bundled.hpp>

using bcos::rpc::JsonValidator;

namespace
{
Json::Value parseSchemaJson(std::string_view source)
{
    Json::Value root;
    Json::CharReaderBuilder reader;
    std::istringstream input(std::string{source});
    BOOST_REQUIRE(Json::parseFromStream(reader, input, &root, nullptr));
    return root;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(Web3ParameterSchemas)

BOOST_AUTO_TEST_CASE(pilotTuplesAndStrictTypes)
{
    auto const balance = "eth_getBalance";
    auto const storage = "eth_getStorageAt";
    auto const block = "eth_getBlockByNumber";
    auto check = [](std::string_view method, std::string_view params, std::string_view path = "") {
        auto error = JsonValidator::validateParams(method, parseSchemaJson(params));
        BOOST_CHECK_EQUAL(error.value_or(""), path);
    };

    check(balance, R"(["0XaAaAaAaAaAaAaAaAaAaAaAaAaAaAaAaAaAaAaAaA"])");
    check(balance, R"(["0x0000000000000000000000000000000000000000",null])", "/params/1");
    check(balance, R"(["0x0000000000000000000000000000000000000000",""])", "/params/1");
    check(balance, R"(["0x0000000000000000000000000000000000000000","latest",false])", "/params");
    check(balance, R"(["0x1234","latest"])", "/params/0");
    check(balance, R"([])", "/params");
    check(storage, R"(["0000000000000000000000000000000000000000","0x"])");
    check(storage, R"(["0000000000000000000000000000000000000000",0])", "/params/1");
    check(storage, R"(["0000000000000000000000000000000000000000","0x0",{}])", "/params/2");
    check(block, R"(["0x0",false])");
    check(block, R"(["0x7fffffffffffffff",true])");
    check(block, R"(["0x8000000000000000",true])", "/params/0");
    check(block, R"(["0x0000000000000000000000000000000000000000000000000000000000000000",false])");
    check(block, R"(["latest","false"])", "/params/1");
    check(block, R"(["latest",0])", "/params/1");
    check(block, R"(["latest",null])", "/params/1");
    check(block, R"(["latest\u0000",false])", "/params/0");
    check(block, R"(["0x0\n",false])", "/params/0");
    check(block, R"(["latest"])", "/params");
    check(block, R"([42,false])", "/params/0");
    check(block, R"([{"blockNumber":"0x0"},false])", "/params/0");
    BOOST_CHECK_EQUAL(JsonValidator::validateParams(block, {}).value_or(""), "/params");
    BOOST_CHECK(!JsonValidator::validateParams("eth_call", Json::Value(42)));

    Json::Value oversized(Json::arrayValue);
    oversized.append(std::string(128 * 1024, '0'));
    oversized.append(false);
    BOOST_CHECK_EQUAL(JsonValidator::validateParams(block, oversized).value_or(""), "/params/0");
}

BOOST_AUTO_TEST_CASE(draft7CompositionReferencesAndBounds)
{
    auto const source = parseSchemaJson(R"({
        "type": "array",
        "items": [
            {
                "allOf": [
                    {"$ref": "#/definitions/id"},
                    {"not": {"const": 3}},
                    {"oneOf": [{"minimum": 2}, {"maximum": 2}]}
                ]
            },
            {
                "type": "object",
                "properties": {"value": {"anyOf": [{"type": "boolean"}, {"type": "string"}]}},
                "required": ["value"],
                "additionalProperties": false
            }
        ],
        "minItems": 2,
        "additionalItems": false,
        "definitions": {"id": {"type": "integer", "minimum": 1, "maximum": 4}}
    })");
    valijson::Schema schema;
    valijson::SchemaParser(valijson::SchemaParser::kDraft7)
        .populateSchema(valijson::adapters::JsonCppAdapter(source), schema);
    valijson::Validator validator;
    auto check = [&](std::string_view request, bool expected) {
        auto const value = parseSchemaJson(request);
        BOOST_CHECK_EQUAL(
            validator.validate(schema, valijson::adapters::JsonCppAdapter(value), nullptr),
            expected);
    };
    check(R"([1,{"value":true}])", true);
    check(R"([4,{"value":"x"}])", true);
    check(R"(["1",{"value":true}])", false);
    check(R"([2,{"value":true}])", false);
    check(R"([3,{"value":true}])", false);
    check(R"([5,{"value":true}])", false);
    check(R"([0,{"value":true}])", false);
    check(R"([1,{"value":1}])", false);
    check(R"([1,{"value":true,"extra":0}])", false);
    check(R"([1,{}])", false);
    check(R"([1,{"value":true},true])", false);
    check(R"([1])", false);
}

BOOST_AUTO_TEST_CASE(concurrentValidation)
{
    JsonValidator::prepareParameterSchemas();
    auto const valid = parseSchemaJson(R"(["0x0",false])");
    auto const invalidTag = parseSchemaJson(R"(["unknown",false])");
    auto const invalidBoolean = parseSchemaJson(R"(["latest",42])");
    std::array<bool, 8> succeeded{};
    {
        std::array<std::jthread, 8> threads;
        for (size_t index = 0; index < threads.size(); ++index)
        {
            threads[index] = std::jthread([&, index] {
                auto const method = "eth_getBlockByNumber";
                for (int request = 0; request < 50; ++request)
                {
                    if (JsonValidator::validateParams(method, valid) ||
                        JsonValidator::validateParams(method, invalidTag) != "/params/0" ||
                        JsonValidator::validateParams(method, invalidBoolean) != "/params/1")
                    {
                        return;
                    }
                }
                succeeded[index] = true;
            });
        }
    }
    for (bool result : succeeded)
    {
        BOOST_CHECK(result);
    }
}

BOOST_AUTO_TEST_SUITE_END()
