/**
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file util.cpp
 * @author: kyonGuo
 * @date 2024/3/29
 */

#include "util.h"

using namespace bcos::rpc;

void bcos::rpc::buildJsonContent(Json::Value& result, Json::Value& response)
{
    response["jsonrpc"] = "2.0";
    response["result"].swap(result);
}

void bcos::rpc::buildJsonError(
    Json::Value const& request, int32_t code, std::string message, Json::Value& response)
{
    response["jsonrpc"] = "2.0";
    // maybe request not init
    response["id"] = request.isMember("id") ? request["id"] : Json::Value::null;
    Json::Value error;
    error["code"] = code;
    error["message"] = std::move(message);
    response["error"] = std::move(error);
}

void bcos::rpc::buildJsonErrorWithData(
    Json::Value& data, int32_t code, std::string message, Json::Value& response)
{
    response["jsonrpc"] = "2.0";
    // maybe request not init
    Json::Value error = Json::objectValue;
    error["code"] = code;
    error["message"] = std::move(message);
    error["data"].swap(data);
    response["error"] = std::move(error);
}
