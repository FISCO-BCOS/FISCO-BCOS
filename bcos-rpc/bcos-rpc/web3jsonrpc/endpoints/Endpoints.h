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
 * @file Endpoints.h
 * @author: kyonGuo
 * @date 2024/3/28
 */

#pragma once
#include "EthEndpoint.h"
#include "NetEndpoint.h"
#include "Web3Endpoint.h"

#include <bcos-rpc/groupmgr/NodeService.h>

namespace bcos::rpc
{
class Endpoints : protected EthEndpoint, NetEndpoint, Web3Endpoint
{
public:
    explicit Endpoints(NodeService::Ptr _nodeService, FilterSystem::Ptr filterSystem)
      : EthEndpoint(_nodeService, filterSystem),
        NetEndpoint(_nodeService),
        Web3Endpoint(_nodeService)
    {}

    ~Endpoints() override = default;
    Endpoints(const Endpoints&) = delete;
    Endpoints& operator=(const Endpoints&) = delete;
    friend class EndpointsMapping;
};
}  // namespace bcos::rpc