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
 * @file GroupFacts.cpp
 */
#include "GroupFacts.h"
#include "bcos-ops/OpsError.h"

namespace bcos::ops
{
GroupFacts groupFacts(
    RpcCall const& _call, std::string const& _group, std::optional<std::string> const& _selfNodeId)
{
    Json::Value params(Json::arrayValue);
    params.append(_group);
    auto info = _call("getGroupInfo", params);
    GroupFacts facts;
    facts.chainId = info["chainID"].asString();
    auto const& nodeList = info["nodeList"];
    Json::Value const* self = nullptr;
    for (auto const& entry : nodeList)
    {
        if (!self || (_selfNodeId && entry["nodeID"].asString() == *_selfNodeId))
        {
            self = &entry;
        }
    }
    if (self == nullptr)
    {
        throw OpsError(c_exitUsage, "getGroupInfo: empty nodeList for group " + _group);
    }
    auto ini = parseJson((*self)["iniConfig"].asString(), "getGroupInfo.iniConfig");
    facts.version = ini["binaryInfo"]["version"].asString();
    facts.smCrypto = ini["smCryptoType"].asBool();
    facts.authCheck = ini["isAuthCheck"].asBool();
    return facts;
}
}  // namespace bcos::ops
