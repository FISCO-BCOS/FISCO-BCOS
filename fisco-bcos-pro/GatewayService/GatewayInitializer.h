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
 * @brief initializer for the GatewayService
 * @file GatewayInitializer.h
 * @author: yujiechen
 * @date 2021-10-15
 */
#pragma once
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework/interfaces/crypto/KeyInterface.h>
#include <bcos-framework/interfaces/gateway/GatewayInterface.h>
#include <bcos-framework/interfaces/multigroup/ChainNodeInfoFactory.h>
#include <bcos-framework/interfaces/multigroup/GroupInfoFactory.h>
#include <bcos-gateway/GatewayConfig.h>

namespace bcostars
{
class GatewayInitializer
{
public:
    using Ptr = std::shared_ptr<GatewayInitializer>;
    GatewayInitializer(
        std::string const& _configPath, bcos::gateway::GatewayConfig::Ptr _gatewayConfig)
      : m_gatewayConfig(_gatewayConfig),
        m_keyFactory(std::make_shared<bcos::crypto::KeyFactoryImpl>()),
        m_groupInfoFactory(std::make_shared<bcos::group::GroupInfoFactory>()),
        m_chainNodeInfoFactory(std::make_shared<bcos::group::ChainNodeInfoFactory>())
    {
        init(_configPath);
    }

    virtual ~GatewayInitializer() { stop(); }


    virtual void start();
    virtual void stop();

    bcos::gateway::GatewayInterface::Ptr gateway() { return m_gateway; }
    bcos::group::ChainNodeInfoFactory::Ptr chainNodeInfoFactory() { return m_chainNodeInfoFactory; }
    bcos::group::GroupInfoFactory::Ptr groupInfoFactory() { return m_groupInfoFactory; }

    bcos::crypto::KeyFactory::Ptr keyFactory() { return m_keyFactory; }

protected:
    virtual void init(std::string const& _configPath);

private:
    bcos::gateway::GatewayConfig::Ptr m_gatewayConfig;
    bcos::crypto::KeyFactory::Ptr m_keyFactory;
    bcos::group::GroupInfoFactory::Ptr m_groupInfoFactory;
    bcos::group::ChainNodeInfoFactory::Ptr m_chainNodeInfoFactory;
    bcos::gateway::GatewayInterface::Ptr m_gateway;
    std::atomic_bool m_running = {false};
};
}  // namespace bcostars
