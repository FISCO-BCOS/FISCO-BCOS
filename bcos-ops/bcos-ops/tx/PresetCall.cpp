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
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/multiprecision/cpp_int.hpp>
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
    {-53001, "FILE_NOT_EXIST"},
    {-53002, "FILE_ALREADY_EXIST"},
    {-53003, "FILE_BUILD_DIR_FAILED"},
    {-53005, "FILE_INVALID_PATH"},
    {-53006, "FILE_INVALID_TYPE"},
    {-53007, "FILE_COUNT_ERROR"},
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

bool isIntegerType(std::string_view _type)
{
    return (_type.starts_with("uint") || _type.starts_with("int")) && !_type.ends_with("]");
}

Json::Value typedArg(ParamSpec const& _spec, std::string const& _arg)
{
    if (isBytesType(_spec.type))
    {
        return toSdkBytes(_arg);
    }
    if (isIntegerType(_spec.type))
    {
        // jsoncpp parses a number beyond 64 bits as a double ("1e+21"), which the codec rejects;
        // the codec's string branch (ContractABICodec.cpp UINT/INT: u256(str) / s256(str)) takes
        // the digits verbatim, so integers always travel as decimal text. Hex is refused: the
        // codec's isUnsigned() would reject "0x…" with a less helpful message.
        auto label = std::string(_spec.name) + " (" + std::string(_spec.type) + ")";
        bool negative = _arg.starts_with("-");
        auto digits = negative ? _arg.substr(1) : _arg;
        if (digits.empty() || digits.find_first_not_of("0123456789") != std::string::npos)
        {
            throw OpsError(c_exitUsage, label + " must be a decimal integer, got: " + _arg);
        }
        bool isUnsigned = _spec.type.starts_with("uint");
        if (negative && isUnsigned)
        {
            throw OpsError(c_exitUsage, label + " cannot be negative, got: " + _arg);
        }
        // the codec's u256(str) reads a leading 0 as octal ("010" would sign 8) and wraps past
        // the type's width without a word (as does the ABI decoder on chain), so the text is
        // normalised to plain decimal and bounded by uintN / intN here
        auto magnitude = withoutLeadingZeros(digits);
        auto widthText = _spec.type.substr(isUnsigned ? 4 : 3);
        auto bits =
            widthText.empty() ? 256U : static_cast<unsigned>(std::stoul(std::string(widthText)));
        boost::multiprecision::cpp_int value(magnitude);
        auto bound = boost::multiprecision::cpp_int(1) << (isUnsigned ? bits : bits - 1);
        if (negative ? value > bound : value >= bound)
        {
            throw OpsError(c_exitUsage, label + " is out of range, got: " + _arg);
        }
        return negative && magnitude != "0" ? "-" + magnitude : magnitude;
    }
    // a zero-padded integer is text for a string / address parameter (argsToJsonArray refuses
    // it because it cannot tell)
    if ((_spec.type == "string" || _spec.type == "address") && isZeroPaddedInteger(_arg))
    {
        return _arg;
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

std::string proposalStatusName(int _status)
{
    switch (_status)  // ProposalManager.sol:19
    {
    case 0:
        return "none";
    case 1:
        return "voting";
    case 2:
        return "passed";
    case 3:
        return "denied";
    case 4:
        return "revoked";
    case 5:
        return "outdated";
    default:
        return "unknown";
    }
}

int64_t proposalInterval(Routing const& _routing)
{
    if (!_routing.interval)
    {
        return c_proposalInterval;
    }
    if (*_routing.interval < 1)
    {
        // the contract adds it to block.number; 0 expires at the creating block and a negative
        // value wraps through u256 into a huge number
        throw OpsError(c_exitUsage,
            "--interval must be >= 1 (blocks from now), got " + std::to_string(*_routing.interval));
    }
    return *_routing.interval;
}

/// CommitteeManager.sol:384-445: the proposalType byte as the console names it
std::string proposalTypeName(int _type)
{
    switch (_type)
    {
    case 11:
        return "governor";
    case 12:
        return "rate";
    case 13:
        return "voteComputer";
    case 21:
        return "deployAuthType";
    case 22:
        return "deployAuth";
    case 31:
        return "resetAdmin";
    case 41:
        return "sysConfig";
    case 51:
        return "consensusWeight";
    case 52:
        return "rmNode";
    default:
        return "type" + std::to_string(_type);
    }
}

Json::Value proposalInfoJson(Json::Value const& _fields)
{
    Json::Value value;
    value["resourceId"] = _fields.get(0U, "");
    value["proposer"] = _fields.get(1U, "");
    value["proposalType"] = _fields.get(2U, "");
    {
        // the decoder hands uint8 back as a decimal string; the name is what operators read
        auto const& type = value["proposalType"];
        int code = -1;
        try
        {
            code = type.isString() ? std::stoi(type.asString()) : type.asInt();
        }
        catch (std::exception const&)
        {}
        value["proposalTypeName"] = code >= 0 ? proposalTypeName(code) : std::string("-");
    }
    value["expiresAtBlock"] = _fields.get(3U, "");  // block.number + interval at creation
    value["status"] = _fields.get(4U, "");
    value["agreeVoters"] = _fields.get(5U, Json::Value(Json::arrayValue));
    value["againstVoters"] = _fields.get(6U, Json::Value(Json::arrayValue));
    return value;
}

Json::Value ProposalOutcome::toJson() const
{
    Json::Value value;
    value["id"] = id;
    value["status"] = status;
    value["statusName"] = statusName;
    value["executed"] = executed;
    if (execCode)
    {
        value["execCode"] = *execCode;
        value["execCodeName"] = execCodeName;
    }
    value["info"] = info;
    if (!error.empty())
    {
        value["error"] = error;
    }
    return value;
}

Json::Value PresetResult::toJson() const
{
    Json::Value value;
    value["mode"] = mode;
    value["to"] = to;
    value["method"] = method;
    value["params"] = params;
    if (!directMethod.empty())
    {
        value["directMethod"] = directMethod;
    }
    if (proposal)
    {
        value["proposal"] = proposal->toJson();
    }
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

std::string revertReason(std::string const& _outputHex, bool _sm)
{
    // Error(string): keccak256 selector 08c379a0, sm3 selector c703cb12 (SM chains hash the
    // signature with sm3)
    constexpr std::string_view c_errorSelector = "08c379a0";
    constexpr std::string_view c_smErrorSelector = "c703cb12";
    auto hex = _outputHex.starts_with("0x") ? _outputHex.substr(2) : _outputHex;
    if ((!hex.starts_with(c_errorSelector) && !hex.starts_with(c_smErrorSelector)) ||
        hex.size() < c_errorSelector.size() + 128)
    {
        return "";
    }
    try
    {
        auto decoded = Abi(_sm).decodeOutput(
            R"([{"name":"Error","type":"function","inputs":[],"outputs":[{"name":"","type":"string"}]}])",
            "Error", fromHex(hex.substr(c_errorSelector.size())));
        auto value = parseJson(decoded, "revert reason");
        return value.isArray() && !value.empty() && value[0].isString() ? value[0].asString() : "";
    }
    catch (std::exception const&)
    {
        return "";
    }
}

PresetResult decodePresetResult(Preset const& _preset, Receipt const& _receipt, bool _sm)
{
    PresetResult result;
    result.to = std::string(_preset.to);
    result.method = std::string(_preset.signature);
    result.receipt = _receipt;
    if (!_receipt.ok() && result.receipt.message.empty())
    {
        result.receipt.message = revertReason(_receipt.output, _sm);
    }
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

PresetResult PresetRunner::runCall(
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
    auto receipt =
        _preset.write ? m_sender.send(to, std::move(data), "") : m_sender.call(to, std::move(data));
    auto result = decodePresetResult(_preset, receipt, m_sm);
    result.to = to;
    result.params = params;
    return result;
}

bool PresetRunner::routesThroughProposal(Preset const& _preset) const
{
    return routesThroughProposal(m_routing, _preset);
}

bool PresetRunner::routesThroughProposal(Routing const& _routing, Preset const& _preset)
{
    return _preset.write && _preset.proposal.has_value() && _routing.proposals && !_routing.direct;
}

Preset const* PresetRunner::proposalPreset(Preset const& _preset)
{
    if (!_preset.proposal)
    {
        return nullptr;
    }
    for (auto const* candidate : presetsOf("committee"))
    {
        if (candidate->signature == _preset.proposal->signature)
        {
            return candidate;
        }
    }
    return nullptr;
}

std::vector<std::string> PresetRunner::proposalArgs(
    Preset const& _preset, std::vector<std::string> const& _args) const
{
    return proposalArgsFor(_preset, _args, m_routing);
}

std::vector<std::string> proposalArgsFor(
    Preset const& _preset, std::vector<std::string> const& _args, Routing const& _routing)
{
    if (!_preset.proposal)
    {
        throw OpsError(c_exitUsage, std::string(_preset.name) + " has no proposal form");
    }
    std::vector<std::string> out;
    std::string const grammar(_preset.proposal->args);
    size_t start = 0;
    while (start <= grammar.size())
    {
        auto end = grammar.find(',', start);
        auto token =
            grammar.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (token == "$interval")
        {
            out.push_back(std::to_string(proposalInterval(_routing)));
        }
        else if (token.size() > 1 && token[0] == '$')
        {
            auto index = static_cast<size_t>(std::stoul(token.substr(1)));
            if (index >= _args.size())
            {
                throw OpsError(c_exitUsage, "proposal argument " + token + " out of range");
            }
            out.push_back(_args[index]);
        }
        else
        {
            out.push_back(token);
        }
        if (end == std::string::npos)
        {
            break;
        }
        start = end + 1;
    }
    return out;
}

std::string PresetRunner::proposalManagerAddress()
{
    if (m_proposalMgr.empty())
    {
        auto const* row = findPreset("committee", "proposal-mgr-address");
        auto result = runCall(*row, {}, "");
        if (!result.ok || !result.decoded.isArray() || result.decoded.empty())
        {
            // TransactionStatus::CallAddressError (19): nothing deployed at 0x…10001, which is
            // what a chain without auth (no AuthInitializer genesis step) looks like
            std::string hint =
                result.receipt.status == 19 ?
                    " (no committee at " + std::string(row->to) + ": not an auth chain?)" :
                    "";
            throw OpsError(c_exitUsage, "CommitteeManager._proposalMgr() failed: status " +
                                            std::to_string(result.receipt.status) + hint);
        }
        m_proposalMgr = result.decoded[0].asString();
    }
    return m_proposalMgr;
}

ProposalOutcome PresetRunner::proposalOutcome(std::string const& _id, Receipt const& _createReceipt)
{
    ProposalOutcome outcome;
    outcome.id = _id;
    auto manager = proposalManagerAddress();
    auto status = runCall(*findPreset("committee", "proposal-status"), {_id}, manager);
    if (status.ok && status.decoded.isArray() && !status.decoded.empty())
    {
        outcome.status = codeOf(status.decoded[0]).value_or(0);
        outcome.statusName = proposalStatusName(outcome.status);
    }
    else
    {
        outcome.statusName = "unknown";
        outcome.error = "getProposalStatus(" + _id + ") failed: status " +
                        std::to_string(status.receipt.status) + " " + status.receipt.message;
    }
    outcome.executed = outcome.status == 2;
    auto info = runCall(*findPreset("committee", "proposal"), {_id}, manager);
    if (info.ok && info.decoded.isArray())
    {
        outcome.info = proposalInfoJson(info.decoded);
    }
    else if (outcome.error.empty())
    {
        outcome.error = "getProposalInfo(" + _id + ") failed: status " +
                        std::to_string(info.receipt.status) + " " + info.receipt.message;
    }
    // CommitteeManager.voteProposal emits execResult(int256) with the precompiled's return
    // code when the vote passes; it is the only place that code surfaces (the receipt is ok)
    std::string signature = "execResult(int256)";
    auto topicHash = m_sm ? bcos::crypto::SM3().hash(bytesConstRef(signature)) :
                            bcos::crypto::Keccak256().hash(bytesConstRef(signature));
    auto topic = topicHash.hexPrefixed();
    for (auto const& entry : _createReceipt.raw["logEntries"])
    {
        auto topics = entry["topics"];
        if (!topics.isArray() || topics.empty() || topics[0].asString() != topic)
        {
            continue;
        }
        auto data = entry["data"].asString();
        if (data.size() >= 8)
        {
            auto low = static_cast<uint32_t>(std::stoul(data.substr(data.size() - 8), nullptr, 16));
            outcome.execCode = static_cast<int32_t>(low);
            outcome.execCodeName =
                *outcome.execCode == 0 ? "SUCCESS" : precompiledCodeName(*outcome.execCode);
        }
    }
    return outcome;
}

PresetResult PresetRunner::run(
    Preset const& _preset, std::vector<std::string> const& _args, std::string const& _to)
{
    if (routesThroughProposal(_preset))
    {
        auto const* committeePreset = proposalPreset(_preset);
        if (committeePreset == nullptr)
        {
            throw OpsError(
                c_exitUsage, "no committee preset for " + std::string(_preset.proposal->signature));
        }
        auto result = runCall(*committeePreset, proposalArgs(_preset, _args), "");
        result.mode = "proposal";
        result.directMethod = std::string(_preset.signature);
        if (result.ok && result.decoded.isArray() && !result.decoded.empty())
        {
            result.proposal = proposalOutcome(result.decoded[0].asString(), result.receipt);
            result.ok = (result.proposal->status == 1 || result.proposal->status == 2) &&
                        (!result.proposal->execCode || *result.proposal->execCode == 0);
        }
        return result;
    }
    auto result = runCall(_preset, _args, _to);
    // a `committee propose-*` call creates a proposal too: report its id and status the same way
    if (_preset.write && _preset.group == "committee" && _preset.signature.starts_with("create") &&
        result.ok && result.decoded.isArray() && !result.decoded.empty())
    {
        result.mode = "proposal";
        result.proposal = proposalOutcome(result.decoded[0].asString(), result.receipt);
        result.ok = (result.proposal->status == 1 || result.proposal->status == 2) &&
                    (!result.proposal->execCode || *result.proposal->execCode == 0);
    }
    // vote / revoke: the vote that passes a proposal executes it, so report where it stands
    // and the executed call's code (a passed add-sealer can still fail inside the precompiled)
    if (_preset.write && _preset.group == "committee" &&
        (_preset.name == "vote" || _preset.name == "revoke") && result.ok && !_args.empty())
    {
        result.proposal = proposalOutcome(_args[0], result.receipt);
        result.ok = !result.proposal->execCode || *result.proposal->execCode == 0;
    }
    return result;
}
}  // namespace bcos::ops
