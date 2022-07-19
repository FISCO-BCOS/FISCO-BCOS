/*
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @file JsonRpcRequest.cpp
 * @author: octopus
 * @date 2021-05-24
 */

#include <bcos-cpp-sdk/rpc/Common.h>
#include <bcos-cpp-sdk/rpc/JsonRpcRequest.h>
#include <json/json.h>

using namespace bcos;
using namespace cppsdk;
using namespace jsonrpc;

std::string JsonRpcRequest::toJson()
{
    Json::Value jReq;
    jReq["jsonrpc"] = m_jsonrpc;
    jReq["method"] = m_method;
    jReq["id"] = m_id;
    jReq["params"] = params();

    Json::FastWriter writer;
    std::string s = writer.write(jReq);
    RPCREQ_LOG(TRACE) << LOG_BADGE("toJson") << LOG_KV("request", s);
    return s;
}

void JsonRpcRequest::fromJson(const std::string& _request)
{
    Json::Value root;
    Json::Reader jsonReader;
    std::string errorMessage;

    try
    {
        std::string jsonrpc = "";
        std::string method = "";
        int64_t id = 0;
        do
        {
            if (!jsonReader.parse(_request, root))
            {
                errorMessage = "invalid request json object";
                break;
            }

            if (!root.isMember("jsonrpc"))
            {
                errorMessage = "request has no jsonrpc field";
                break;
            }
            jsonrpc = root["jsonrpc"].asString();

            if (!root.isMember("method"))
            {
                errorMessage = "request has no method field";
                break;
            }
            method = root["method"].asString();

            if (root.isMember("id"))
            {
                id = root["id"].asInt64();
            }

            if (!root.isMember("params"))
            {
                errorMessage = "request has no params field";
                break;
            }

            if (!root["params"].isArray())
            {
                errorMessage = "request params is not array object";
                break;
            }

            auto jParams = root["params"];

            m_jsonrpc = jsonrpc;
            m_method = method;
            m_id = id;
            m_params = jParams;

            // RPCREQ_LOG(DEBUG) << LOG_BADGE("fromJson") << LOG_KV("method", method)
            //                   << LOG_KV("request", _request);

            return;

        } while (0);
    }
    catch (const std::exception& e)
    {
        RPCREQ_LOG(WARNING) << LOG_BADGE("fromJson") << LOG_KV("request", _request)
                            << LOG_KV("error", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(
            JsonRpcException(JsonRpcError::ParseError, "Invalid JSON was received by the server."));
    }

    RPCREQ_LOG(WARNING) << LOG_BADGE("fromJson") << LOG_KV("request", _request)
                        << LOG_KV("errorMessage", errorMessage);

    BOOST_THROW_EXCEPTION(JsonRpcException(
        JsonRpcError::InvalidRequest, "The JSON sent is not a valid Request object."));
}