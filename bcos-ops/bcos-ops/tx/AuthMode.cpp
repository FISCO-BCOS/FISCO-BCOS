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
 *  limitations under the License. *
 * @file AuthMode.cpp
 */
#include "AuthMode.h"
#include "bcos-ops/OpsError.h"

namespace bcos::ops
{
bool chainUsesProposals(
    RpcCall const& _call, std::string const& _group, std::optional<NodeDir> const& _nodeDir)
{
    Json::Value params(Json::arrayValue);
    params.append(_group);
    params.append("");
    params.append("auth_check_status");
    try
    {
        auto result = _call("getSystemConfigByKey", params);
        auto value = result.isObject() ? result.get("value", "").asString() : result.asString();
        // the value is the number the committee set: 0 off, anything else on
        if (!value.empty())
        {
            return value != "0";
        }
    }
    catch (OpsRpcError const&)
    {
        // never set (LedgerError::GetStorageError 3008 on a fresh chain): genesis decides
    }
    return _nodeDir ? _nodeDir->authCheck : false;
}

bool chainUsesProposals(Connection const& _connection)
{
    return chainUsesProposals(_connection.call, _connection.group, _connection.nodeDir);
}
}  // namespace bcos::ops
