/*
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @file FrontServiceFactory.h
 * @author: octopus
 * @date 2021-05-20
 */

#pragma once

#include <bcos-framework/front/FrontServiceInterface.h>
#include <bcos-framework/gateway/GatewayInterface.h>
#include <bcos-front/FrontService.h>

namespace bcos
{
namespace front
{
class FrontServiceFactory
{
public:
    using Ptr = std::shared_ptr<FrontServiceFactory>;

public:
    FrontService::Ptr buildFrontService(
        const std::string& _groupID, const bcos::crypto::NodeIDPtr _nodeID);

public:
    void setGatewayInterface(bcos::gateway::GatewayInterface::Ptr _gatewayInterface)
    {
        m_gatewayInterface = _gatewayInterface;
    }

    std::shared_ptr<bcos::ThreadPool> threadPool() { return m_threadPool; }
    void setThreadPool(std::shared_ptr<bcos::ThreadPool> _threadPool)
    {
        m_threadPool = _threadPool;
    }

private:
    // gatewayInterface
    bcos::gateway::GatewayInterface::Ptr m_gatewayInterface;
    // threadpool
    std::shared_ptr<bcos::ThreadPool> m_threadPool;
};

}  // namespace front
}  // namespace bcos