/**
 * Copyright (C) 2026 FISCO BCOS.
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @file MinerEndpoint.h
 * @brief OP Stack miner namespace: miner_setMaxDASize DA-throttling handshake.
 */

#pragma once

#include <bcos-rpc/groupmgr/GroupManager.h>
#include <json/json.h>

namespace bcos::rpc
{

/// OP Stack miner namespace (DA throttling handshake from the batcher).
class MinerEndpoint
{
public:
    explicit MinerEndpoint(NodeService::Ptr nodeService) : m_nodeService(std::move(nodeService)) {}
    virtual ~MinerEndpoint() = default;

    task::Task<void> setMaxDASize(const Json::Value&, Json::Value&);

private:
    NodeService::Ptr m_nodeService;
};

}  // namespace bcos::rpc
