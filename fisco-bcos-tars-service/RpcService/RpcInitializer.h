/**
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
 * @brief initializer for the RpcService
 * @file RpcInitializer.h
 * @author: yujiechen
 * @date 2021-10-15
 */
#pragma once
#include <bcos-crypto/interfaces/crypto/KeyFactory.h>
#include <bcos-framework/interfaces/multigroup/GroupInfoFactory.h>
#include <bcos-framework/interfaces/rpc/RPCInterface.h>
#include <bcos-leader-election/src/LeaderEntryPoint.h>
#include <bcos-tool/NodeConfig.h>
#include <memory>
#include <utility>

namespace bcos::rpc
{
class RpcFactory;
class Rpc;
}  // namespace bcos::rpc
namespace bcostars
{
class RpcInitializer
{
public:
    using Ptr = std::shared_ptr<RpcInitializer>;
    RpcInitializer(std::string const& _configDir, bcos::tool::NodeConfig::Ptr _nodeConfig)
      : m_nodeConfig(_nodeConfig),
        m_groupInfoFactory(std::make_shared<bcos::group::GroupInfoFactory>()),
        m_chainNodeInfoFactory(std::make_shared<bcos::group::ChainNodeInfoFactory>())
    {
        init(_configDir);
    }
    virtual ~RpcInitializer() { stop(); }


    virtual void start();
    virtual void stop();

    void setClientID(std::string const& _clientID);
    bcos::rpc::RPCInterface::Ptr rpc();
    bcos::crypto::KeyFactory::Ptr keyFactory() { return m_keyFactory; }
    bcos::group::GroupInfoFactory::Ptr groupInfoFactory() { return m_groupInfoFactory; }
    bcos::group::ChainNodeInfoFactory::Ptr chainNodeInfoFactory() { return m_chainNodeInfoFactory; }

protected:
    virtual void init(std::string const& _configPath);
    std::shared_ptr<bcos::rpc::RpcFactory> initRpcFactory(bcos::tool::NodeConfig::Ptr _nodeConfig);

private:
    std::shared_ptr<bcos::rpc::Rpc> m_rpc;
    bcos::tool::NodeConfig::Ptr m_nodeConfig;
    bcos::crypto::KeyFactory::Ptr m_keyFactory;
    bcos::group::GroupInfoFactory::Ptr m_groupInfoFactory;
    bcos::group::ChainNodeInfoFactory::Ptr m_chainNodeInfoFactory;
    std::atomic_bool m_running = {false};

    bcos::election::LeaderEntryPointInterface::Ptr m_leaderEntryPoint;
};
}  // namespace bcostars