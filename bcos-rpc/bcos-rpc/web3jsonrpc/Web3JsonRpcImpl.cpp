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
        if (auto const& [valid, msg] = parseRequestAndValidate(_requestBody, request); !valid)
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(InvalidRequest, msg));
        }
        response["id"] = request["id"];
        if (auto const handler = m_endpointsMapping.findHandler(request["method"].asString());
            handler.has_value())
        {
            if (c_fileLogLevel == TRACE) [[unlikely]]
            {
                WEB3_LOG(TRACE) << LOG_BADGE("Web3Request")
                                << LOG_KV("request", printJson(request));
            }
            task::wait([](Web3JsonRpcImpl* self, EndpointsMapping::Handler _handler,
                           Json::Value _request, Sender sender) -> task::Task<void> {
                Json::Value resp;
                try
                {
                    // FIXME)): throw exception here will core dump
                    Json::Value const& params = _request["params"];

                    co_await (self->m_endpoints.*_handler)(params, resp);
                    resp["id"] = _request["id"];
                }
                catch (const JsonRpcException& e)
                {
                    buildJsonError(_request, e.code(), e.msg(), resp);
                }
                catch (bcos::Error const& e)
                {
                    buildJsonError(_request, InternalError, e.errorMessage(), resp);
                }
                catch (const std::exception& e)
                {
                    buildJsonError(_request, InternalError, boost::diagnostic_information(e), resp);
                }
                auto&& respBytes = toBytesResponse(resp);
                if (c_fileLogLevel == TRACE) [[unlikely]]
                {
                    std::string method = _request["method"].asString();
                    WEB3_LOG(TRACE)
                        << LOG_BADGE("Web3Response") << LOG_KV("method", method)
                        << LOG_KV("response",
                               std::string_view((const char*)(respBytes.data()), respBytes.size()));
                }
                sender(std::move(respBytes));
            }(this, handler.value(), std::move(request), _sender));
            return;
        }
        BOOST_THROW_EXCEPTION(JsonRpcException(MethodNotFound, "Method not found"));
    }
    catch (const JsonRpcException& e)
    {
        buildJsonError(request, e.code(), e.msg(), response);
    }
    catch (...)
    {
        buildJsonError(request, InternalError, "Internal error", response);
    }
    auto&& resp = toBytesResponse(response);
    WEB3_LOG(DEBUG) << LOG_BADGE("onRPCRequest") << LOG_DESC("response with exception")
                    << LOG_KV("request", _requestBody)
                    << LOG_KV("response", std::string_view((const char*)resp.data(), resp.size()));
    _sender(std::move(resp));
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
    auto builder = Json::StreamWriterBuilder();
    builder["commentStyle"] = "None";
    builder["indentation"] = "";
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());

    bcos::bytes out;
    boost::iostreams::stream<JsonSink> outputStream(out);

    writer->write(jResp, &outputStream);
    writer.reset();
    return out;
}