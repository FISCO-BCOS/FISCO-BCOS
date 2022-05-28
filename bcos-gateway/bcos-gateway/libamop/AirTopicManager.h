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
 * @file AirTopicManager.h
 * @author: octopus
 * @date 2021-06-18
 */
#pragma once
#include <bcos-framework//rpc/RPCInterface.h>
#include <bcos-gateway/libamop/TopicManager.h>
namespace bcos
{
namespace amop
{
class LocalTopicManager : public TopicManager
{
public:
    using Ptr = std::shared_ptr<LocalTopicManager>;
    LocalTopicManager(std::string const& _rpcServiceName, bcos::gateway::P2PInterface::Ptr _network)
      : TopicManager(_rpcServiceName, _network)
    {}
    ~LocalTopicManager() override {}

    void setLocalClient(bcos::rpc::RPCInterface::Ptr _rpc) { m_rpc = _rpc; }
    bcos::rpc::RPCInterface::Ptr createAndGetServiceByClient(std::string const&) override
    {
        return m_rpc;
    }
    void start() override {}
    void stop() override {}

private:
    bcos::rpc::RPCInterface::Ptr m_rpc;
};
}  // namespace amop
}  // namespace bcos