/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file Abi.cpp
 */
#include "Abi.h"
#include "bcos-ops/OpsError.h"
#include "bcos-ops/RpcCall.h"
#include <bcos-cpp-sdk/utilities/abi/ContractABICodec.h>
#include <bcos-cpp-sdk/utilities/abi/ContractABITypeCodec.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <json/json.h>

namespace bcos::ops
{
namespace
{
bcos::cppsdk::abi::ContractABICodec makeCodec(bool _sm)
{
    bcos::crypto::Hash::Ptr hash;
    if (_sm)
    {
        hash = std::make_shared<bcos::crypto::SM3>();
    }
    else
    {
        hash = std::make_shared<bcos::crypto::Keccak256>();
    }
    return bcos::cppsdk::abi::ContractABICodec(
        hash, std::make_shared<bcos::cppsdk::abi::ContractABITypeCodecSolImpl>());
}

template <typename F>
auto guarded(std::string_view _what, F&& _f)
{
    try
    {
        return _f();
    }
    catch (OpsError const&)
    {
        throw;
    }
    catch (std::exception const& e)
    {
        throw OpsError(c_exitUsage, std::string(_what) + ": " + e.what());
    }
}
}  // namespace

bytes Abi::encodeConstructor(
    std::string const& _abi, std::string const& _bin, std::string const& _jsonParams) const
{
    // solc leaves the implicit constructor out of the ABI; the SDK's encodeConstructor then
    // fails on getMethod("constructor"). Such a contract deploys with its bytecode alone.
    bool hasConstructor = false;
    auto abi = parseJson(_abi, "abi");
    for (auto const& entry : abi)
    {
        if (entry.isObject() && entry.get("type", "").asString() == "constructor")
        {
            hasConstructor = true;
            break;
        }
    }
    if (!hasConstructor)
    {
        auto params = parseJson(_jsonParams.empty() ? "[]" : _jsonParams, "constructor params");
        if (!params.empty())
        {
            throw OpsError(c_exitUsage, "ABI has no constructor: cannot encode " +
                                            std::to_string(params.size()) +
                                            " constructor argument(s)");
        }
        return guarded("decode bytecode", [&]() { return fromHex(_bin); });
    }
    // ContractABICodec::encodeConstructor goes through encodeMethod, which always prepends the
    // 4-byte id of the "constructor" signature (ContractABICodec.cpp encodeMethod); the init
    // code then reads the selector as the first argument and a deploy with arguments reverts.
    // Constructor arguments are the bare ABI encoding appended to the bytecode.
    return guarded("encode constructor", [&]() {
        auto encoded = makeCodec(m_sm).encodeMethod(_abi, "constructor", _jsonParams);
        constexpr size_t c_selectorSize = 4;
        auto data = fromHex(_bin);
        data.insert(data.end(), encoded.begin() + static_cast<std::ptrdiff_t>(c_selectorSize),
            encoded.end());
        return data;
    });
}

bytes Abi::encodeMethod(
    std::string const& _abi, std::string const& _method, std::string const& _jsonParams) const
{
    return guarded("encode " + _method,
        [&]() { return makeCodec(m_sm).encodeMethod(_abi, _method, _jsonParams); });
}

bytes Abi::encodeMethodBySignature(
    std::string const& _signature, std::string const& _jsonParams) const
{
    return guarded("encode " + _signature,
        [&]() { return makeCodec(m_sm).encodeMethodBySignature(_signature, _jsonParams); });
}

std::string Abi::decodeOutput(
    std::string const& _abi, std::string const& _method, bytes const& _output) const
{
    return guarded("decode output of " + _method,
        [&]() { return makeCodec(m_sm).decodeMethodOutput(_abi, _method, _output); });
}

std::string Abi::decodeInputBySignature(std::string const& _signature, bytes const& _input) const
{
    return guarded("decode input of " + _signature,
        [&]() { return makeCodec(m_sm).decodeMethodInputByMethodSig(_signature, _input); });
}

std::string argsToJsonArray(std::vector<std::string> const& _args)
{
    Json::Value array(Json::arrayValue);
    Json::CharReaderBuilder builder;
    builder["failIfExtra"] = true;  // "0x1234" must stay a string, not parse as 0
    for (auto const& arg : _args)
    {
        Json::Value parsed;
        std::string errors;
        std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
        if (!arg.empty() &&
            (arg[0] == '[' || arg[0] == '{' || arg[0] == '"' || arg == "true" || arg == "false" ||
                std::isdigit(static_cast<unsigned char>(arg[0])) || arg[0] == '-') &&
            reader->parse(arg.data(), arg.data() + arg.size(), &parsed, &errors))
        {
            array.append(parsed);
        }
        else
        {
            array.append(arg);
        }
    }
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    return Json::writeString(writer, array);
}
}  // namespace bcos::ops
