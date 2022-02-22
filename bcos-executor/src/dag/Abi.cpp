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

string canonizeParamter(const ParameterAbi& param)
{
    const auto TUPLE_STR = "tuple";
    auto type = string();
    if (boost::starts_with(param.type, TUPLE_STR))
    {
        type += "(";
        for (auto i = (size_t)0; i < param.components.size(); i++)
        {
            type += canonizeParamter(param.components[i]);
            if (i != param.components.size() - 1)
            {
                type += ",";
            }
        }
        type += ")";
        type.append(param.type.substr(strlen(TUPLE_STR)));
    }
    else
    {
        type = param.type;
    }
    return type;
}

unique_ptr<FunctionAbi> FunctionAbi::deserialize(
    string_view abiStr, const bytes& expected, crypto::Hash::Ptr hashImpl)
{
    assert(expected.size() == 4);

    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(abiStr.begin(), abiStr.end(), root))
    {
        BCOS_LOG(ERROR) << LOG_BADGE("EXECUTOR") << LOG_DESC("unable to parse contract ABI")
                        << LOG_KV("abiStr", abiStr);
        return unique_ptr<FunctionAbi>();
    }

    if (!root.isArray())
    {
        BCOS_LOG(ERROR) << LOG_BADGE("EXECUTOR") << LOG_DESC("contract ABI is not an array")
                        << LOG_KV("abiStr", abiStr);
        return unique_ptr<FunctionAbi>();
    }

    for (auto& function : root)
    {
        auto& type = function["type"];
        if (type.isNull() || type.asString() != "function")
        {
            continue;
        }

        auto& constant = function["constant"];
        if (constant.isNull() || constant.asBool())
        {
            continue;
        }

        auto& functionName = function["name"];
        assert(!functionName.isNull());
        auto signature = functionName.asString() + "(";

        auto& functionInputs = function["inputs"];
        assert(!functionInputs.isNull());
        auto inputs = vector<ParameterAbi>();
        inputs.reserve(functionInputs.size());
        for (auto i = (Json::ArrayIndex)0; i < functionInputs.size(); ++i)
        {
            auto param = parseParameter(functionInputs[i]);
            signature += canonizeParamter(param);
            if (i < functionInputs.size() - 1)
            {
                signature += ",";
            }
            inputs.emplace_back(std::move(param));
        }
        signature += ")";

        auto selector = hashImpl->hash(signature).asBytes();
        selector.resize(4);
        BCOS_LOG(TRACE) << LOG_BADGE("EXECUTOR") << LOG_DESC("found selector")
                        << LOG_KV("selector", toHexStringWithPrefix(selector))
                        << LOG_KV("signature", signature);

        if (!std::equal(selector.begin(), selector.end(), expected.begin()))
        {
            continue;
        }

        auto& functionConflictFields = function["conflictFields"];
        auto conflictFields = vector<ConflictField>();
        conflictFields.reserve(functionConflictFields.size());
        if (!functionConflictFields.isNull())
        {
            for (auto& conflictField : functionConflictFields)
            {
                auto accessPath = vector<uint8_t>();
                accessPath.reserve(conflictField["path"].size());
                for (auto& pathItem : conflictField["path"])
                {
                    accessPath.emplace_back(static_cast<uint8_t>(pathItem.asUInt()));
                }

                conflictFields.emplace_back(
                    ConflictField{static_cast<uint8_t>(conflictField["kind"].asUInt()), accessPath,
                        conflictField["read_only"].asBool(),
                        static_cast<uint8_t>(conflictField["slot"].asUInt())});
            }
        }

        return unique_ptr<FunctionAbi>(
            new FunctionAbi{functionName.asString(), inputs, conflictFields});
    }

    BCOS_LOG(ERROR) << LOG_BADGE("EXECUTOR") << LOG_DESC("expected selector not found")
                    << LOG_KV("selector", toHexStringWithPrefix(expected));
    return unique_ptr<FunctionAbi>();
}
