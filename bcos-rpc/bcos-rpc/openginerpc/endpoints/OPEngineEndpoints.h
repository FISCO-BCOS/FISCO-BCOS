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

#include <bcos-framework/engine/AnyEngineService.h>
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


    void setEngineService(
        std::shared_ptr<bcos::engine::AnyEngineService> _engineService)
    {
        m_engineService = std::move(_engineService);
    }

private:
    NodeService::Ptr m_nodeService;
    std::shared_ptr<bcos::engine::AnyEngineService> m_engineService;
};
}  // namespace bcos::rpc
