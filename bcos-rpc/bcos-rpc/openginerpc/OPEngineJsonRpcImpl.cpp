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
#include <cassert>
#include "Common.h"

#include <bcos-rpc/validator/JsonValidator.h>
#include <bcos-task/Wait.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <boost/exception/diagnostic_information.hpp>

namespace bcos::rpc
{
OPEngineJsonRpcImpl::OPEngineJsonRpcImpl(std::string const& _groupId, uint32_t _batchRequestSizeLimit,
    GroupManager::Ptr const& _groupManager, FilterSystem::Ptr _filterSystem,
    bool _syncTransaction)
  : m_batchRequestSizeLimit(_batchRequestSizeLimit),
    m_endpoints(_groupManager->getNodeService(_groupId, "")),
    m_web3Endpoints(
        _groupManager->getNodeService(_groupId, ""), std::move(_filterSystem), _syncTransaction)
{}

void OPEngineJsonRpcImpl::setEngineService(
    std::shared_ptr<bcos::engine::AnyEngineService> _engineService)
{
    m_endpoints.setEngineService(std::move(_engineService));
}

void OPEngineJsonRpcImpl::setJwtVerifier(bcos::rpc::JwtVerifier::Ptr _jwtVerifier)
{
    m_jwtVerifier = std::move(_jwtVerifier);
}

void OPEngineJsonRpcImpl::onRPCRequest(
    std::string_view _requestBody, const bcos::boostssl::http::HttpRequestMeta& _meta, Sender _sender)
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
        
        std::string authorization;
        if (auto it = _meta.headers.find("Authorization"); it != _meta.headers.end())
        {
            authorization = it->second;
        }

        assert(m_jwtVerifier != nullptr && "jwt verifier is not initialized");
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
                task::wait([this, _request = std::move(request), sender = std::move(_sender)]() -> task::Task<void> 
                {
                    auto response = co_await this->handleBatchRequest(_request);
                    sender(toBytesResponse(response));
                }());
                return;
            }
        }

        task::wait([this, _request = std::move(request), sender = std::move(_sender)]() -> task::Task<void> 
        {
            auto response = co_await this->handleRequest(_request);
            sender(toBytesResponse(response));
        }());
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

task::Task<Json::Value> OPEngineJsonRpcImpl::handleRequest(Json::Value _request)
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
            co_return response;
        }

        OP_ENGINE_LOG(TRACE) << LOG_BADGE("EngineRPCRequest") << LOG_DESC("parsed request")
                           << LOG_KV("method", _request["method"].asString())
                           << LOG_KV("request", printJson(_request));

        auto method = _request["method"].asString();
        auto optHandler = m_endpointsMapping.findHandler(method);
        // if the method is engine API, use the engine endpoints to handle the request; otherwise, use the web3 endpoints to handle the request;
        if (optHandler.has_value())
        {
            Json::Value const& params = _request["params"];
            auto result = co_await (m_endpoints.*optHandler.value())(params);
            buildJsonContent(result, response);
            response["id"] = _request["id"];
            co_return response;   
        }

        auto web3Handler = m_web3EndpointsMapping.findHandler(method);
        if (web3Handler.has_value())
        {
            Json::Value const& params = _request["params"];
            //auto result = co_await (m_web3Endpoints.*web3Handler.value())(params);
            //buildJsonContent(result, response);
            response["id"] = _request["id"];
        }
        else 
        {
            buildJsonError(_request, MethodNotFound, "Method not found", response);
        }
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

    co_return response;
}

task::Task<Json::Value> OPEngineJsonRpcImpl::handleBatchRequest(Json::Value _request)
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
        auto result = co_await handleRequest(std::move(req));
        responses->append(std::move(result));
    }
    
    if (c_fileLogLevel == TRACE) [[unlikely]]
    {
                auto const endT = utcTime();
                OP_ENGINE_LOG(TRACE) << LOG_BADGE("handleBatchRequest") << LOG_DESC("end")
                                    << LOG_KV("costMs", endT - startT)
                                    << LOG_KV("response", printJson(*responses));
    }

    co_return *responses;
}


}  // namespace bcos::rpc
