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
    m_web3Endpoints(
        _groupManager->getNodeService(_groupId, ""), std::move(_filterSystem), _syncTransaction)
{}

void OPEngineJsonRpcImpl::setEngineService(
    std::shared_ptr<bcos::engine::AnyEngineService> _engineService)
{
}

void OPEngineJsonRpcImpl::setJwtVerifier(bcos::rpc::JwtVerifier::Ptr _jwtVerifier)
{
    m_jwtVerifier = std::move(_jwtVerifier);
}

void OPEngineJsonRpcImpl::onRPCRequest(
    std::string_view _requestBody, const bcos::boostssl::http::HttpRequestMeta& _meta, Sender _sender)
{
    
}

task::Task<Json::Value> OPEngineJsonRpcImpl::handleRequest(Json::Value _request)
{
    co_return Json::Value();
}

task::Task<Json::Value> OPEngineJsonRpcImpl::handleBatchRequest(Json::Value _request)
{
    co_return Json::Value();
}


}  // namespace bcos::rpc
