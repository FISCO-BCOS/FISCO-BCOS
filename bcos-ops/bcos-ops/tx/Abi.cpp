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
#include <bcos-cpp-sdk/utilities/abi/ContractABICodec.h>
#include <bcos-cpp-sdk/utilities/abi/ContractABITypeCodec.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
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
    return guarded("encode constructor",
        [&]() { return makeCodec(m_sm).encodeConstructor(_abi, _bin, _jsonParams); });
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
