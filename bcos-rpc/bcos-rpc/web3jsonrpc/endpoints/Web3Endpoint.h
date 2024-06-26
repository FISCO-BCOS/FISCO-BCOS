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
 * @file Web3Endpoint.h
 * @author: kyonGuo
 * @date 2024/3/21
 */

#pragma once
#include "bcos-rpc/groupmgr/GroupManager.h"
#include <bcos-rpc/jsonrpc/JsonRpcInterface.h>
#include <json/json.h>
#include <tbb/concurrent_hash_map.h>
#include <unordered_map>

namespace bcos::rpc
{
class Web3Endpoint
{
public:
    explicit Web3Endpoint(NodeService::Ptr nodeService) : m_nodeService(std::move(nodeService)) {}
    virtual ~Web3Endpoint() = default;

protected:
    task::Task<void> clientVersion(const Json::Value&, Json::Value&);
    task::Task<void> sha3(const Json::Value&, Json::Value&);

private:
    NodeService::Ptr m_nodeService;
};

}  // namespace bcos::rpc