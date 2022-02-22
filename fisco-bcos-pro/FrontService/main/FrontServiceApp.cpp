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
 * @brief application for FrontService
 * @file FrontServiceApp.cpp
 * @author: yujiechen
 * @date 2021-10-17
 */
#include "FrontServiceApp.h"
#include "Common/TarsUtils.h"
#include <bcos-tars-protocol/client/GatewayServiceClient.h>
#include <bcos-tars-protocol/client/PBFTServiceClient.h>
#include <bcos-tars-protocol/client/TxPoolServiceClient.h>

using namespace bcostars;
using namespace bcos;
using namespace bcos::tool;
using namespace bcos::initializer;

void FrontServiceApp::initialize()
{
    initConfig();
    initService();
    FrontServiceParam param;
    param.frontServiceInitializer = m_frontServiceInitializer;
    addServantWithParams<FrontServiceServer, FrontServiceParam>(
        getProxyDesc(bcos::protocol::FRONT_SERVANT_NAME), param);
}

void FrontServiceApp::initService()
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

    // init the protocol
    auto protocolInitializer = std::make_shared<ProtocolInitializer>();
    protocolInitializer->init(nodeConfig);
    protocolInitializer->loadKeyPair(m_privateKeyPath);
    nodeConfig->loadNodeServiceConfig(protocolInitializer->keyPair()->publicKey()->hex(), pt);
    nodeConfig->loadServiceConfig(pt);

    // get gateway client
    auto gatewayPrx = Application::getCommunicator()->stringToProxy<bcostars::GatewayServicePrx>(
        nodeConfig->gatewayServiceName());
    auto gateWay = std::make_shared<bcostars::GatewayServiceClient>(gatewayPrx,
        nodeConfig->gatewayServiceName(), protocolInitializer->cryptoSuite()->keyFactory());

    m_frontServiceInitializer =
        std::make_shared<FrontServiceInitializer>(nodeConfig, protocolInitializer, gateWay);

    // get pbft client
    auto pbftPrx = Application::getCommunicator()->stringToProxy<PBFTServicePrx>(
        nodeConfig->consensusServiceName());
    auto pbft = std::make_shared<PBFTServiceClient>(pbftPrx);

    // get sync client
    auto blockSync = std::make_shared<BlockSyncServiceClient>(pbftPrx);

    // get txpool client
    auto txpoolPrx = Application::getCommunicator()->stringToProxy<bcostars::TxPoolServicePrx>(
        nodeConfig->txpoolServiceName());
    auto txpoolClient = std::make_shared<bcostars::TxPoolServiceClient>(
        txpoolPrx, protocolInitializer->cryptoSuite(), protocolInitializer->blockFactory());

    // init the front service
    m_frontServiceInitializer->init(pbft, blockSync, txpoolClient);

    // start the front service
    m_frontServiceInitializer->start();
}