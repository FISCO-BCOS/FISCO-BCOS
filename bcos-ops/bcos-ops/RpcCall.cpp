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
 * @file RpcCall.cpp
 */
#include "RpcCall.h"
#include "OpsError.h"

namespace bcos::ops
{
std::string buildJsonRpcRequest(std::string_view _method, Json::Value const& _params, int64_t _id)
{
    Json::Value request;
    request["jsonrpc"] = "2.0";
    request["id"] = static_cast<Json::Int64>(_id);
    request["method"] = std::string(_method);
    request["params"] = _params.isNull() ? Json::Value(Json::arrayValue) : _params;
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, request);
}

Json::Value parseJson(std::string_view _text, std::string_view _what)
{
    Json::Value value;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(_text.data(), _text.data() + _text.size(), &value, &errors))
    {
        throw OpsError(c_exitUsage, std::string(_what) + ": response is not JSON: " + errors);
    }
    return value;
}

Json::Value parseJsonRpcResponse(std::string_view _body, std::string_view _method)
{
    auto response = parseJson(_body, _method);
    if (response.isMember("error") && !response["error"].isNull())
    {
        auto const& error = response["error"];
        throw OpsError(c_exitUsage,
            std::string(_method) + " failed: code=" + std::to_string(error.get("code", 0).asInt()) +
                " msg=" + error.get("message", "").asString());
    }
    return response["result"];
}

Json::Value unwrapStringResult(std::string_view _method, Json::Value _result)
{
    if ((_method == "getSyncStatus" || _method == "getConsensusStatus") && _result.isString())
    {
        return parseJson(_result.asString(), _method);
    }
    return _result;
}
}  // namespace bcos::ops
