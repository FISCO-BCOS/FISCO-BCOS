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
 * @brief Application for the NodeService
 * @file NodeServiceApp.h
 * @author: yujiechen
 * @date 2021-10-18
 */
#pragma once
#include "bcos-tars-protocol/tars/RpcService.h"
#include <bcos-tool/NodeConfig.h>
#include <bcos-utilities/BoostLogInitializer.h>
#include <tarscpp/servant/Application.h>

namespace bcos::initializer
{
class Initializer;
}
namespace bcostars
{
class NodeServiceApp : public Application
{
public:
    NodeServiceApp() {}
    ~NodeServiceApp() override {}

    void initialize() override;
    void destroyApp() override;

protected:
    virtual void initConfig()
    {
        m_iniConfigPath = ServerConfig::BasePath + "/config.ini";
        m_genesisConfigPath = ServerConfig::BasePath + "/config.genesis";
        m_privateKeyPath = ServerConfig::BasePath + "/node.pem";
        addConfig("node.pem");
        addConfig("config.genesis");
        addConfig("config.ini");
    }
    virtual void initLog();
    virtual void initNodeService();
    virtual void initTarsNodeService();

private:
    bcos::BoostLogInitializer::Ptr m_logInitializer;
    std::string m_iniConfigPath;
    std::string m_genesisConfigPath;
    std::string m_privateKeyPath;
    std::shared_ptr<bcos::initializer::Initializer> m_nodeInitializer;
};
}  // namespace bcostars