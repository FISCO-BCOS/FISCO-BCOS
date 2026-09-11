/**
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
 * @brief: PrecompiledContract implementation
 * @file: PrecompiledContract.cpp
 */

#include "PrecompiledContract.h"
#include <bcos-cpp-sdk/utilities/abi/ContractABITypeCodec.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <stdexcept>

namespace bcos::console::precompiled
{

bcos::cppsdk::abi::ContractABICodec& PrecompiledContract::getCodec(const char* abiJson)
{
    auto it = m_codecs.find(abiJson);
    if (it != m_codecs.end())
    {
        return *it->second;
    }

    auto codecImpl = std::make_shared<bcos::cppsdk::abi::ContractABITypeCodecSolImpl>();
    auto codec = std::make_unique<bcos::cppsdk::abi::ContractABICodec>(
        m_hashImpl, std::move(codecImpl));
    auto& ref = *codec;
    m_codecs[abiJson] = std::move(codec);
    return ref;
}

bcos::bytes PrecompiledContract::encode(
    PrecompiledType type, std::string_view method, std::string_view jsonParams)
{
    const char* abiJson = abiOf(type);
    if (!abiJson || abiJson[0] == '\0')
    {
        throw std::runtime_error(
            std::string("PrecompiledContract: no ABI defined for type ")
            + std::to_string(static_cast<int>(type)));
    }

    auto& codec = getCodec(abiJson);
    return codec.encodeMethod(abiJson, std::string(method), std::string(jsonParams));
}

std::string PrecompiledContract::encodeHex(
    PrecompiledType type, std::string_view method, std::string_view jsonParams)
{
    auto bytes = encode(type, method, jsonParams);
    return bcos::toHex(bytes, "0x");
}

std::string PrecompiledContract::decodeOutput(
    PrecompiledType type, std::string_view method, bcos::bytesConstRef output)
{
    const char* abiJson = abiOf(type);
    if (!abiJson || abiJson[0] == '\0')
    {
        throw std::runtime_error(
            std::string("PrecompiledContract: no ABI defined for type ")
            + std::to_string(static_cast<int>(type)));
    }

    auto& codec = getCodec(abiJson);
    return codec.decodeMethodOutput(abiJson, std::string(method), output.toBytes());
}

// ============================================================================
// Phase 2: ABI Introspection
// ============================================================================

bcos::cppsdk::abi::ContractABIDefinition::Ptr PrecompiledContract::getDefinition(
    const char* abiJson)
{
    auto it = m_definitions.find(abiJson);
    if (it != m_definitions.end())
    {
        return it->second;
    }
    auto def = m_abiFactory.buildABI(std::string(abiJson));
    m_definitions[abiJson] = def;
    return def;
}

MethodInfo PrecompiledContract::getMethodInfo(
    PrecompiledType type, std::string_view methodName)
{
    const char* abiJson = abiOf(type);
    if (!abiJson || abiJson[0] == '\0')
    {
        throw std::runtime_error("No ABI for precompiled type");
    }

    auto def = getDefinition(abiJson);
    auto methods = def->getMethod(std::string(methodName));
    if (methods.empty())
    {
        throw std::runtime_error(
            std::string("Method not found: ") + std::string(methodName));
    }

    auto& m = methods[0];  // Take first overload
    MethodInfo info;
    info.name = m->name();
    info.signature = m->getMethodSignatureAsString();
    info.methodID = m->getMethodIDAsString(m_hashImpl);
    info.inputCount = m->inputs().size();

    for (auto& input : m->inputs())
    {
        info.inputTypes.push_back(input->getTypeAsString());
    }
    for (auto& output : m->outputs())
    {
        info.outputTypes.push_back(output->getTypeAsString());
    }

    return info;
}

std::vector<std::string> PrecompiledContract::listMethods(PrecompiledType type)
{
    const char* abiJson = abiOf(type);
    if (!abiJson || abiJson[0] == '\0')
    {
        return {};
    }

    auto def = getDefinition(abiJson);
    return def->methodNames();
}

std::string PrecompiledContract::getUsage(
    PrecompiledType type, std::string_view methodName)
{
    try
    {
        auto info = getMethodInfo(type, methodName);
        std::string result = info.signature;

        // Build input hint
        if (!info.inputTypes.empty())
        {
            result += "\n  Params: ";
            for (size_t i = 0; i < info.inputTypes.size(); ++i)
            {
                if (i > 0) result += ", ";
                result += "<" + info.inputTypes[i] + ">";
            }
        }
        else
        {
            result += "\n  Params: (none)";
        }

        // Build output hint
        if (!info.outputTypes.empty())
        {
            result += "\n  Returns: ";
            for (size_t i = 0; i < info.outputTypes.size(); ++i)
            {
                if (i > 0) result += ", ";
                result += info.outputTypes[i];
            }
        }

        result += "\n  Selector: " + info.methodID;
        return result;
    }
    catch (std::exception const& e)
    {
        return std::string("(unknown method: ") + e.what() + ")";
    }
}

std::string PrecompiledContract::validateParamCount(
    PrecompiledType type, std::string_view methodName, size_t paramCount)
{
    try
    {
        auto info = getMethodInfo(type, methodName);
        if (info.inputCount != paramCount)
        {
            return "Expected " + std::to_string(info.inputCount) + " params, got "
                + std::to_string(paramCount) + ".  Usage: " + info.signature;
        }
        return {};  // OK
    }
    catch (std::exception const& e)
    {
        return e.what();
    }
}

}  // namespace bcos::console::precompiled
