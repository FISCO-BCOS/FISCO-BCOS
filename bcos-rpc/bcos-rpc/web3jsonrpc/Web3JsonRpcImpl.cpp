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
#include "bcos-task/Wait.h"

using namespace bcos;
using namespace bcos::rpc;

bcos::rpc::Web3JsonRpcImpl::Web3JsonRpcImpl(std::string _groupId, uint32_t _batchRequestSizeLimit,
    bcos::rpc::GroupManager::Ptr _groupManager,
    bcos::gateway::GatewayInterface::Ptr _gatewayInterface,
    std::shared_ptr<boostssl::ws::WsService> _wsService, FilterSystem::Ptr filterSystem)
  : m_groupManager(std::move(_groupManager)),
    m_gatewayInterface(std::move(_gatewayInterface)),
    m_wsService(std::move(_wsService)),
    m_groupId(std::move(_groupId)),
    m_batchRequestSizeLimit(_batchRequestSizeLimit),
    m_endpoints(m_groupManager->getNodeService(m_groupId, ""), std::move(filterSystem))
{
    RPC_LOG(INFO) << LOG_KV("[NEWOBJ][Web3JsonRpcImpl]", this);
}

void Web3JsonRpcImpl::handleRequest(
    Json::Value _request, std::function<void(Json::Value)> _callback)
{
    Json::Value response;
    try
    {
        auto startT = utcTime();
        if (c_fileLogLevel == TRACE) [[unlikely]]
        {
            WEB3_LOG(TRACE) << LOG_BADGE("handleRequest") << LOG_DESC("begin")
                            << LOG_KV("request", printJson(_request));
        }

        // check request params
        if (auto result{JsonValidator::validate(_request)}; !std::get<bool>(result))
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(InvalidRequest, std::get<std::string>(result)));
        }

        std::string method = _request["method"].asString();
        response["id"] = _request["id"];

        auto optHandler = m_endpointsMapping.findHandler(method);
        if (!optHandler.has_value())
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(MethodNotFound, "Method not found"));
        }

        task::wait([](Web3JsonRpcImpl* self, EndpointsMapping::Handler _handler,
                       Json::Value _request, std::function<void(Json::Value)> _callback,
                       decltype(startT) startT) -> task::Task<void> {
            Json::Value resp;
            try
            {
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
            catch (...)
            {
                buildJsonError(_request, InternalError,
                    boost::current_exception_diagnostic_information(), resp);
            }
            if (c_fileLogLevel == TRACE) [[unlikely]]
            {
                auto endT = utcTime();
                WEB3_LOG(TRACE) << LOG_BADGE("handleRequest") << LOG_DESC("end")
                                << LOG_KV("costMs", endT - startT)
                                << LOG_KV("request", printJson(_request))
                                << LOG_KV("response", printJson(resp));
            }

            _callback(std::move(resp));
        }(this, optHandler.value(), std::move(_request), std::move(_callback), startT));

        return;
    }
    catch (const JsonRpcException& e)
    {
        buildJsonError(_request, e.code(), e.msg(), response);
    }
    catch (bcos::Error const& e)
    {
        buildJsonError(_request, InternalError, e.errorMessage(), response);
    }
    catch (...)
    {
        buildJsonError(
            _request, InternalError, boost::current_exception_diagnostic_information(), response);
    }

    _callback(std::move(response));
}

void Web3JsonRpcImpl::handleRequest(Json::Value _request, Sender _sender)
{
    handleRequest(std::move(_request), [_sender = std::move(_sender)](Json::Value _response) {
        auto respBytes = toBytesResponse(_response);
        _sender(std::move(respBytes));
    });
}

void Web3JsonRpcImpl::handleBatchRequest(Json::Value _request, const Sender& _sender)
{
    auto respJsonValuePtr = std::make_shared<Json::Value>(Json::arrayValue);
    auto requestSize = _request.size();

    auto startT = utcTime();
    if (c_fileLogLevel == TRACE) [[unlikely]]
    {
        WEB3_LOG(TRACE) << LOG_BADGE("handleBatchRequest") << LOG_DESC("begin")
                        << LOG_KV("reqSize", requestSize);
    }

    for (auto& reqItem : _request)
    {
        handleRequest(std::move(reqItem),
            [respJsonValuePtr, requestSize, _sender, startT](Json::Value response) {
                respJsonValuePtr->append(std::move(response));
                if (respJsonValuePtr->size() < requestSize)
                {
                    return;
                }

                auto respBytes = toBytesResponse(*respJsonValuePtr);
                _sender(std::move(respBytes));

                auto endT = utcTime();
                if (c_fileLogLevel == TRACE) [[unlikely]]
                {
                    WEB3_LOG(TRACE) << LOG_BADGE("handleBatchRequest") << LOG_DESC("end")
                                    << LOG_KV("costMs", endT - startT)
                                    << LOG_KV("response", printJson(*respJsonValuePtr));
                }
            });
    }
}

void Web3JsonRpcImpl::onRPCRequest(std::string_view _requestBody, const Sender& _sender)
{
    auto startT = utcTime();
    auto batchRequestSizeLimit = m_batchRequestSizeLimit;

    Json::Value request;
    Json::Value response;
    try
    {
        if (c_fileLogLevel == TRACE) [[unlikely]]
        {
            WEB3_LOG(TRACE) << LOG_BADGE("onRPCRequest") << LOG_DESC("begin")
                            << LOG_KV("request", _requestBody);
        }

        // parse json
        if (Json::Reader jsonReader;
            !jsonReader.parse(_requestBody.begin(), _requestBody.end(), request))
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(InvalidRequest, "Parse json failed"));
        }

        if (request.isObject() && request.isMember("id"))
        {
            response["id"] = request["id"];
        }

        bool isBatchRequest = false;
        if (request.isArray())
        {
            if (request.size() == 0)
            {
                BOOST_THROW_EXCEPTION(
                    JsonRpcException(InvalidRequest, "The request array is empty"));
            }

            if (request.size() > batchRequestSizeLimit)
            {
                BOOST_THROW_EXCEPTION(JsonRpcException(
                    InvalidRequest, "The requested array size exceeds the limit size: " +
                                        std::to_string(batchRequestSizeLimit)));
            }

            // handle batch request, https://www.jsonrpc.org/specification#batch
            if (request.size() > 1)
            {
                isBatchRequest = true;
            }

            // single request
            if (request.size() == 1)
            {
                request = std::move(request[0]);
            }
        }

        if (isBatchRequest)
        {
            // handle batch request
            handleBatchRequest(std::move(request), _sender);
        }
        else
        {
            // handle single request
            handleRequest(std::move(request), _sender);
        }

        return;
    }
    catch (const JsonRpcException& e)
    {
        buildJsonError(request, e.code(), e.msg(), response);
    }
    catch (bcos::Error const& e)
    {
        buildJsonError(request, InternalError, e.errorMessage(), response);
    }
    catch (...)
    {
        std::stringstream msg;
        msg << "Internal error: " << boost::current_exception_diagnostic_information();
        buildJsonError(request, InternalError, msg.str(), response);
    }

    auto respBytes = toBytesResponse(response);

    auto endT = utcTime();
    if (c_fileLogLevel == DEBUG) [[unlikely]]
    {
        WEB3_LOG(DEBUG) << LOG_BADGE("onRPCRequest") << LOG_DESC("end")
                        << LOG_KV("request", _requestBody)
                        << LOG_KV("response", printJson(response))
                        << LOG_KV("costMs", endT - startT);
    }

    _sender(std::move(respBytes));
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
