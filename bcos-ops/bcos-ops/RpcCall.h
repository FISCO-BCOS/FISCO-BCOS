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
 * @brief the single injection point of bcos-ops: a synchronous JSON-RPC call. RPC over
 *        WebSocket and the local socket are two implementations; tests pass a table-driven fake.
 * @file RpcCall.h
 */
#pragma once

#include "NodeDir.h"
#include <json/json.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace bcos::ops
{
/// returns the `result` member; throws OpsError{1} on transport error or a JSON-RPC `error`
using RpcCall = std::function<Json::Value(std::string_view method, Json::Value const& params)>;

struct Connection
{
    RpcCall call;
    std::string source;  // "rpc" | "attach"
    std::string group;
    std::string endpoint;
    std::optional<NodeDir> nodeDir;
    std::shared_ptr<void> keepAlive;  // owns the transport
};

std::string buildJsonRpcRequest(std::string_view _method, Json::Value const& _params, int64_t _id);
/// parses a JSON-RPC response body; throws OpsError{1} when it carries `error` or is not JSON
Json::Value parseJsonRpcResponse(std::string_view _body, std::string_view _method);
/// getSyncStatus / getConsensusStatus return a JSON object encoded as a string; unwrap it
Json::Value unwrapStringResult(std::string_view _method, Json::Value _result);
Json::Value parseJson(std::string_view _text, std::string_view _what);
}  // namespace bcos::ops
