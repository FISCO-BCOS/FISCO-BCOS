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
 * @file Smoke.cpp
 */
#include "Smoke.h"
#include "Abi.h"
#include "HelloWorld.h"
#include "bcos-ops/OpsError.h"
#include <bcos-utilities/DataConvertUtility.h>
#include <algorithm>

namespace bcos::ops
{
namespace
{
// bcos::protocol::TransactionStatus::PermissionDenied (bcos-protocol/TransactionStatus.h)
constexpr int32_t c_statusPermissionDenied = 18;

bool looksAuthDenied(Receipt const& _receipt, std::optional<bool> _authCheck)
{
    if (_receipt.status == c_statusPermissionDenied)
    {
        return true;
    }
    auto lower = _receipt.message;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower.find("permission") != std::string::npos || lower.find("auth") != std::string::npos)
    {
        return true;
    }
    // auth-enabled chain and the deploy failed without a more specific reason
    return _authCheck.value_or(false) && !_receipt.ok();
}
}  // namespace

Json::Value SmokeStep::toJson() const
{
    Json::Value value;
    value["name"] = name;
    value["ok"] = ok;
    value["txHash"] = txHash;
    value["status"] = status;
    value["detail"] = detail;
    return value;
}

int SmokeResult::exitCode() const
{
    if (ok)
    {
        return c_exitOk;
    }
    return c_exitChecksFailed;
}

Json::Value SmokeResult::toJson() const
{
    Json::Value value;
    value["ok"] = ok;
    value["reason"] = reason;
    value["steps"] = Json::Value(Json::arrayValue);
    for (auto const& step : steps)
    {
        value["steps"].append(step.toJson());
    }
    return value;
}

SmokeResult runSmoke(Sender& _sender, bool _sm, std::optional<bool> _authCheck)
{
    SmokeResult result;
    Abi abi(_sm);
    std::string const abiText(helloworld::c_abi);

    // 1. deploy
    SmokeStep deploy;
    deploy.name = "deploy";
    auto deployReceipt = _sender.send("", fromHex(std::string(helloworld::binary(_sm))), abiText);
    deploy.txHash = deployReceipt.txHash;
    deploy.status = deployReceipt.status;
    deploy.ok = deployReceipt.ok() && !deployReceipt.contractAddress.empty();
    deploy.detail = deploy.ok ? "contract " + deployReceipt.contractAddress :
                                "message " + deployReceipt.message;
    result.steps.push_back(deploy);
    if (!deploy.ok)
    {
        result.reason =
            looksAuthDenied(deployReceipt, _authCheck) ? "auth_denied" : "deploy_failed";
        return result;
    }
    auto const& address = deployReceipt.contractAddress;

    // 2. set
    SmokeStep set;
    set.name = "set";
    auto setData = abi.encodeMethod(abiText, "set", "[\"" + std::string(c_smokeMessage) + "\"]");
    auto setReceipt = _sender.send(address, std::move(setData), "");
    set.txHash = setReceipt.txHash;
    set.status = setReceipt.status;
    set.ok = setReceipt.ok();
    set.detail = set.ok ? "gasUsed " + setReceipt.gasUsed : "message " + setReceipt.message;
    result.steps.push_back(set);
    if (!set.ok)
    {
        result.reason = looksAuthDenied(setReceipt, _authCheck) ? "auth_denied" : "set_failed";
        return result;
    }

    // 3. get
    SmokeStep get;
    get.name = "get";
    auto getData = abi.encodeMethod(abiText, "get", "[]");
    auto getReceipt = _sender.call(address, std::move(getData));
    get.status = getReceipt.status;
    std::string decoded;
    if (getReceipt.ok())
    {
        auto outputHex = getReceipt.output;
        if (outputHex.starts_with("0x"))
        {
            outputHex = outputHex.substr(2);
        }
        decoded = abi.decodeOutput(abiText, "get", fromHex(outputHex));
    }
    get.ok = getReceipt.ok() && decoded.find(c_smokeMessage) != std::string::npos;
    get.detail =
        get.ok ? decoded : "status " + std::to_string(getReceipt.status) + " output " + decoded;
    result.steps.push_back(get);
    if (!get.ok)
    {
        result.reason = "get_failed";
        return result;
    }
    result.ok = true;
    return result;
}
}  // namespace bcos::ops
