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
 * @brief application for PBFTService
 * @file PBFTServiceApp.cpp
 * @author: yujiechen
 * @date 2021-10-15
 */
#include "PBFTServiceApp.h"
#include "Common/TarsUtils.h"
#include <bcos-ledger/libledger/Ledger.h>
#include <bcos-tars-protocol/client/FrontServiceClient.h>
#include <bcos-tars-protocol/client/SchedulerServiceClient.h>
#include <bcos-tars-protocol/client/TxPoolServiceClient.h>

using namespace bcos;
using namespace bcos::tool;
using namespace bcos::ledger;
using namespace bcostars;
using namespace bcos::initializer;

void PBFTServiceApp::initialize()
{
    initConfig();
    initService();
    PBFTServiceParam param;
    param.pbftInitializer = m_pbftInitializer;
    addServantWithParams<PBFTServiceServer, PBFTServiceParam>(
        getProxyDesc(bcos::protocol::CONSENSUS_SERVANT_NAME), param);
}

void PBFTServiceApp::initService()
{
    // init log
    boost::property_tree::ptree pt;
    boost::property_tree::read_ini(m_iniConfigPath, pt);
    m_logInitializer = std::make_shared<BoostLogInitializer>();
    m_logInitializer->setLogPath(getLogPath());
    m_logInitializer->initLog(pt);

    // load iniConfig
    auto nodeConfig =
        std::make_shared<NodeConfig>(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    nodeConfig->loadConfig(m_iniConfigPath);
    nodeConfig->loadGenesisConfig(m_genesisConfigPath);

    // init the protocol
    auto protocolInitializer = std::make_shared<ProtocolInitializer>();
    protocolInitializer->init(nodeConfig);
    protocolInitializer->loadKeyPair(m_privateKeyPath);
    nodeConfig->loadNodeServiceConfig(protocolInitializer->keyPair()->publicKey()->hex(), pt);
    nodeConfig->loadServiceConfig(pt);

    auto keyFactory = protocolInitializer->keyFactory();
    auto blockFactory = protocolInitializer->blockFactory();
    // create the frontService
    auto frontServiceProxy =
        Application::getCommunicator()->stringToProxy<bcostars::FrontServicePrx>(
            nodeConfig->frontServiceName());
    auto frontService =
        std::make_shared<bcostars::FrontServiceClient>(frontServiceProxy, keyFactory);

    // create the ledger: TODO: create tikv storage
    auto ledger = std::make_shared<bcos::ledger::Ledger>(blockFactory, nullptr);

    // create txpool
    auto txpoolProxy = Application::getCommunicator()->stringToProxy<bcostars::TxPoolServicePrx>(
        nodeConfig->txpoolServiceName());
    auto txpool = std::make_shared<bcostars::TxPoolServiceClient>(
        txpoolProxy, protocolInitializer->cryptoSuite(), blockFactory);
    PBFTSERVICE_LOG(INFO) << LOG_DESC("create TxPool client success");

    // create scheduler
    auto schedulerPrx =
        Application::getCommunicator()->stringToProxy<bcostars::SchedulerServicePrx>(
            nodeConfig->schedulerServiceName());
    auto scheduler = std::make_shared<bcostars::SchedulerServiceClient>(
        schedulerPrx, protocolInitializer->cryptoSuite());

    // TODO: create tikv storage
    m_pbftInitializer =
        std::make_shared<ProPBFTInitializer>(bcos::initializer::NodeArchitectureType::MAX,
            nodeConfig, protocolInitializer, txpool, ledger, scheduler, nullptr, frontService);
    m_pbftInitializer->init();
    m_pbftInitializer->start();
}