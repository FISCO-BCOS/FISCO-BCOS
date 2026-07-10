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
#include "bcos-rpc/validator/JsonValidator.h"
#include "bcos-task/Wait.h"
#include "utils/util.h"

using namespace bcos;
using namespace bcos::rpc;

bcos::rpc::Web3JsonRpcImpl::Web3JsonRpcImpl(std::string const& _groupId, uint32_t _batchRequestSizeLimit,
    bcos::rpc::GroupManager::Ptr const& _groupManager, FilterSystem::Ptr filterSystem, 
    bool syncTransaction, bool _enableOPEngine)
  : m_endpoints(
        _groupManager->getNodeService(_groupId, ""), std::move(filterSystem), syncTransaction),
    m_endpointsMapping(_enableOPEngine),
    m_batchRequestSizeLimit(_batchRequestSizeLimit)
{
    RPC_LOG(INFO) << LOG_KV("[NEWOBJ][Web3JsonRpcImpl]", this);
}

task::Task<Json::Value> Web3JsonRpcImpl::handleRequest(
    Json::Value _request, std::shared_ptr<boostssl::ws::WsSession> _session)
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

        if (m_web3Subscribe && m_web3Subscribe->isSubscribeRequest(method))
        {
            auto result = handleSubscribeRequest(std::move(_request), std::move(method), std::move(_session));
            co_return result;
        }

        auto optHandler = m_endpointsMapping.findHandler(method);
        if (!optHandler.has_value())
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(MethodNotFound, "Method not found"));
        }

        Json::Value const& params = _request["params"];
        Json::Value result;
        co_await (m_endpoints.*optHandler.value())(params, result);
        buildJsonContent(result, response);

        if (c_fileLogLevel == TRACE) [[unlikely]]
        {
            auto endT = utcTime();
            WEB3_LOG(TRACE) << LOG_BADGE("handleRequest") << LOG_DESC("end")
                            << LOG_KV("costMs", endT - startT)
                            << LOG_KV("request", printJson(_request))
                            << LOG_KV("response", printJson(response));
        }

        co_return response;
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

    co_return response;
}

task::Task<Json::Value> Web3JsonRpcImpl::handleBatchRequest(
    Json::Value _request, std::shared_ptr<boostssl::ws::WsSession> _session)
{
    auto responses = Json::Value(Json::arrayValue);
    auto requestSize = _request.size();

    auto startT = utcTime();
    if (c_fileLogLevel == TRACE) [[unlikely]]
    {
        WEB3_LOG(TRACE) << LOG_BADGE("handleBatchRequest") << LOG_DESC("begin")
                        << LOG_KV("reqSize", requestSize);
    }

    for (auto& reqItem : _request)
    {
        auto result = co_await handleRequest(std::move(reqItem), _session);
        responses.append(std::move(result));
    }

    if (c_fileLogLevel == TRACE) [[unlikely]]
    {
        auto endT = utcTime();
        WEB3_LOG(TRACE) << LOG_BADGE("handleBatchRequest") << LOG_DESC("end")
                        << LOG_KV("costMs", endT - startT)
                        << LOG_KV("response", printJson(responses));
    }

    co_return responses;
}

Json::Value Web3JsonRpcImpl::handleSubscribeRequest(Json::Value _request, std::string _method,
    std::shared_ptr<boostssl::ws::WsSession> _session)
{
    if (!_session)
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(InvalidRequest, "Subscribe request only support websocket protocol"));
    }

    Json::Value response;
    if (_request.isObject() && _request.isMember("id"))
    {
        response["id"] = _request["id"];
    }

    if (_method == Web3Subscribe::SUBSCRIBE_METHOD)
    {
        Json::Value result = m_web3Subscribe->onSubscribeRequest(std::move(_request), _session);
        buildJsonContent(result, response);
    }
    else if (_method == Web3Subscribe::UNSUBSCRIBE_METHOD)
    {
        Json::Value result = m_web3Subscribe->onUnsubscribeRequest(std::move(_request), _session);
        buildJsonContent(result, response);
    }
    else
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidRequest, "Invalid subscribe method"));
    }

    return response;
}

void Web3JsonRpcImpl::onRPCRequest(std::string_view _requestBody, const Sender& _sender)
{
    onRPCRequest(_requestBody, nullptr, _sender);
}

void Web3JsonRpcImpl::onRPCRequest(const bcos::boostssl::http::HttpRequest& _request, const Sender& _sender)
{
    assert(m_jwtVerifier && "m_jwtVerifier is not set");

    std::string authorization;
    if (auto it = _request.find(boost::beast::http::field::authorization); it != _request.end())
    {
        authorization = std::string(it->value());
    }
     auto verifyResult = m_jwtVerifier->verify(authorization);
    if (!verifyResult)
    {
        Json::Value request;
        Json::Value response;
        buildJsonError(request, bcos::rpc::toJsonRpcJwtErrorCode(verifyResult.error),
            "JWT authentication failed: " + verifyResult.errorMessage, response);
        _sender(toBytesResponse(response));
        return;
    }

    onRPCRequest(_request.body(), nullptr, _sender);
}

void Web3JsonRpcImpl::onRPCRequest(std::string_view _requestBody,
    std::shared_ptr<boostssl::ws::WsSession> _session, const Sender& _sender)
{
    auto startT = utcTime();
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

        if (request.isArray())
        {
            if (request.size() == 0)
            {
                BOOST_THROW_EXCEPTION(
                    JsonRpcException(InvalidRequest, "The request array is empty"));
            }

            if (request.size() > m_batchRequestSizeLimit)
            {
                BOOST_THROW_EXCEPTION(JsonRpcException(
                    InvalidRequest, "The requested array size exceeds the limit size: " +
                                        std::to_string(m_batchRequestSizeLimit)));
            }

            if (request.size() > 1)
            {
                task::wait([this, request = std::move(request), session = std::move(_session),
                               sender = _sender]() mutable -> task::Task<void> {
                    auto result = co_await this->handleBatchRequest(std::move(request), session);
                    sender(toBytesResponse(result));
                }());
                return;
            }

            // single request in array
            request = std::move(request[0]);
        }

        task::wait([this, request = std::move(request), session = std::move(_session),
                       sender = _sender]() mutable -> task::Task<void> {
            auto result = co_await this->handleRequest(std::move(request), session);
            sender(toBytesResponse(result));
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
