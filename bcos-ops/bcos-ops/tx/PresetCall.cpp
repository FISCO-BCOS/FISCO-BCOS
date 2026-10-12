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
 * @file PresetCall.cpp
 */
#include "PresetCall.h"
#include "Abi.h"
#include "bcos-ops/OpsError.h"
#include "bcos-ops/RpcCall.h"
#include <bcos-utilities/DataConvertUtility.h>
#include <map>

namespace bcos::ops
{
namespace
{
// bcos-executor/src/precompiled/common/Common.h; the six consensus names follow spec §5.1.3
std::map<int32_t, std::string> const c_codeNames = {
    {-50000, "NO_AUTHORIZED"},
    {-50100, "TABLE_NOT_EXIST"},
    {-50101, "UNKNOWN_FUNCTION_CALL"},
    {-50102, "ADDRESS_INVALID"},
    {-50103, "TABLE_SET_ROW_ERROR"},
    {-50104, "TABLE_CREATE_ERROR"},
    {-50105, "TABLE_OPEN_ERROR"},
    {-50106, "SENDER_ERROR"},
    {-51000, "TABLE_AGENT_ROW_NOT_EXIST"},
    {-51001, "TABLE_AUTH_ROW_NOT_EXIST"},
    {-51002, "TABLE_AUTH_TYPE_NOT_EXIST"},
    {-51003, "TABLE_ERROR_AUTH_TYPE"},
    {-51004, "TABLE_AUTH_TYPE_DECODE_ERROR"},
    {-51100, "INVALID_NODE_ID"},
    {-51101, "LAST_SEALER"},
    {-51102, "INVALID_WEIGHT"},
    {-51103, "NODE_NOT_EXIST"},
    {-51104, "SEALER_MUST_BE_OBSERVER_FIRST"},
    {-51400, "INVALID_USER_NAME"},
    {-51401, "INVALID_AMOUNT"},
    {-51402, "INVALID_USER_NOT_EXIST"},
    {-51403, "INVALID_USER_ALREADY_EXIST"},
    {-51404, "INVALID_INSUFFICIENT_BALANCE"},
    {-51405, "INVALID_BALANCE_OVERFLOW"},
    {-51406, "INVALID_OPENTABLE_FAILED"},
    {-51900, "ACCOUNT_ALREADY_EXIST"},
    {-51901, "INVALID_CONTRACT_AVAILABLE"},
    {-51902, "INVALID_CONTRACT_REPEAT_AUTHORIZATION"},
    {-51903, "INVALID_CONTRACT_ADDRESS"},
    {-51904, "INVALID_TABLE_NOT_EXIST"},
    {-51905, "INVALID_NO_AUTHORIZED"},
    {-51906, "INVALID_NON_EXIST_AUTHORIZATION"},
    {-51907, "INVALID_REVOKE_LAST_AUTHORIZATION"},
};

/// the SDK renders integer outputs as decimal strings ("-51104"); accept numbers too
std::optional<int32_t> codeOf(Json::Value const& _value)
{
    if (_value.isIntegral())
    {
        return _value.asInt();
    }
    if (!_value.isString())
    {
        return std::nullopt;
    }
    try
    {
        size_t used = 0;
        auto parsed = std::stoll(_value.asString(), &used);
        if (used != _value.asString().size())
        {
            return std::nullopt;
        }
        return static_cast<int32_t>(parsed);
    }
    catch (std::exception const&)
    {
        return std::nullopt;
    }
}

bool isBytesType(std::string_view _type)
{
    return _type.starts_with("bytes") && !_type.ends_with("]");
}

Json::Value typedArg(ParamSpec const& _spec, std::string const& _arg)
{
    if (isBytesType(_spec.type))
    {
        return toSdkBytes(_arg);
    }
    // argsToJsonArray decides number / bool / array vs string per value
    auto array = parseJson(argsToJsonArray({_arg}), "preset argument");
    auto value = array[0];
    if ((_spec.type == "string" || _spec.type == "address") && !value.isString())
    {
        return _arg;  // "123" as a string parameter stays text
    }
    return value;
}
}  // namespace

std::string precompiledCodeName(int32_t _code)
{
    auto it = c_codeNames.find(_code);
    return it == c_codeNames.end() ? "" : it->second;
}

std::string toSdkBytes(std::string const& _hexValue)
{
    if (_hexValue.starts_with("hex://") || _hexValue.starts_with("base64://"))
    {
        return _hexValue;
    }
    auto hex = _hexValue.starts_with("0x") || _hexValue.starts_with("0X") ? _hexValue.substr(2) :
                                                                            _hexValue;
    if (hex.empty() || hex.size() % 2 != 0 ||
        hex.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
    {
        throw OpsError(c_exitUsage, "not a hex bytes value: " + _hexValue);
    }
    return "hex://" + hex;
}

std::string encodePresetArgs(Preset const& _preset, std::vector<std::string> const& _args)
{
    if (_args.size() != _preset.params.size())
    {
        std::string expected;
        for (auto const& param : _preset.params)
        {
            expected += (expected.empty() ? "<" : " <") + std::string(param.name) + ">";
        }
        throw OpsError(c_exitUsage, std::string(_preset.group) + " " + std::string(_preset.name) +
                                        " takes " + std::to_string(_preset.params.size()) +
                                        " argument(s): " + expected);
    }
    Json::Value array(Json::arrayValue);
    for (size_t i = 0; i < _args.size(); ++i)
    {
        array.append(typedArg(_preset.params[i], _args[i]));
    }
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    return Json::writeString(writer, array);
}

int PresetResult::exitCode() const
{
    return ok ? c_exitOk : c_exitChecksFailed;
}

Json::Value PresetResult::toJson() const
{
    Json::Value value;
    value["mode"] = mode;
    value["to"] = to;
    value["method"] = method;
    value["params"] = params;
    value["ok"] = ok;
    value["status"] = receipt.status;
    if (!receipt.txHash.empty())
    {
        value["txHash"] = receipt.txHash;
        value["blockNumber"] = Json::Int64(receipt.blockNumber);
        value["gasUsed"] = receipt.gasUsed;
    }
    value["output"] = receipt.output;
    if (!receipt.message.empty())
    {
        value["message"] = receipt.message;
    }
    value["decoded"] = decoded;
    if (code)
    {
        value["code"] = *code;
        value["codeName"] = codeName;
    }
    return value;
}

PresetResult decodePresetResult(Preset const& _preset, Receipt const& _receipt, bool _sm)
{
    PresetResult result;
    result.to = std::string(_preset.to);
    result.method = std::string(_preset.signature);
    result.receipt = _receipt;
    result.decoded = Json::Value(Json::nullValue);
    auto abi = _preset.outputsAbi();
    auto outputHex =
        _receipt.output.starts_with("0x") ? _receipt.output.substr(2) : _receipt.output;
    if (_receipt.ok() && !abi.empty() && !outputHex.empty())
    {
        auto decodedText = Abi(_sm).decodeOutput(abi, _preset.methodName(), fromHex(outputHex));
        result.decoded = parseJson(decodedText, "decoded output");
        if (_preset.returnsCode() && result.decoded.isArray() && !result.decoded.empty())
        {
            result.code = codeOf(result.decoded[0]);
            if (result.code)
            {
                result.codeName = *result.code == 0 ? "SUCCESS" : precompiledCodeName(*result.code);
            }
        }
    }
    result.ok = _receipt.ok() && (!result.code || *result.code == 0);
    return result;
}

bytes PresetRunner::encode(Preset const& _preset, std::vector<std::string> const& _args) const
{
    return Abi(m_sm).encodeMethodBySignature(
        std::string(_preset.signature), encodePresetArgs(_preset, _args));
}

PresetResult PresetRunner::run(
    Preset const& _preset, std::vector<std::string> const& _args, std::string const& _to)
{
    auto to = _to.empty() ? std::string(_preset.to) : _to;
    if (to.empty() || to.starts_with("@"))
    {
        throw OpsError(c_exitUsage, "preset " + std::string(_preset.group) + " " +
                                        std::string(_preset.name) + " needs a contract address (" +
                                        to + ")");
    }
    auto params = parseJson(encodePresetArgs(_preset, _args), "preset params");
    auto data = encode(_preset, _args);
    // ticket 04: an auth chain routes write presets with a ProposalSpec through the committee
    // here and reports mode = "proposal"
    auto receipt =
        _preset.write ? m_sender.send(to, std::move(data), "") : m_sender.call(to, std::move(data));
    auto result = decodePresetResult(_preset, receipt, m_sm);
    result.to = to;
    result.params = params;
    return result;
}
}  // namespace bcos::ops
