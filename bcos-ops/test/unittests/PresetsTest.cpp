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
 * @file PresetsTest.cpp
 */
#include "bcos-ops/tx/Presets.h"
#include "bcos-ops/OpsError.h"
#include "bcos-ops/RpcCall.h"
#include "bcos-ops/tx/Abi.h"
#include "bcos-ops/tx/PresetCall.h"
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>

namespace bcos::ops::test
{
namespace
{
std::string presetDummyArg(std::string_view _type)
{
    if (_type == "string")
    {
        return "text";
    }
    if (_type == "address")
    {
        return "0x0000000000000000000000000000000000001003";
    }
    if (_type == "bool")
    {
        return "true";
    }
    if (_type.starts_with("bytes"))
    {
        return "0x12345678";
    }
    return "7";  // uintN / intN
}

std::vector<std::string> presetDummyArgs(Preset const& _preset)
{
    std::vector<std::string> args;
    for (auto const& param : _preset.params)
    {
        args.push_back(presetDummyArg(param.type));
    }
    return args;
}

/// abi.encode(int32(_code))
std::string presetInt32Output(int32_t _code)
{
    u256 value = _code < 0 ? u256(0) - u256(-static_cast<int64_t>(_code)) : u256(_code);
    return "0x" + toHex(toBigEndian(value));
}
}  // namespace

BOOST_AUTO_TEST_SUITE(PresetsTest)

BOOST_AUTO_TEST_CASE(everyContractPresetEncodes)
{
    for (auto const& preset : allPresets())
    {
        if (preset.isRpc())
        {
            BOOST_CHECK(preset.params.empty());
            continue;
        }
        BOOST_CHECK_EQUAL(preset.params.size(), signatureInputTypes(preset.signature).size());
        auto args = presetDummyArgs(preset);
        for (bool sm : {false, true})
        {
            auto data = Abi(sm).encodeMethodBySignature(
                std::string(preset.signature), encodePresetArgs(preset, args));
            BOOST_CHECK_MESSAGE(data.size() >= 4, std::string(preset.signature));
        }
    }
}

BOOST_AUTO_TEST_CASE(everyOutputsFragmentParsesAndDecodes)
{
    Abi abi(false);
    size_t withOutputs = 0;
    for (auto const& preset : allPresets())
    {
        auto fragment = preset.outputsAbi();
        if (fragment.empty())
        {
            continue;
        }
        ++withOutputs;
        auto parsed = parseJson(fragment, "fragment");
        BOOST_REQUIRE(parsed.isArray());
        BOOST_CHECK_EQUAL(parsed[0]["name"].asString(), preset.methodName());
        if (preset.returnsCode())
        {
            auto decoded = abi.decodeOutput(fragment, preset.methodName(),
                fromHex("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff3860"));
            BOOST_CHECK_MESSAGE(decoded.find("-51104") != std::string::npos, decoded);
        }
    }
    BOOST_CHECK(withOutputs > 20);
}

BOOST_AUTO_TEST_CASE(writePresetsOfGovernedGroupsRouteOrAreDirect)
{
    for (auto const& preset : allPresets())
    {
        if (!preset.write ||
            (preset.group != "consensus" && preset.group != "sysconfig" && preset.group != "auth"))
        {
            continue;
        }
        // committee-only writes need a proposal on an auth chain; the others are callable by
        // the governor / contract admin directly. The three-argument addSealer has no proposal
        // in the CommitteeManager bytecode (spec §5.1.4), so it stays direct.
        bool committee = preset.access == Access::Committee && preset.name != "add-sealer-term";
        BOOST_CHECK_MESSAGE(committee == preset.proposal.has_value(), std::string(preset.name));
        if (preset.proposal)
        {
            BOOST_CHECK(preset.proposal->signature.starts_with("create"));
            BOOST_CHECK(preset.proposal->args.find("$interval") != std::string_view::npos);
        }
    }
    BOOST_CHECK(!findPreset("consensus", "set-term-weight")->proposal);
    BOOST_CHECK(findPreset("consensus", "add-sealer")->proposal);
    BOOST_CHECK_EQUAL(
        findPreset("consensus", "add-observer")->proposal->args, "$0,0,true,$interval");
    BOOST_CHECK(findPreset("nope", "x") == nullptr);
    BOOST_CHECK_EQUAL(presetsOf("balance").size(), 7U);
}

BOOST_AUTO_TEST_CASE(bytesArgumentsBecomeSdkHex)
{
    BOOST_CHECK_EQUAL(toSdkBytes("0x12345678"), "hex://12345678");
    BOOST_CHECK_EQUAL(toSdkBytes("12345678"), "hex://12345678");
    BOOST_CHECK_EQUAL(toSdkBytes("hex://12345678"), "hex://12345678");
    BOOST_CHECK_THROW(toSdkBytes("0x123"), OpsError);
    BOOST_CHECK_THROW(toSdkBytes("0xzz"), OpsError);
    auto const* preset = findPreset("auth", "check-method-auth");
    auto json =
        encodePresetArgs(*preset, {"0x0000000000000000000000000000000000001003", "0x4ed3885e",
                                      "0x0000000000000000000000000000000000000001"});
    BOOST_CHECK(json.find("\"hex://4ed3885e\"") != std::string::npos);
    BOOST_CHECK_THROW(encodePresetArgs(*preset, {"0x1003"}), OpsError);  // wrong count
    // a numeric-looking string parameter stays a string
    auto const* set = findPreset("sysconfig", "set");
    BOOST_CHECK_EQUAL(
        encodePresetArgs(*set, {"tx_count_limit", "2000"}), R"(["tx_count_limit","2000"])");
    auto const* weight = findPreset("consensus", "set-weight");
    BOOST_CHECK_EQUAL(encodePresetArgs(*weight, {"3a1f", "2"}),
        R"(["3a1f","2"])");  // integers travel as digit strings
}

// the codec's u256(str) reads a leading 0 as octal and wraps past the type's width, so integer
// arguments are normalised to plain decimal and bounded by their uintN before they get there
BOOST_AUTO_TEST_CASE(integerArgumentsAreDecimalAndBoundedByTheirWidth)
{
    auto const* weight = findPreset("consensus", "set-weight");  // (string, uint256)
    BOOST_CHECK_EQUAL(encodePresetArgs(*weight, {"3a1f", "010"}), R"(["3a1f","10"])");
    BOOST_CHECK_EQUAL(encodePresetArgs(*weight, {"3a1f", "000"}), R"(["3a1f","0"])");
    BOOST_CHECK_THROW(encodePresetArgs(*weight, {"3a1f", "-1"}), OpsError);
    auto const* governor = findPreset("committee", "propose-governor");  // (address, uint32, …)
    auto const account = std::string("0x0000000000000000000000000000000000000001");
    BOOST_CHECK(encodePresetArgs(*governor, {account, "4294967295", "0"}).find(R"("4294967295")") !=
                std::string::npos);
    BOOST_CHECK_THROW(encodePresetArgs(*governor, {account, "4294967296", "0"}), OpsError);
    auto const* authType = findPreset("auth", "set-deploy-auth-type");  // (uint8)
    BOOST_CHECK_EQUAL(encodePresetArgs(*authType, {"255"}), R"(["255"])");
    BOOST_CHECK_THROW(encodePresetArgs(*authType, {"256"}), OpsError);
    BOOST_CHECK_THROW(encodePresetArgs(*authType, {"0256"}), OpsError);  // 256, not octal 174
    auto const* add = findPreset("balance", "add");                      // (address, uint256)
    auto const max = std::string(
        "115792089237316195423570985008687907853269984665640564039457584007913129639935");
    BOOST_CHECK(encodePresetArgs(*add, {account, max}).find(max) != std::string::npos);
    BOOST_CHECK_THROW(
        encodePresetArgs(*add,
            {account,
                "115792089237316195423570985008687907853269984665640564039457584007913129639936"}),
        OpsError);
}

BOOST_AUTO_TEST_CASE(negativeCodesMapToNames)
{
    BOOST_CHECK_EQUAL(precompiledCodeName(-50000), "NO_AUTHORIZED");
    BOOST_CHECK_EQUAL(precompiledCodeName(-51100), "INVALID_NODE_ID");
    BOOST_CHECK_EQUAL(precompiledCodeName(-51101), "LAST_SEALER");
    BOOST_CHECK_EQUAL(precompiledCodeName(-51102), "INVALID_WEIGHT");
    BOOST_CHECK_EQUAL(precompiledCodeName(-51103), "NODE_NOT_EXIST");
    BOOST_CHECK_EQUAL(precompiledCodeName(-51104), "SEALER_MUST_BE_OBSERVER_FIRST");
    BOOST_CHECK_EQUAL(precompiledCodeName(-51404), "INVALID_INSUFFICIENT_BALANCE");
    BOOST_CHECK_EQUAL(precompiledCodeName(-1), "");
}

BOOST_AUTO_TEST_CASE(decodeResultReadsCodeFromOutputNotStatus)
{
    auto const* preset = findPreset("consensus", "add-sealer");
    Receipt receipt;
    receipt.status = 0;
    receipt.txHash = "0x3f";
    receipt.output = presetInt32Output(-51104);
    auto result = decodePresetResult(*preset, receipt, false);
    BOOST_REQUIRE(result.code.has_value());
    BOOST_CHECK_EQUAL(*result.code, -51104);
    BOOST_CHECK_EQUAL(result.codeName, "SEALER_MUST_BE_OBSERVER_FIRST");
    BOOST_CHECK(!result.ok);
    BOOST_CHECK_EQUAL(result.exitCode(), c_exitChecksFailed);
    auto json = result.toJson();
    BOOST_CHECK_EQUAL(json["codeName"].asString(), "SEALER_MUST_BE_OBSERVER_FIRST");
    BOOST_CHECK_EQUAL(json["mode"].asString(), "direct");

    receipt.output = presetInt32Output(0);
    auto success = decodePresetResult(*preset, receipt, false);
    BOOST_CHECK(success.ok);
    BOOST_CHECK_EQUAL(success.codeName, "SUCCESS");

    receipt.status = 16;  // a reverted receipt is a failure whatever the output says
    BOOST_CHECK(!decodePresetResult(*preset, receipt, false).ok);

    // sysconfig get → (string,int64)
    auto const* get = findPreset("sysconfig", "get");
    Receipt callResult;
    callResult.status = 0;
    callResult.output =
        "0x0000000000000000000000000000000000000000000000000000000000000040"
        "0000000000000000000000000000000000000000000000000000000000000036"
        "0000000000000000000000000000000000000000000000000000000000000004"
        "3130303000000000000000000000000000000000000000000000000000000000";
    auto decoded = decodePresetResult(*get, callResult, false);
    BOOST_CHECK(!decoded.code.has_value());
    BOOST_CHECK(decoded.ok);
    BOOST_CHECK_EQUAL(decoded.decoded[0].asString(), "1000");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
