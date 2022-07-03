/*
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief ABI data structure used in transaction construction
 * @file Abi.cpp
 * @author: catli
 * @date: 2021-09-11
 */

#include "Abi.h"
#include <json/forwards.h>
#include <json/json.h>
#include <sys/types.h>
#include <boost/algorithm/string/predicate.hpp>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

using namespace std;
using namespace bcos::executor;

ParameterAbi parseParameter(const Json::Value& input)
{
    auto paramType = input["type"].asString();
    auto components = vector<ParameterAbi>();
    if (boost::starts_with(paramType, "tuple"))
    {
        auto& paramComponents = input["components"];
        assert(!paramComponents.isNull());

        components.reserve(paramComponents.size());
        for (auto& component : paramComponents)
        {
            components.emplace_back(parseParameter(component));
        }
    }
    auto parameterAbi = ParameterAbi(paramType, components);
    return parameterAbi;
}

vector<string> flattenStaticParamter(const ParameterAbi& param)
{  // TODO: return vector<std::pair<string, vector<uint8>>>, pair is type and access path
    const auto TUPLE_STR = "tuple";
    auto flatTypes = vector<string>();
    if (boost::starts_with(param.type, TUPLE_STR))
    {
        for (auto i = (size_t)0; i < param.components.size(); i++)
        {
            auto types = flattenStaticParamter(param.components[i]);
            flatTypes.insert(flatTypes.end(), types.begin(), types.end());
        }
    }
    else if (boost::algorithm::contains(param.type, "[") &&
             !boost::algorithm::contains(param.type, "[]"))
    {
        auto type = param.type.substr(0, param.type.find("["));
        size_t len = std::stoi(param.type.substr(param.type.find("["), param.type.find("]")));
        flatTypes.insert(flatTypes.end(), len, type);
    }
    else
    {
        flatTypes.push_back(param.type);
    }
    return flatTypes;
}

unique_ptr<FunctionAbi> FunctionAbi::deserialize(
    string_view abiStr, const bytes& expected, bool isSMCrypto)
{
    assert(expected.size() == 4);

    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(abiStr.begin(), abiStr.end(), root))
    {
        BCOS_LOG(DEBUG) << LOG_BADGE("EXECUTOR") << LOG_DESC("unable to parse contract ABI")
                        << LOG_KV("abiStr", abiStr);
        return nullptr;
    }

    if (!root.isArray())
    {
        BCOS_LOG(DEBUG) << LOG_BADGE("EXECUTOR") << LOG_DESC("contract ABI is not an array")
                        << LOG_KV("abiStr", abiStr);
        return nullptr;
    }

    for (auto& function : root)
    {
        auto& type = function["type"];
        if (type.isNull() || type.asString() != "function")
        {
            continue;
        }

        if (!function["constant"].isNull())
        {  // liquid
            if (function["constant"].asBool())
            {
                continue;
            }
        }
        else if (!function["stateMutability"].isNull())
        {  // solidity
            if (function["stateMutability"].asString() == "view" ||
                function["stateMutability"].asString() == "pure")
            {
                continue;
            }
        }
        else
        {
            continue;
        }
        auto& functionName = function["name"];
        assert(!functionName.isNull());
        uint32_t selector = 0;
        if (!function["selector"].isNull() && function["selector"].isArray())
        {
            if (isSMCrypto)
            {
                selector = (uint32_t)function["selector"][1].asUInt();
            }
            else
            {
                selector = (uint32_t)function["selector"][0].asUInt();
            }
        }

        auto expectedSelector = *((uint32_t*)expected.data());
        expectedSelector = ((expectedSelector & 0xff) << 24) | ((expectedSelector & 0xff00) << 8) |
                           ((expectedSelector & 0xff0000) >> 8) |
                           ((expectedSelector & 0xff000000) >> 24);

        if (expectedSelector != selector)
        {
            BCOS_LOG(DEBUG) << LOG_BADGE("EXECUTOR") << LOG_DESC("selector mismatch")
                            << LOG_KV("name", functionName)
                            << LOG_KV("expected selector", expectedSelector)
                            << LOG_KV("selector", selector);
            continue;
        }

        auto& functionConflictFields = function["conflictFields"];
        auto conflictFields = vector<ConflictField>();
        conflictFields.reserve(functionConflictFields.size());
        if (!functionConflictFields.isNull())
        {
            for (auto& conflictField : functionConflictFields)
            {
                auto value = vector<uint8_t>();
                if (!conflictField["value"].isNull())
                {
                    value.reserve(conflictField["value"].size());
                    for (auto& pathItem : conflictField["value"])
                    {
                        value.emplace_back(static_cast<uint8_t>(pathItem.asUInt()));
                    }
                }
                std::optional<uint8_t> slot = std::nullopt;
                if (!conflictField["slot"].isNull())
                {
                    slot = std::optional<uint8_t>(conflictField["slot"].asInt());
                }
                conflictFields.emplace_back(ConflictField{
                    static_cast<uint8_t>(conflictField["kind"].asUInt()), value, slot});
            }
        }

        auto& functionInputs = function["inputs"];
        assert(!functionInputs.isNull());
        auto inputs = vector<ParameterAbi>();
        inputs.reserve(functionInputs.size());
        auto flatInputs = vector<string>();
        for (auto i = (Json::ArrayIndex)0; i < functionInputs.size(); ++i)
        {
            auto param = parseParameter(functionInputs[i]);
            auto flatTypes = flattenStaticParamter(param);
            flatInputs.insert(flatInputs.end(), flatTypes.begin(), flatTypes.end());
            inputs.emplace_back(std::move(param));
        }

        return unique_ptr<FunctionAbi>(
            new FunctionAbi{functionName.asString(), inputs, selector, conflictFields, flatInputs});
    }

    BCOS_LOG(DEBUG) << LOG_BADGE("EXECUTOR") << LOG_DESC("expected selector not found")
                    << LOG_KV("expected", toHexStringWithPrefix(expected));
    return nullptr;
}
