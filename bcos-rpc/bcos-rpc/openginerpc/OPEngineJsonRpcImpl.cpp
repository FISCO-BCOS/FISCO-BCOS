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
 * @file OPEngineJsonRpcImpl.cpp
 * @date 2026/5/20
 */

#include "OPEngineJsonRpcImpl.h"

#include "Common.h"

#include <bcos-rpc/validator/JsonValidator.h>
#include <bcos-task/Wait.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <boost/exception/diagnostic_information.hpp>

namespace bcos::rpc
{
OPEngineJsonRpcImpl::OPEngineJsonRpcImpl(std::string _groupId, uint32_t _batchRequestSizeLimit,
    GroupManager::Ptr _groupManager, bcos::gateway::GatewayInterface::Ptr _gatewayInterface,
    std::shared_ptr<boostssl::ws::WsService> _wsService, FilterSystem::Ptr _filterSystem,
    bool _syncTransaction)
  : m_groupId(std::move(_groupId)),
    m_batchRequestSizeLimit(_batchRequestSizeLimit),
    m_groupManager(std::move(_groupManager)),
    m_gatewayInterface(std::move(_gatewayInterface)),
    m_wsService(std::move(_wsService)),
    m_endpoints(m_groupManager->getNodeService(m_groupId, "")),
    m_web3Endpoints(
        m_groupManager->getNodeService(m_groupId, ""), std::move(_filterSystem), _syncTransaction)
{}

void OPEngineJsonRpcImpl::setEngineService(
    std::shared_ptr<bcos::engine::AnyEngineService> _engineService)
{
    m_engineService = std::move(_engineService);
    m_endpoints.setEngineService(m_engineService);
}

void OPEngineJsonRpcImpl::setJwtVerifier(bcos::rpc::JwtVerifier::Ptr _jwtVerifier)
{
    m_jwtVerifier = std::move(_jwtVerifier);
}

void OPEngineJsonRpcImpl::onRPCRequest(
    std::string_view _requestBody, const bcos::boostssl::http::HttpRequestMeta& _meta, const Sender& _sender)
{
    auto const startT = utcTime();
    Json::Value request;
    Json::Value response;
    try
    {
        if (c_fileLogLevel == TRACE) [[unlikely]]
        {
            OP_ENGINE_LOG(TRACE) << LOG_BADGE("onRPCRequest") << LOG_DESC("begin")
                                << LOG_KV("request", _requestBody);
        }

        if (!m_jwtVerifier)
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(InternalError, "jwt verifier is not initialized"));
        }

        std::string authorization;
        if (auto it = _meta.headers.find("Authorization"); it != _meta.headers.end())
        {
            authorization = it->second;
        }

        auto verifyResult = m_jwtVerifier->verify(authorization);
        if (!verifyResult)
        {
            buildJsonError(request, bcos::rpc::toJsonRpcJwtErrorCode(verifyResult.error),
                "JWT authentication failed: " + verifyResult.errorMessage, response);
            _sender(toBytesResponse(response));
            return;
        }

        Json::Reader reader;
        if (!reader.parse(_requestBody.begin(), _requestBody.end(), request))
        {
            buildJsonError(request, InvalidRequest, "Parse json failed", response);
            _sender(toBytesResponse(response));
            return;
        }

        if (request.isArray())
        {
            if (request.size() == 0)
            {
                buildJsonError(request, InvalidRequest, "The request array is empty", response);
                _sender(toBytesResponse(response));
                return;
            }
            if (request.size() > m_batchRequestSizeLimit)
            {
                buildJsonError(request, InvalidRequest,
                    "The requested array size exceeds the limit size: " +
                        std::to_string(m_batchRequestSizeLimit),
                    response);
                _sender(toBytesResponse(response));
                return;
            }

            if (request.size() == 1)
            {
                request = std::move(request[0]);
            }
            else
            {
                handleBatchRequest(std::move(request), _sender);
                return;
            }
        }

        handleRequest(std::move(request), [_sender](Json::Value _response) {
            _sender(toBytesResponse(_response));
        });
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
        buildJsonError(request, InternalError, boost::current_exception_diagnostic_information(),
            response);
    }

    if (c_fileLogLevel == DEBUG) [[unlikely]]
    {
        auto const endT = utcTime();
        OP_ENGINE_LOG(DEBUG) << LOG_BADGE("onRPCRequest") << LOG_DESC("end")
                            << LOG_KV("request", _requestBody)
                            << LOG_KV("response", printJson(response))
                            << LOG_KV("costMs", endT - startT);
    }

    _sender(toBytesResponse(response));
}

void OPEngineJsonRpcImpl::handleRequest(
    Json::Value _request, const std::function<void(Json::Value)>& _callback)
{
    Json::Value response;
    try
    {
        auto const startT = utcTime();
        if (c_fileLogLevel == TRACE) [[unlikely]]
        {
            OP_ENGINE_LOG(TRACE) << LOG_BADGE("handleRequest") << LOG_DESC("begin")
                                << LOG_KV("request", printJson(_request));
        }

        if (auto result = JsonValidator::validate(_request); !std::get<bool>(result))
        {
            buildJsonError(_request, InvalidRequest, std::get<std::string>(result), response);
            _callback(std::move(response));
            return;
        }

        OP_ENGINE_LOG(TRACE) << LOG_BADGE("EngineRPCRequest") << LOG_DESC("parsed request")
                           << LOG_KV("method", _request["method"].asString())
                           << LOG_KV("request", printJson(_request));

        auto method = _request["method"].asString();
        auto optHandler = m_endpointsMapping.findHandler(method);
        // if the method is engine API, use the engine endpoints to handle the request; otherwise, use the web3 endpoints to handle the request;
        if (optHandler.has_value())
        {
            task::wait([](OPEngineJsonRpcImpl* self, OPEngineEndpointsMapping::Handler _handler,
                           Json::Value _request, std::function<void(Json::Value)> _callback,
                           decltype(startT) startT) -> task::Task<void> {
                Json::Value result;
                Json::Value resp;
                try
                {
                    Json::Value const& params = _request["params"];
                    co_await (self->m_endpoints.*_handler)(params, result);
                    buildJsonContent(result, resp);
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
                    auto const endT = utcTime();
                    OP_ENGINE_LOG(TRACE) << LOG_BADGE("handleRequest") << LOG_DESC("end")
                                        << LOG_KV("costMs", endT - startT)
                                        << LOG_KV("request", printJson(_request))
                                        << LOG_KV("response", printJson(resp));
                }

                _callback(std::move(resp));
            }(this, optHandler.value(), std::move(_request), _callback, startT));
            return;
        }

        auto web3Handler = m_web3EndpointsMapping.findHandler(method);
        if (!web3Handler.has_value())
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(MethodNotFound, "Method not found"));
        }

        task::wait([](OPEngineJsonRpcImpl* self, EndpointsMapping::Handler _handler,
                       Json::Value _request, std::function<void(Json::Value)> _callback,
                       decltype(startT) startT) -> task::Task<void> {
            Json::Value resp;
            try
            {
                Json::Value const& params = _request["params"];
                co_await (self->m_web3Endpoints.*_handler)(params, resp);
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
                auto const endT = utcTime();
                OP_ENGINE_LOG(TRACE) << LOG_BADGE("handleRequest") << LOG_DESC("end")
                                    << LOG_KV("costMs", endT - startT)
                                    << LOG_KV("request", printJson(_request))
                                    << LOG_KV("response", printJson(resp));
            }

            _callback(std::move(resp));
        }(this, web3Handler.value(), std::move(_request), _callback, startT));
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
        buildJsonError(_request, InternalError, boost::current_exception_diagnostic_information(),
            response);
    }

    _callback(std::move(response));
}

void OPEngineJsonRpcImpl::handleBatchRequest(Json::Value _request, const Sender& _sender)
{
    auto responses = std::make_shared<Json::Value>(Json::arrayValue);
    auto const requestSize = _request.size();
    auto const startT = utcTime();
    if (c_fileLogLevel == TRACE) [[unlikely]]
    {
        OP_ENGINE_LOG(TRACE) << LOG_BADGE("handleBatchRequest") << LOG_DESC("begin")
                            << LOG_KV("reqSize", requestSize);
    }

    for (auto& req : _request)
    {
        handleRequest(std::move(req), [responses, requestSize, _sender, startT](Json::Value _response) {
            responses->append(std::move(_response));
            if (responses->size() < requestSize)
            {
                return;
            }

            if (c_fileLogLevel == TRACE) [[unlikely]]
            {
                auto const endT = utcTime();
                OP_ENGINE_LOG(TRACE) << LOG_BADGE("handleBatchRequest") << LOG_DESC("end")
                                    << LOG_KV("costMs", endT - startT)
                                    << LOG_KV("response", printJson(*responses));
            }

            _sender(toBytesResponse(*responses));
        });
    }
}
}  // namespace bcos::rpc
