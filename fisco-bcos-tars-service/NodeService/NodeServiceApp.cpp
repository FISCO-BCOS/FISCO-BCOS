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
 * @file NodeServiceApp.cpp
 * @author: yujiechen
 * @date 2021-10-18
 */
#include "NodeServiceApp.h"
#include "libinitializer/Initializer.h"
#include <bcos-framework/protocol/GlobalConfig.h>
#include <bcos-scheduler/src/SchedulerImpl.h>
#include <bcos-tars-protocol/client/RpcServiceClient.h>
#include <bcos-tars-protocol/protocol/ProtocolInfoCodecImpl.h>
#include <fisco-bcos-tars-service/Common/TarsUtils.h>
#include <fisco-bcos-tars-service/FrontService/FrontServiceServer.h>
#include <fisco-bcos-tars-service/LedgerService/LedgerServiceServer.h>
#include <fisco-bcos-tars-service/PBFTService/PBFTServiceServer.h>
#include <fisco-bcos-tars-service/SchedulerService/SchedulerServiceServer.h>
#include <fisco-bcos-tars-service/TxPoolService/TxPoolServiceServer.h>

using namespace bcostars;
using namespace bcos;
using namespace bcos::initializer;
using namespace bcos::protocol;

void NodeServiceApp::destroyApp()
{
    // terminate the network threads
    Application::terminate();
    // stop the nodeService
    m_nodeInitializer->stop();
}

void NodeServiceApp::initialize()
{
    // Note: since tars Application catch the exception and output the error message with
    // e.what() which unable to explicitly display specific error messages, we actively catch
    // and output exception information here
    try
    {
        BCOS_LOG(INFO) << LOG_DESC("initGlobalConfig");
        g_BCOSConfig.setCodec(std::make_shared<bcostars::protocol::ProtocolInfoCodecImpl>());

        initConfig();
        initLog();
        initNodeService();
        initTarsNodeService();
        initServiceInfo(this);
        m_nodeInitializer->start();
    }
    catch (std::exception const& e)
    {
        std::cout << "init NodeService failed, error: " << boost::diagnostic_information(e)
                  << std::endl;
        throw e;
    }
}

void NodeServiceApp::initLog()
{
    boost::property_tree::ptree pt;
    boost::property_tree::read_ini(m_iniConfigPath, pt);
    m_logInitializer = std::make_shared<BoostLogInitializer>();
    m_logInitializer->setLogPath(getLogPath());
    m_logInitializer->initLog(pt);
}

void NodeServiceApp::initNodeService()
{
    m_nodeInitializer = std::make_shared<Initializer>();
    m_nodeInitializer->initMicroServiceNode(
        m_nodeArchType, m_iniConfigPath, m_genesisConfigPath, m_privateKeyPath, getLogPath());
    auto rpcServiceName = m_nodeInitializer->nodeConfig()->rpcServiceName();
    auto rpcServicePrx =
        tars::Application::getCommunicator()->stringToProxy<bcostars::RpcServicePrx>(rpcServiceName);
    auto rpc = std::make_shared<bcostars::RpcServiceClient>(rpcServicePrx, rpcServiceName);
    m_nodeInitializer->initNotificationHandlers(rpc);

    // NOTE: this should be last called
    m_nodeInitializer->initSysContract();
}

void NodeServiceApp::initTarsNodeService()
{
    // init the txpool servant
    TxPoolServiceParam txpoolParam;
    txpoolParam.txPoolInitializer = m_nodeInitializer->txPoolInitializer();
    addServantWithParams<TxPoolServiceServer, TxPoolServiceParam>(
        getProxyDesc(TXPOOL_SERVANT_NAME), txpoolParam);

    // init the pbft servant
    PBFTServiceParam pbftParam;
    pbftParam.pbftInitializer = m_nodeInitializer->pbftInitializer();
    addServantWithParams<PBFTServiceServer, PBFTServiceParam>(
        getProxyDesc(CONSENSUS_SERVANT_NAME), pbftParam);

    // init the ledger
    LedgerServiceParam ledgerParam;
    ledgerParam.ledger = m_nodeInitializer->ledger();
    addServantWithParams<LedgerServiceServer, LedgerServiceParam>(
        getProxyDesc(LEDGER_SERVANT_NAME), ledgerParam);

    // init the scheduler
    SchedulerServiceParam schedulerParam;
    schedulerParam.scheduler = m_nodeInitializer->scheduler();
    schedulerParam.cryptoSuite = m_nodeInitializer->protocolInitializer()->cryptoSuite();
    addServantWithParams<SchedulerServiceServer, SchedulerServiceParam>(
        getProxyDesc(SCHEDULER_SERVANT_NAME), schedulerParam);

    // init the frontService, for the gateway to access the frontService
    FrontServiceParam frontServiceParam;
    frontServiceParam.frontServiceInitializer = m_nodeInitializer->frontService();
    addServantWithParams<FrontServiceServer, FrontServiceParam>(
        getProxyDesc(FRONT_SERVANT_NAME), frontServiceParam);
}

void NodeServiceApp::initServiceInfo(Application* _application)
{
    auto nodeInfo = m_nodeInitializer->pbftInitializer()->nodeInfo();
    // Note: must call this after addServantWithParams of NodeServiceApp
    auto schedulerDesc = getEndPointDescByAdapter(_application, SCHEDULER_SERVANT_NAME);
    if (!schedulerDesc.first)
    {
        throw std::runtime_error("init NodeService failed for get scheduler-desc failed");
    }
    nodeInfo->appendServiceInfo(SCHEDULER, schedulerDesc.second);

    auto ledgerDesc = getEndPointDescByAdapter(_application, LEDGER_SERVANT_NAME);
    if (!ledgerDesc.first)
    {
        throw std::runtime_error("init NodeService failed for get ledger-desc failed");
    }
    nodeInfo->appendServiceInfo(LEDGER, ledgerDesc.second);

    auto frontServiceDesc = getEndPointDescByAdapter(_application, FRONT_SERVANT_NAME);
    if (!frontServiceDesc.first)
    {
        throw std::runtime_error("init NodeService failed for get front-service-desc failed");
    }
    nodeInfo->appendServiceInfo(FRONT, frontServiceDesc.second);

    auto txpoolServiceDesc = getEndPointDescByAdapter(_application, TXPOOL_SERVANT_NAME);
    if (!txpoolServiceDesc.first)
    {
        throw std::runtime_error("init NodeService failed for get txpool-service-desc failed");
    }
    nodeInfo->appendServiceInfo(TXPOOL, txpoolServiceDesc.second);

    auto consensusServiceDesc = getEndPointDescByAdapter(_application, CONSENSUS_SERVANT_NAME);
    if (!consensusServiceDesc.first)
    {
        throw std::runtime_error("init NodeService failed for get consensus-service-desc failed");
    }
    nodeInfo->appendServiceInfo(CONSENSUS, consensusServiceDesc.second);
    // sync the latest groupInfo to rpc/gateway
    m_nodeInitializer->pbftInitializer()->onGroupInfoChanged();
    BCOS_LOG(INFO) << LOG_DESC("initServiceInfo") << LOG_KV("schedulerDesc", schedulerDesc.second)
                   << LOG_KV("ledgerDesc", ledgerDesc.second)
                   << LOG_KV("frontServiceDesc", frontServiceDesc.second)
                   << LOG_KV("txpoolServiceDesc", txpoolServiceDesc.second)
                   << LOG_KV("consensusServiceDesc", consensusServiceDesc.second);
}