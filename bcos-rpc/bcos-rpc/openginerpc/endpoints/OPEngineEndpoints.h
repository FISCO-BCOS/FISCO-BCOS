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
 * @file OPEngineEndpoints.h
 * @date 2026/5/20
 */

#pragma once

#include "../helper/ForkchoiceUpdated.h"
#include "../helper/GetPayload.h"
#include "../helper/NewPayload.h"
#include <bcos-framework/engine/AnyEngineService.h>
#include <bcos-rpc/openginerpc/Common.h>
#include "bcos-rpc/groupmgr/NodeService.h"
#include "bcos-task/Task.h"
#include <cstdint>
#include <json/json.h>
#include <string>

namespace bcos::rpc
{
class OPEngineEndpoints
{
public:
    explicit OPEngineEndpoints(NodeService::Ptr _nodeService)
      : m_nodeService(std::move(_nodeService)), m_engineService(m_nodeService->engineService())
    {}
    virtual ~OPEngineEndpoints() = default;

    task::Task<void> exchangeCapabilities(const Json::Value&, Json::Value&);
    task::Task<void> forkchoiceUpdatedV1(const Json::Value&, Json::Value&);
    task::Task<void> forkchoiceUpdatedV2(const Json::Value&, Json::Value&);
    task::Task<void> forkchoiceUpdatedV3(const Json::Value&, Json::Value&);
    task::Task<void> forkchoiceUpdatedV4(const Json::Value&, Json::Value&);
    task::Task<void> getPayloadV1(const Json::Value&, Json::Value&);
    task::Task<void> getPayloadV2(const Json::Value&, Json::Value&);
    task::Task<void> getPayloadV3(const Json::Value&, Json::Value&);
    task::Task<void> getPayloadV4(const Json::Value&, Json::Value&);
    task::Task<void> newPayloadV1(const Json::Value&, Json::Value&);
    task::Task<void> newPayloadV2(const Json::Value&, Json::Value&);
    task::Task<void> newPayloadV3(const Json::Value&, Json::Value&);
    task::Task<void> newPayloadV4(const Json::Value&, Json::Value&);

    void setEngineService(
        std::shared_ptr<bcos::engine::AnyEngineService> _engineService)
    {
        m_engineService = std::move(_engineService);
    }

private:
    task::Task<void> handleForkchoiceUpdated(
        engine::ApiVersion version, const Json::Value& _request, Json::Value& _response);
    task::Task<void> handleGetPayload(
        engine::ApiVersion version, const Json::Value& _request, Json::Value& _response);
    task::Task<void> handleNewPayload(
        engine::ApiVersion version, const Json::Value& _request, Json::Value& _response);
    static std::uint32_t toServiceVersion(engine::ApiVersion version);

    NodeService::Ptr m_nodeService;
    std::shared_ptr<bcos::engine::AnyEngineService> m_engineService;
};
}  // namespace bcos::rpc
