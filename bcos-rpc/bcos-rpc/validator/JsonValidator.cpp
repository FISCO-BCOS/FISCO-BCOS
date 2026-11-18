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
#include "ParameterSchemas.generated.h"
#include <boost/regex.hpp>
#include <map>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <valijson/valijson_jsoncpp_bundled.hpp>
#include <vector>

namespace bcos::rpc
{
namespace
{
struct ParameterSchema
{
    valijson::Schema rule;
    std::vector<Json::UInt> maxStringLengths;
};

struct CompiledParameterSchemas
{
    std::map<std::string, ParameterSchema, std::less<>> methods;
    std::unordered_map<std::string, boost::regex> patterns;

    CompiledParameterSchemas()
    {
        Json::CharReaderBuilder reader;
        std::istringstream input(std::string{schema::parameterSchemas});
        Json::Value source;
        if (!Json::parseFromStream(reader, input, &source, nullptr) || !source.isObject())
        {
            throw std::logic_error("Invalid embedded Web3 parameter schemas");
        }

        for (auto entry = source.begin(); entry != source.end(); ++entry)
        {
            auto& compiled = methods.try_emplace(entry.name()).first->second;
            auto const& rule = *entry;
            auto const& items = rule["items"];
            auto const& maxItems = rule["maxItems"];
            if ((!items.isArray() && !items.isNull()) || !maxItems.isUInt() ||
                maxItems.asUInt() != items.size())
            {
                throw std::logic_error("Web3 parameter schema must be a bounded tuple");
            }
            for (auto const& item : items)
            {
                compiled.maxStringLengths.push_back(item["maxLength"].asUInt());
            }
            compilePatterns(rule);
            valijson::SchemaParser(valijson::SchemaParser::kDraft7)
                .populateSchema(valijson::adapters::JsonCppAdapter(rule), compiled.rule);
        }
    }

    void compilePatterns(const Json::Value& rule)
    {
        if (rule.isObject())
        {
            if (rule.isMember("pattern"))
            {
                auto const pattern = rule["pattern"].asString();
                patterns.try_emplace(pattern, pattern, boost::regex::ECMAScript);
            }
        }
        if (rule.isObject() || rule.isArray())
        {
            for (auto const& child : rule)
            {
                compilePatterns(child);
            }
        }
    }
};

auto const& compiledParameterSchemas()
{
    static const CompiledParameterSchemas schemas;
    return schemas;
}

// Valijson maintains a mutable pattern cache per Validator. Each thread caches only references
// to the patterns compiled during startup. Request-local visitors own the validation state.
struct ParameterRegex
{
    boost::regex const& pattern;

    explicit ParameterRegex(std::string const& source)
      : pattern(compiledParameterSchemas().patterns.at(source))
    {}

    static bool search(const std::string& value, const ParameterRegex& rule)
    {
        // ECMAScript does not enable multiline anchors by default.
        return boost::regex_search(value, rule.pattern, boost::match_single_line);
    }
};
}  // namespace

std::tuple<bool, std::string> JsonValidator::validate(const Json::Value& root)
{
    return checkRequestFields(root);
}

void JsonValidator::prepareParameterSchemas()
{
    (void)compiledParameterSchemas();
}

std::optional<std::string> JsonValidator::validateParams(
    std::string_view method, const Json::Value& params)
{
    auto const& schemas = compiledParameterSchemas().methods;
    auto const entry = schemas.find(method);
    if (entry == schemas.end())
    {
        return std::nullopt;
    }

    auto const& [rule, maxStringLengths] = entry->second;
    static const Json::Value emptyParams(Json::arrayValue);
    auto const& values = params.isNull() ? emptyParams : params;
    if (!values.isArray() || values.size() > maxStringLengths.size())
    {
        return "/params";
    }
    for (Json::ArrayIndex index = 0; index < values.size(); ++index)
    {
        char const* begin = nullptr;
        char const* end = nullptr;
        auto const maxLength = maxStringLengths[index];
        // These schemas only accept ASCII strings. Check the byte length before the adapter
        // copies the string or gives it to the regex engine.
        if (maxLength && values[index].getString(&begin, &end) &&
            static_cast<size_t>(end - begin) > maxLength)
        {
            return "/params/" + std::to_string(index);
        }
    }

    thread_local valijson::ValidatorT<ParameterRegex> validator;
    valijson::adapters::JsonCppAdapter adapter(values);
    if (validator.validate(rule, adapter, nullptr))
    {
        return std::nullopt;
    }

    valijson::ValidationResults results;
    validator.validate(rule, adapter, &results);
    for (auto const& error : results)
    {
        for (Json::ArrayIndex index = 0; index < values.size(); ++index)
        {
            if (error.jsonPointer == "/" + std::to_string(index))
            {
                return "/params" + error.jsonPointer;
            }
        }
    }
    return "/params";
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
