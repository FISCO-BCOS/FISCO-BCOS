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
 * @file JsonValidator.cpp
 * @author: kyonGuo
 * @date 2024/3/28
 */

#include "JsonValidator.h"
#include "Web3RpcSchema.h"
#include <bcos-rpc/Common.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <bcos-rpc/web3jsonrpc/Web3Subscribe.h>
#include <boost/regex.hpp>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

namespace bcos::rpc
{
namespace
{
using Json::Value;

std::string_view stringView(Value const& value)
{
    char const* begin = nullptr;
    char const* end = nullptr;
    if (!value.getString(&begin, &end))
    {
        throw std::logic_error("Expected a string in the parameter schema");
    }
    return {begin, end};
}

enum class RuleType
{
    Any,
    Null,
    Boolean,
    Object,
    Array,
    String,
    Number
};

RuleType parseType(std::string_view type)
{
    constexpr std::array types{std::pair{"null", RuleType::Null},
        std::pair{"boolean", RuleType::Boolean}, std::pair{"object", RuleType::Object},
        std::pair{"array", RuleType::Array}, std::pair{"string", RuleType::String},
        std::pair{"number", RuleType::Number}};
    for (auto const& [name, kind] : types)
    {
        if (name == type)
            return kind;
    }
    throw std::logic_error("Unsupported JSON Schema type: " + std::string(type));
}

bool hasType(Value const& value, RuleType type)
{
    switch (type)
    {
    case RuleType::Any:
        return true;
    case RuleType::Null:
        return value.isNull();
    case RuleType::Boolean:
        return value.isBool();
    case RuleType::Object:
        return value.isObject();
    case RuleType::Array:
        return value.isArray();
    case RuleType::String:
        return value.isString();
    case RuleType::Number:
        return value.isNumeric();
    }
    return false;
}

struct Rule
{
    RuleType type = RuleType::Any;
    bool allowed = true;
    bool allowAdditionalProperties = true;
    boost::regex const* pattern = nullptr;
    std::vector<std::string_view> choices;
    std::vector<std::string_view> required;
    std::map<std::string_view, std::unique_ptr<Rule>, std::less<>> properties;
    std::vector<Rule> anyOf;
    std::vector<Rule> oneOf;
    std::unique_ptr<Rule> items;
    std::unique_ptr<Rule> negated;
};

enum class ArgumentMode
{
    Standard,
    FeeHistoryCount,
    SubscriptionId,
    RawHeaderBlock,
    NewPayloadV2,
    ForkchoiceV2,
    Subscription
};

struct MethodSchema
{
    struct Parameter
    {
        Rule rule;
        bool allowNull = false;
        ArgumentMode mode = ArgumentMode::Standard;
        Rule const* preShanghaiAttributes = nullptr;
    };

    std::vector<Parameter> params;
    Json::ArrayIndex required = 0;
};

class ParameterSchemas
{
public:
    ParameterSchemas();

    MethodSchema const* find(std::string_view method) const
    {
        auto item = m_methods.find(method);
        return item == m_methods.end() ? nullptr : &item->second;
    }

private:
    Rule compileRule(Value const& schema);

    // Rules borrow keys/strings from this immutable document and patterns from this cache.
    Value m_document;
    std::map<std::string_view, boost::regex, std::less<>> m_patterns;
    std::map<std::string_view, MethodSchema, std::less<>> m_methods;
};

Rule ParameterSchemas::compileRule(Value const& schema)
{
    Rule rule;
    if (schema.isBool())
    {
        rule.allowed = schema.asBool();
        return rule;
    }
    if (!schema.isObject())
        throw std::logic_error("Invalid JSON Schema object");

    for (auto item = schema.begin(); item != schema.end(); ++item)
    {
        auto key = item.name();
        if (key == "type")
        {
            rule.type = parseType(stringView(*item));
        }
        else if (key == "pattern")
        {
            auto text = stringView(*item);
            rule.pattern =
                &m_patterns.try_emplace(text, text.begin(), text.end(), boost::regex::ECMAScript)
                     .first->second;
        }
        else if (key == "enum" || key == "required")
        {
            if (!item->isArray() || (key == "enum" && item->empty()))
                throw std::logic_error("Invalid value for JSON Schema keyword: " + key);
            auto& strings = key == "enum" ? rule.choices : rule.required;
            for (auto const& child : *item)
                strings.push_back(stringView(child));
        }
        else if (key == "properties")
        {
            if (!item->isObject())
                throw std::logic_error("Expected JSON Schema properties object");
            for (auto property = item->begin(); property != item->end(); ++property)
            {
                char const* end = nullptr;
                auto const* begin = property.memberName(&end);
                rule.properties.emplace(
                    std::string_view(begin, end), std::make_unique<Rule>(compileRule(*property)));
            }
        }
        else if (key == "oneOf" || key == "anyOf")
        {
            if (!item->isArray() || item->empty())
                throw std::logic_error("Invalid JSON Schema alternatives: " + key);
            auto& alternatives = key == "oneOf" ? rule.oneOf : rule.anyOf;
            alternatives.reserve(item->size());
            for (auto const& child : *item)
                alternatives.emplace_back(compileRule(child));
        }
        else if (key == "items")
        {
            rule.items = std::make_unique<Rule>(compileRule(*item));
        }
        else if (key == "not")
        {
            rule.negated = std::make_unique<Rule>(compileRule(*item));
        }
        else if (key == "additionalProperties")
        {
            if (!item->isBool())
                throw std::logic_error("Expected a boolean JSON Schema additionalProperties");
            rule.allowAdditionalProperties = item->asBool();
        }
        else
        {
            throw std::logic_error("Unsupported JSON Schema keyword: " + key);
        }
    }
    return rule;
}

ArgumentMode argumentMode(std::string_view method, Json::ArrayIndex index)
{
    if (index == 0)
    {
        if (method == "eth_feeHistory")
            return ArgumentMode::FeeHistoryCount;
        if (method == "eth_unsubscribe")
            return ArgumentMode::SubscriptionId;
        if (method == "debug_getRawHeader")
            return ArgumentMode::RawHeaderBlock;
        if (method == "engine_newPayloadV2")
            return ArgumentMode::NewPayloadV2;
    }
    if (index == 1)
    {
        if (method == "engine_forkchoiceUpdatedV2")
            return ArgumentMode::ForkchoiceV2;
        if (method == "eth_subscribe")
            return ArgumentMode::Subscription;
    }
    return ArgumentMode::Standard;
}

bool nullablePayloadAttributes(std::string_view method)
{
    // Execution API forkchoice updates carry null when no payload construction is requested.
    return method == "engine_forkchoiceUpdatedV1" || method == "engine_forkchoiceUpdatedV2" ||
           method == "engine_forkchoiceUpdatedV3" || method == "engine_forkchoiceUpdatedV4";
}

ParameterSchemas::ParameterSchemas()
{
    Json::CharReaderBuilder builder;
    builder["rejectDupKeys"] = true;
    auto reader = std::unique_ptr<Json::CharReader>(builder.newCharReader());
    std::string errors;
    auto source = schema::web3RpcSchema;
    if (!reader->parse(source.data(), source.data() + source.size(), &m_document, &errors) ||
        !m_document["methods"].isArray())
    {
        throw std::logic_error("Invalid embedded Web3 parameter schemas: " + errors);
    }
    auto& methods = m_document["methods"];
    // CallRequest accepts data as an alias for input. Use the same upstream rule for both keys.
    for (auto& method : methods)
    {
        auto name = stringView(method["name"]);
        if (name == "eth_call" || name == "eth_estimateGas")
        {
            auto& properties = method["params"][0U]["schema"]["properties"];
            if (!properties.isObject() || !properties.isMember("input"))
                throw std::logic_error("Missing upstream transaction input property");
            properties["data"] = properties["input"];
        }
    }
    for (auto const& method : std::as_const(methods))
    {
        if (!method["params"].isArray())
            throw std::logic_error("Expected an OpenRPC positional parameter array");
        auto name = stringView(method["name"]);
        auto [entry, inserted] = m_methods.try_emplace(name);
        if (!inserted)
            throw std::logic_error("Duplicate Web3 parameter schema");
        auto& target = entry->second;
        auto const& parameters = method["params"];
        auto count = parameters.size();
        target.params.reserve(count);
        for (Json::ArrayIndex i = 0; i < count; ++i)
        {
            auto const& parameter = parameters[i];
            if (!parameter["required"].isBool())
                throw std::logic_error("Expected the OpenRPC required flag");
            bool const forkchoiceAttributes = i == 1 && nullablePayloadAttributes(name);
            // Existing feeHistory accepts an unsigned JSON count and omitted/null percentiles.
            bool const feeHistoryPercentiles = name == "eth_feeHistory" && i == 2;
            target.params.push_back({compileRule(parameter["schema"]),
                forkchoiceAttributes || feeHistoryPercentiles, argumentMode(name, i)});
            if (parameter["required"].asBool() && !feeHistoryPercentiles)
                target.required = i + 1;
        }
    }
    // The OpenRPC V2 payload branches overlap because both permit extra keys.
    auto& payload = m_methods.at("engine_newPayloadV2").params.at(0).rule;
    if (payload.oneOf.size() != 2 || payload.oneOf[0].properties.contains("withdrawals") ||
        !payload.oneOf[1].properties.contains("withdrawals"))
        throw std::logic_error("Unexpected upstream engine_newPayloadV2 alternatives");
    // Shanghai permits V1 attributes without withdrawals on the V2 method before activation.
    m_methods.at("engine_forkchoiceUpdatedV2").params.at(1).preShanghaiAttributes =
        &m_methods.at("engine_forkchoiceUpdatedV1").params.at(1).rule;
    // The pinned eth_subscribe description discriminates these three union branches by kind.
    auto const& subscription = m_methods.at("eth_subscribe").params.at(1).rule.oneOf;
    if (subscription.size() != 3 || subscription[0].oneOf.empty() ||
        subscription[1].type != RuleType::Boolean ||
        !subscription[2].properties.contains("transactionHashes"))
        throw std::logic_error("Unexpected upstream eth_subscribe alternatives");
}

ParameterSchemas const& parameterSchemas()
{
    static const ParameterSchemas schemas;
    return schemas;
}

struct Path
{
    std::array<char, 128> bytes{};
    size_t length = 0;

    explicit Path(std::string_view base)
    {
        std::ranges::copy(base, bytes.begin());
        length = base.size();
    }

    Path child(std::string_view token) const
    {
        auto result = *this;
        // A parent pointer still locates the failure when a key does not fit.
        if (token.size() >= bytes.size() - length)
            return result;
        result.bytes[result.length++] = '/';
        for (char ch : token)
        {
            if (static_cast<unsigned char>(ch) < 0x20 || ch == 0x7f)
                return *this;
            if (ch == '~' || ch == '/')
            {
                if (result.length + 2 > bytes.size())
                    return *this;
                result.bytes[result.length++] = '~';
                result.bytes[result.length++] = ch == '~' ? '0' : '1';
            }
            else
            {
                if (result.length == bytes.size())
                    return *this;
                result.bytes[result.length++] = ch;
            }
        }
        return result;
    }

    std::string str() const { return {bytes.data(), length}; }
};

using Failure = std::optional<Path>;

Failure validateRule(Rule const& rule, Value const& value, Path const& path);

Failure validateObject(Rule const& rule, Value const& value, Path const& path)
{
    for (auto name : rule.required)
    {
        if (!value.find(name.data(), name.data() + name.size()))
            return path.child(name);
    }
    if (rule.properties.empty() && rule.allowAdditionalProperties)
        return std::nullopt;

    for (auto item = value.begin(); item != value.end(); ++item)
    {
        char const* end = nullptr;
        auto const* begin = item.memberName(&end);
        auto name = std::string_view(begin, end);
        auto property = rule.properties.find(name);
        if (property != rule.properties.end())
        {
            if (auto failure = validateRule(*property->second, *item, path.child(name)))
                return failure;
        }
        else if (!rule.allowAdditionalProperties)
        {
            return path.child(name);
        }
    }
    return std::nullopt;
}

Failure validateAlternatives(
    std::vector<Rule> const& rules, Value const& value, Path const& path, bool exactlyOne)
{
    if (rules.empty())
        return std::nullopt;

    bool matched = false;
    auto deepest = path;
    for (auto const& child : rules)
    {
        auto failure = validateRule(child, value, path);
        if (!failure)
        {
            if (!exactlyOne)
                return std::nullopt;
            if (matched)
                return path;
            matched = true;
        }
        else if (failure->length > deepest.length)
        {
            deepest = *failure;
        }
    }
    return matched ? Failure{} : deepest;
}

Failure validateRule(Rule const& rule, Value const& value, Path const& path)
{
    if (!rule.allowed || !hasType(value, rule.type))
        return path;
    if (!rule.choices.empty() &&
        (!value.isString() ||
            std::ranges::find(rule.choices, stringView(value)) == rule.choices.end()))
        return path;
    if (!rule.anyOf.empty())
    {
        if (auto failure = validateAlternatives(rule.anyOf, value, path, false))
            return failure;
    }
    if (!rule.oneOf.empty())
    {
        if (auto failure = validateAlternatives(rule.oneOf, value, path, true))
            return failure;
    }
    if (rule.negated && !validateRule(*rule.negated, value, path))
        return path;
    if (value.isObject())
        return validateObject(rule, value, path);
    if (rule.items && value.isArray())
    {
        auto count = value.size();
        for (Json::ArrayIndex i = 0; i < count; ++i)
        {
            if (auto failure = validateRule(*rule.items, value[i], path.child(std::to_string(i))))
                return failure;
        }
    }
    if (rule.pattern && value.isString())
    {
        auto text = stringView(value);
        if (!boost::regex_search(
                text.begin(), text.end(), *rule.pattern, boost::regex_constants::match_single_line))
            return path;
    }
    return std::nullopt;
}

bool fixedWidthHex(Value const& value, size_t bytes)
{
    if (!value.isString())
        return false;
    auto text = stringView(value);
    return text.size() == 2 + 2 * bytes && text.starts_with("0x") &&
           std::ranges::all_of(text.substr(2), [](char digit) {
               return (digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f');
           });
}

Failure validateParameter(MethodSchema::Parameter const& parameter, Value const& params,
    Json::ArrayIndex i, Path const& path)
{
    auto const& value = params[i];
    if (parameter.allowNull && value.isNull())
        return std::nullopt;

    auto const& rule = parameter.rule;
    switch (parameter.mode)
    {
    case ArgumentMode::FeeHistoryCount:
        if (value.isUInt64())
            return std::nullopt;
        break;
    case ArgumentMode::SubscriptionId:
        // The local ID producer emits 16 bytes verbatim, including any leading zeros.
        if (fixedWidthHex(value, Web3Subscribe::SUBSCRIPTION_ID_LENGTH))
            return std::nullopt;
        break;
    case ArgumentMode::RawHeaderBlock:
        // Bare block hashes belong to the existing challenger data plane.
        if (fixedWidthHex(value, 32))
            return std::nullopt;
        break;
    case ArgumentMode::NewPayloadV2:
        if (value.isObject())
            return validateRule(rule.oneOf[value.isMember("withdrawals") ? 1 : 0], value, path);
        break;
    case ArgumentMode::ForkchoiceV2:
        if (value.isObject() && !value.isMember("withdrawals"))
            return validateRule(*parameter.preShanghaiAttributes, value, path);
        break;
    case ArgumentMode::Subscription:
        // The preceding argument already passed the official subscription kind enum.
        if (auto kind = stringView(params[0U]); kind == "logs")
            return validateRule(rule.oneOf[0], value, path);
        else if (kind == "newPendingTransactions")
            return validateRule(rule.oneOf[1], value, path);
        else if (kind == "transactionReceipts")
            return validateRule(rule.oneOf[2], value, path);
        else
            return path;  // newHeads has no second argument.
    case ArgumentMode::Standard:
        break;
    }
    return validateRule(rule, value, path);
}

[[noreturn]] void invalidParams(Path const& path)
{
    BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Invalid params at " + path.str()));
}
}  // namespace

std::tuple<bool, std::string> JsonValidator::validate(Value const& root)
{
    return checkRequestFields(root);
}

void JsonValidator::prepareParameterSchemas()
{
    static_cast<void>(parameterSchemas());
}

void JsonValidator::requireParams(std::string_view method, Value const& params)
{
    auto const& schemas = parameterSchemas();
    auto const* signature = schemas.find(method);
    if (!signature)
        return;
    Path root("/params");
    auto count = params.size();
    if ((!params.isNull() && !params.isArray()) || count < signature->required ||
        count > signature->params.size())
        invalidParams(root);
    for (Json::ArrayIndex i = 0; i < count; ++i)
    {
        auto path = root.child(std::to_string(i));
        Failure failure;
        try
        {
            failure = validateParameter(signature->params[i], params, i, path);
        }
        catch (std::exception const&)
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(InternalError, "Parameter validation failed"));
        }
        if (failure)
            invalidParams(*failure);
    }
}

std::tuple<bool, std::string> JsonValidator::checkRequestFields(const Json::Value& root)
{
    // flag for 4 fields
    auto flag = 0b111;
    for (auto item = root.begin(); item != root.end(); ++item)
    {
        if (item.name() == "jsonrpc")
        {
            if (!item->isString())
            {
                return {false, "Invalid field: " + item.name()};
            }
            flag &= 0b110;
        }
        else if (item.name() == "method")
        {
            if (!item->isString())
            {
                return {false, "Invalid field: " + item.name()};
            }
            flag &= 0b101;
        }
        else if (item.name() == "params")
        {
            if (!item->isArray())
            {
                return {false, "Invalid field: " + item.name()};
            }
        }
        else if (item.name() == "id")
        {
            // JSON-RPC 2.0 allows id to be any JSON number or string
            if (!item->isNumeric() && !item->isString())
            {
                return {false, "Invalid field: " + item.name()};
            }
            flag &= 0b011;
        }
        else
        {
            return {false, "Invalid field: " + item.name()};
        }
    }
    if (flag != 0)
    {
        return {false, "Request not valid, required fields: jsonrpc, method, params, id"};
    }
    return {true, ""};
}

}  // namespace bcos::rpc
