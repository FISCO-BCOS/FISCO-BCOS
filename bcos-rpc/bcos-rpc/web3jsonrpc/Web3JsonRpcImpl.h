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
 * @file Web3JsonRpcImpl.h
 * @author: kyonGuo
 * @date 2024/3/21
 */
#pragma once
#include <boost/beast/http/status.hpp>
#include "bcos-rpc/groupmgr/GroupManager.h"
#include <bcos-rpc/jwtAuth/JwtVerifier.h>
#include "bcos-rpc/web3jsonrpc/Web3Subscribe.h"
#include "bcos-rpc/web3jsonrpc/endpoints/Endpoints.h"
#include "bcos-rpc/web3jsonrpc/endpoints/EndpointsMapping.h"
#include <bcos-task/Task.h>
#include <json/json.h>
namespace bcos::rpc
{
class Web3JsonRpcImpl : public std::enable_shared_from_this<Web3JsonRpcImpl>
{
public:
    using Ptr = std::shared_ptr<Web3JsonRpcImpl>;
    using WeakPtr = std::weak_ptr<Web3JsonRpcImpl>;
    using Sender = std::function<void(bcos::bytes, boost::beast::http::status)>;
    Web3JsonRpcImpl(std::string const& _groupId, uint32_t _batchRequestSizeLimit,
        bcos::rpc::GroupManager::Ptr const& _groupManager, FilterSystem::Ptr filterSystem,
        bool syncTransaction, bool _enableOPEngine = false);
    ~Web3JsonRpcImpl() = default;

    void setJwtVerifier(bcos::rpc::JwtVerifier::Ptr _jwtVerifier) { m_jwtVerifier = std::move(_jwtVerifier);}

    void onRPCRequest(std::string_view _requestBody, const Sender& _sender);

    void onRPCRequest(std::string_view _requestBody,
        std::shared_ptr<boostssl::ws::WsSession> _session, const Sender& _sender);

    void onRPCRequest(const bcos::boostssl::http::HttpRequest& _request, const Sender& _sender);

    void setWeb3Subscribe(Web3Subscribe::Ptr _web3Subscribe)
    {
        m_web3Subscribe = std::move(_web3Subscribe);
    }
    Web3Subscribe::Ptr web3Subscribe() const { return m_web3Subscribe; }

    Endpoints& endpoints() { return m_endpoints; }

private:
    task::Task<Json::Value> handleRequest(Json::Value _request,
        std::shared_ptr<boostssl::ws::WsSession> _session = nullptr);
    task::Task<Json::Value> handleBatchRequest(Json::Value _request,
        std::shared_ptr<boostssl::ws::WsSession> _session);
    Json::Value handleSubscribeRequest(Json::Value _request, std::string _method,
        std::shared_ptr<boostssl::ws::WsSession> _session);

    Endpoints m_endpoints;
    EndpointsMapping m_endpointsMapping;
    bcos::rpc::JwtVerifier::Ptr m_jwtVerifier;
    // Note: only use in one group
    Web3Subscribe::Ptr m_web3Subscribe;
    uint32_t m_batchRequestSizeLimit;
};
}  // namespace bcos::rpc
