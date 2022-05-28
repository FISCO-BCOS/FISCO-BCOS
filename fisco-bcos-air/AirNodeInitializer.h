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
 * @brief Initializer for all the modules
 * @file LocalInitializer.h
 * @author: yujiechen
 * @date 2021-10-28
 */

#pragma once
#include "libinitializer/Initializer.h"
#include <bcos-framework//gateway/GatewayInterface.h>
#include <bcos-framework//rpc/RPCInterface.h>
namespace bcos
{
namespace node
{
class AirNodeInitializer
{
public:
    AirNodeInitializer() = default;
    virtual ~AirNodeInitializer() { stop(); }

    virtual void init(std::string const& _configFilePath, std::string const& _genesisFile);
    virtual void start();
    virtual void stop();

protected:
    virtual void initAirNode(std::string const& _configFilePath, std::string const& _genesisFile,
        bcos::gateway::GatewayInterface::Ptr _gateway)
    {
        m_nodeInitializer = std::make_shared<bcos::initializer::Initializer>();
        m_nodeInitializer->initAirNode(_configFilePath, _genesisFile, _gateway);
    }

private:
    BoostLogInitializer::Ptr m_logInitializer;
    bcos::initializer::Initializer::Ptr m_nodeInitializer;

    bcos::gateway::GatewayInterface::Ptr m_gateway;
    bcos::rpc::RPCInterface::Ptr m_rpc;
};
}  // namespace node
}  // namespace bcos