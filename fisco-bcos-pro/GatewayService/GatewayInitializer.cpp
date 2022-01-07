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
 * @file GatewayInitializer.cpp
 * @author: yujiechen
 * @date 2021-10-15
 */
#include <bcos-gateway/Gateway.h>
#include <bcos-gateway/GatewayConfig.h>
#include <bcos-gateway/GatewayFactory.h>
#include <bcos-tool/NodeConfig.h>

#include "GatewayInitializer.h"

using namespace bcostars;

void GatewayInitializer::init(std::string const& _configPath)
{
    GATEWAYSERVICE_LOG(INFO) << LOG_DESC("initGateWayConfig") << LOG_KV("configPath", _configPath);

    GATEWAYSERVICE_LOG(INFO) << LOG_DESC("load nodeConfig");
    auto nodeConfig = std::make_shared<bcos::tool::NodeConfig>();
    nodeConfig->loadConfig(_configPath);

    boost::property_tree::ptree pt;
    boost::property_tree::read_ini(_configPath, pt);
    nodeConfig->loadServiceConfig(pt);
    GATEWAYSERVICE_LOG(INFO) << LOG_DESC("load nodeConfig success");

    GATEWAYSERVICE_LOG(INFO) << LOG_DESC("buildGateWay")
                             << LOG_KV("certPath", m_gatewayConfig->certPath())
                             << LOG_KV("nodePath", m_gatewayConfig->nodePath());

    bcos::gateway::GatewayFactory factory(nodeConfig->chainId(), nodeConfig->rpcServiceName());
    auto gateway = factory.buildGateway(m_gatewayConfig, false);

    m_gateway = gateway;
    GATEWAYSERVICE_LOG(INFO) << LOG_DESC("buildGateway success");
}

void GatewayInitializer::start()
{
    if (m_running)
    {
        GATEWAYSERVICE_LOG(INFO) << LOG_DESC("the gateway has already been started");
        return;
    }
    m_running = true;
    // start the gateway
    GATEWAYSERVICE_LOG(INFO) << LOG_DESC("start the gateway");
    m_gateway->start();
    GATEWAYSERVICE_LOG(INFO) << LOG_DESC("start the gateway success");
}

void GatewayInitializer::stop()
{
    if (!m_running)
    {
        GATEWAYSERVICE_LOG(WARNING) << LOG_DESC("The GatewayService has already been stopped");
        return;
    }
    m_running = false;
    GATEWAYSERVICE_LOG(INFO) << LOG_DESC("Stop the GatewayService");
    if (m_gateway)
    {
        m_gateway->stop();
    }
    TLOGINFO(LOG_DESC("[GATEWAYSERVICE] Stop the GatewayService success") << std::endl);
}