/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @file NetEndpoint.h
 * @author: kyonGuo
 * @date 2024/3/21
 */

#pragma once
#include "bcos-rpc/groupmgr/GroupManager.h"
#include <bcos-rpc/jsonrpc/JsonRpcInterface.h>
#include <json/json.h>
#include <tbb/concurrent_hash_map.h>
#include <boost/core/ignore_unused.hpp>
#include <unordered_map>

namespace bcos::rpc
{
class NetEndpoint
{
public:
    explicit NetEndpoint(NodeService::Ptr nodeService) : m_nodeService(std::move(nodeService)) {}
    virtual ~NetEndpoint() = default;

protected:
    task::Task<void> verison(const Json::Value&, Json::Value&);
    task::Task<void> listening(const Json::Value&, Json::Value&);
    task::Task<void> peerCount(const Json::Value&, Json::Value&);

private:
    NodeService::Ptr m_nodeService;
};

}  // namespace bcos::rpc