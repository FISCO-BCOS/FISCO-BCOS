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
 * @file EngineEndpoint.h
 * @author: GitHub Copilot
 * @date 2026/5/7
 */

#pragma once

#include "bcos-rpc/groupmgr/NodeService.h"
#include "bcos-task/Task.h"
#include <json/json.h>

namespace bcos::rpc
{
class EngineEndpoint
{
public:
    explicit EngineEndpoint(NodeService::Ptr nodeService);
    ~EngineEndpoint() = default;

    task::Task<void> exchangeCapabilities(const Json::Value&, Json::Value&);
    task::Task<void> forkchoiceUpdatedV1(const Json::Value&, Json::Value&);
    task::Task<void> forkchoiceUpdatedV2(const Json::Value&, Json::Value&);
    task::Task<void> forkchoiceUpdatedV3(const Json::Value&, Json::Value&);
    task::Task<void> forkchoiceUpdatedV4(const Json::Value&, Json::Value&);
    task::Task<void> getPayloadV1(const Json::Value&, Json::Value&);
    task::Task<void> getPayloadV2(const Json::Value&, Json::Value&);
    task::Task<void> getPayloadV3(const Json::Value&, Json::Value&);
    task::Task<void> getPayloadV4(const Json::Value&, Json::Value&);
    task::Task<void> getPayloadV5(const Json::Value&, Json::Value&);
    task::Task<void> newPayloadV1(const Json::Value&, Json::Value&);
    task::Task<void> newPayloadV2(const Json::Value&, Json::Value&);
    task::Task<void> newPayloadV3(const Json::Value&, Json::Value&);
    task::Task<void> newPayloadV4(const Json::Value&, Json::Value&);

private:
    task::Task<void> handleForkchoiceUpdated(
        engine::ApiVersion version, const Json::Value&, Json::Value&);
    task::Task<void> handleGetPayload(engine::ApiVersion version, const Json::Value&, Json::Value&);
    task::Task<void> handleNewPayload(engine::ApiVersion version, const Json::Value&, Json::Value&);

    /// Build a JSON-RPC error response when the engine service is unavailable.
    void buildEngineNotAvailableError(Json::Value& response) const;

    /// Build the -38005 answer for a method version this node does not implement.
    void buildUnimplementedVersionError(std::string_view method, Json::Value& response) const;

    NodeService::Ptr m_nodeService;
};
}  // namespace bcos::rpc
