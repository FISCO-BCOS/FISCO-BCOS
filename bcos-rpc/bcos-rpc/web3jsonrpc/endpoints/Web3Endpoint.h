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
#include "EndpointInterface.h"
#include "bcos-rpc/groupmgr/GroupManager.h"
#include <bcos-rpc/jsonrpc/JsonRpcInterface.h>
#include <json/json.h>
#include <tbb/concurrent_hash_map.h>
#include <unordered_map>

namespace bcos::rpc
{
class Web3Endpoint : public EndpointInterface
{
public:
    explicit Web3Endpoint(bcos::rpc::GroupManager::Ptr groupManager);
    void initMethod();
    MethodMap&& exportMethods() override { return std::move(m_methods); }
    void clientVersion(RespFunc);
    void sha3(RespFunc);

protected:
    void clientVersionInterface(Json::Value const&, RespFunc func)
    {
        clientVersion(std::move(func));
    }
    void sha3Interface(Json::Value const&, RespFunc func) { sha3(std::move(func)); };

private:
    bcos::rpc::GroupManager::Ptr m_groupManager;
};

}  // namespace bcos::rpc