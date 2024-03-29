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
 * @file Web3JsonRpcImpl.cpp
 * @author: kyonGuo
 * @date 2024/3/21
 */

#include "Web3JsonRpcImpl.h"
#include <bcos-task/Wait.h>

using namespace bcos;
using namespace bcos::rpc;

void Web3JsonRpcImpl::onRPCRequest(std::string_view _requestBody, Sender _sender)
{
    Json::Value request;
    Json::Value response;
    try
    {
        if (auto const result{parseRequestAndValidate(_requestBody, request)};
            !std::get<bool>(result))
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(InvalidRequest, std::get<std::string>(result)));
        }
        if (auto const handler{m_endpointsMapping.findHandler(request["method"].asString())};
            handler.has_value())
        {
            if (c_fileLogLevel == TRACE) [[unlikely]]
            {
                RPC_IMPL_LOG(TRACE) << LOG_BADGE("onRPCRequest") << LOG_KV("request", _requestBody);
            }
            Json::Value const& params = request["params"];
            task::wait([](Web3JsonRpcImpl* self, EndpointsMapping::Handler _handler,
                           Json::Value _params, Sender sender) -> task::Task<void> {
                Json::Value response;
                co_await self->handleRequest(_handler, _params, response);
                auto&& resp = toBytesResponse(response);
                if (c_fileLogLevel == TRACE) [[unlikely]]
                {
                    RPC_IMPL_LOG(TRACE)
                        << LOG_BADGE("onRPCRequest")
                        << LOG_KV("response",
                               std::string_view((const char*)(resp.data()), resp.size()));
                }
                sender(std::move(resp));
            }(this, handler.value(), std::move(params), std::move(_sender)));
            return;
        }
        BOOST_THROW_EXCEPTION(JsonRpcException(MethodNotFound, "Method not found"));
    }
    catch (const JsonRpcException& e)
    {
        buildJsonError(request, e.code(), e.msg(), response);
    }
    catch (const std::exception& e)
    {
        buildJsonError(request, InternalError, e.what(), response);
    }
    auto&& resp = toBytesResponse(response);
    RPC_IMPL_LOG(DEBUG) << LOG_BADGE("onRPCRequest") << LOG_DESC("response with exception")
                        << LOG_KV("request", _requestBody)
                        << LOG_KV(
                               "response", std::string_view((const char*)resp.data(), resp.size()));
    _sender(std::move(resp));
}

task::Task<void> Web3JsonRpcImpl::handleRequest(
    EndpointsMapping::Handler handler, Json::Value const& request, Json::Value& response)
{
    try
    {
        co_await (m_endpoints.*handler)(request, response);
    }
    catch (std::exception const& e)
    {
        buildJsonError(request, InternalError, e.what(), response);
    }
    catch (...)
    {
        buildJsonError(request, InternalError, "Internal error", response);
    }
    co_return;
}


std::tuple<bool, std::string> Web3JsonRpcImpl::parseRequestAndValidate(
    std::string_view request, Json::Value& root)
{
    if (Json::Reader jsonReader; !jsonReader.parse(request.begin(), request.end(), root))
    {
        return {false, "Parse json failed"};
    }
    if (auto result{JsonValidator::validate(root)}; !std::get<bool>(result))
    {
        return result;
    }
    return {true, ""};
}

bcos::bytes Web3JsonRpcImpl::toBytesResponse(Json::Value const& jResp)
{
    std::unique_ptr<Json::StreamWriter> writer(Json::StreamWriterBuilder().newStreamWriter());

    bcos::bytes out;
    boost::iostreams::stream<JsonSink> outputStream(out);

    writer->write(jResp, &outputStream);
    writer.reset();
    return out;
}

void Web3JsonRpcImpl::buildJsonError(
    Json::Value const& request, int32_t code, std::string const& message, Json::Value& response)
{
    response["jsonrpc"] = "2.0";
    response["id"] = request["id"];
    response["error"]["code"] = code;
    response["error"]["message"] = message;
}
