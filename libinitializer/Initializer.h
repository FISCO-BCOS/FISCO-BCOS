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
 * @file Initializer.h
 * @author: yujiechen
 * @date 2021-06-11
 */
#pragma once
#include "FrontServiceInitializer.h"
#include "LedgerInitializer.h"
#include "PBFTInitializer.h"
#include "ProtocolInitializer.h"
#include "SchedulerInitializer.h"
#include "StorageInitializer.h"
#include "TxPoolInitializer.h"
#include <bcos-framework/interfaces/gateway/GatewayInterface.h>
#include <bcos-framework/interfaces/rpc/RPCInterface.h>
#include <bcos-framework/libutilities/BoostLogInitializer.h>
#include <memory>

namespace bcos::initializer
{
class Initializer
{
public:
    using Ptr = std::shared_ptr<Initializer>;
    Initializer() = default;
    virtual ~Initializer() { stop(); }


    virtual void start();
    virtual void stop();

    bcos::tool::NodeConfig::Ptr nodeConfig() { return m_nodeConfig; }
    ProtocolInitializer::Ptr protocolInitializer() { return m_protocolInitializer; }
    PBFTInitializer::Ptr pbftInitializer() { return m_pbftInitializer; }
    TxPoolInitializer::Ptr txPoolInitializer() { return m_txpoolInitializer; }

    bcos::ledger::LedgerInterface::Ptr ledger() { return m_ledger; }
    bcos::scheduler::SchedulerInterface::Ptr scheduler() { return m_scheduler; }

    FrontServiceInitializer::Ptr frontService() { return m_frontServiceInitializer; }

    void initLocalNode(std::string const& _configFilePath, std::string const& _genesisFile,
        bcos::gateway::GatewayInterface::Ptr _gateway)
    {
        initConfig(_configFilePath, _genesisFile, "", true);
        init(bcos::initializer::NodeArchitectureType::AIR, _configFilePath, _genesisFile, _gateway,
            true);
    }
    void initMicroServiceNode(std::string const& _configFilePath, std::string const& _genesisFile,
        std::string const& _privateKeyPath);

protected:
    virtual void init(bcos::initializer::NodeArchitectureType _nodeArchType,
        std::string const& _configFilePath, std::string const& _genesisFile,
        bcos::gateway::GatewayInterface::Ptr _gateway, bool _localMode);

    virtual void initConfig(std::string const& _configFilePath, std::string const& _genesisFile,
        std::string const& _privateKeyPath, bool _localMode);

private:
    bcos::tool::NodeConfig::Ptr m_nodeConfig;
    ProtocolInitializer::Ptr m_protocolInitializer;
    FrontServiceInitializer::Ptr m_frontServiceInitializer;
    TxPoolInitializer::Ptr m_txpoolInitializer;

    PBFTInitializer::Ptr m_pbftInitializer;

    bcos::ledger::LedgerInterface::Ptr m_ledger;
    bcos::scheduler::SchedulerInterface::Ptr m_scheduler;
};
}  // namespace bcos::initializer