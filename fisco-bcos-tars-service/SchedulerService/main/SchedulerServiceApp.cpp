/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @brief Application for the SchedulerService
 * @file SchedulerServiceApp.cpp
 * @author: yujiechen
 * @date 2022-5-10
 */
#include "SchedulerServiceApp.h"
#include "Common/TarsUtils.h"
#include "SchedulerService/SchedulerServiceServer.h"
#include "libinitializer/CommandHelper.h"
#include "libinitializer/SchedulerInitializer.h"
#include "libinitializer/StorageInitializer.h"
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework/interfaces/protocol/ServiceDesc.h>
#include <bcos-ledger/src/libledger/Ledger.h>
#include <bcos-scheduler/src/SchedulerImpl.h>
#include <bcos-scheduler/src/TarsRemoteExecutorManager.h>
#include <bcos-tars-protocol/client/RpcServiceClient.h>
#include <bcos-tars-protocol/client/TxPoolServiceClient.h>
#include <bcos-tars-protocol/protocol/ExecutionMessageImpl.h>

using namespace bcostars;
using namespace bcos::initializer;
using namespace bcos::ledger;
using namespace bcos::protocol;

void SchedulerServiceApp::initialize()
{
    try
    {
        createAndInitSchedulerService();
    }
    catch (std::exception const& e)
    {
        std::cout << "init SchedulerService failed, error: " << boost::diagnostic_information(e)
                  << std::endl;
        throw e;
    }
}

void SchedulerServiceApp::createAndInitSchedulerService()
{
    fetchConfig();
    initConfig();

    // for stat the nodeVersion
    bcos::initializer::showNodeVersionMetric();

    auto rpcServiceName = m_nodeConfig->rpcServiceName();
    SCHEDULER_SERVICE_LOG(INFO) << LOG_DESC("create RpcServiceClient")
                                << LOG_KV("rpcServiceName", rpcServiceName);
    auto rpcServicePrx =
        Application::getCommunicator()->stringToProxy<bcostars::RpcServicePrx>(rpcServiceName);
    m_rpc = std::make_shared<bcostars::RpcServiceClient>(rpcServicePrx, rpcServiceName);

    auto txpoolServiceName = m_nodeConfig->txpoolServiceName();

    SCHEDULER_SERVICE_LOG(INFO) << LOG_DESC("create TxPoolServiceClient")
                                << LOG_KV("txpoolServiceName", txpoolServiceName);
    auto txpoolServicePrx =
        Application::getCommunicator()->stringToProxy<bcostars::TxPoolServicePrx>(
            txpoolServiceName);
    m_txpool = std::make_shared<bcostars::TxPoolServiceClient>(txpoolServicePrx,
        m_protocolInitializer->cryptoSuite(), m_protocolInitializer->blockFactory());

    SCHEDULER_SERVICE_LOG(INFO) << LOG_DESC("createScheduler");
    createScheduler();
    SCHEDULER_SERVICE_LOG(INFO) << LOG_DESC("createScheduler success");

    SCHEDULER_SERVICE_LOG(INFO) << LOG_DESC("addServant for scheduler");
    SchedulerServiceParam param;
    param.scheduler = m_scheduler;
    param.cryptoSuite = m_protocolInitializer->cryptoSuite();
    param.blockFactory = m_protocolInitializer->blockFactory();
    addServantWithParams<SchedulerServiceServer, SchedulerServiceParam>(
        getProxyDesc(SCHEDULER_SERVANT_NAME), param);
    SCHEDULER_SERVICE_LOG(INFO) << LOG_DESC("addServant for scheduler success");
}
void SchedulerServiceApp::fetchConfig()
{
    m_iniConfigPath = ServerConfig::BasePath + "/config.ini";
    m_genesisConfigPath = ServerConfig::BasePath + "/config.genesis";
    addConfig("config.ini");
    addConfig("config.genesis");
    SCHEDULER_SERVICE_LOG(INFO) << LOG_DESC("fetchConfig success")
                                << LOG_KV("iniConfigPath", m_iniConfigPath)
                                << LOG_KV("genesisConfigPath", m_genesisConfigPath);
}

void SchedulerServiceApp::initConfig()
{
    boost::property_tree::ptree pt;
    boost::property_tree::ptree genesisPt;
    boost::property_tree::read_ini(m_iniConfigPath, pt);
    boost::property_tree::read_ini(m_genesisConfigPath, pt);
    m_logInitializer = std::make_shared<bcos::BoostLogInitializer>();
    m_logInitializer->setLogPath(getLogPath());
    m_logInitializer->initLog(pt);

    m_nodeConfig =
        std::make_shared<bcos::tool::NodeConfig>(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    m_nodeConfig->loadConfig(pt);
    m_nodeConfig->loadGenesisConfig(genesisPt);
    m_nodeConfig->loadServiceConfig(pt);
    m_nodeConfig->loadNodeServiceConfig(m_nodeConfig->nodeName(), pt, true);
    // init the protocol
    m_protocolInitializer = std::make_shared<ProtocolInitializer>();
    m_protocolInitializer->init(m_nodeConfig);
    SCHEDULER_SERVICE_LOG(INFO) << LOG_DESC("initConfig success");
}

void SchedulerServiceApp::createScheduler()
{
    auto blockFactory = m_protocolInitializer->blockFactory();
    auto ledger = std::make_shared<bcos::ledger::Ledger>(
        blockFactory, StorageInitializer::build(m_nodeConfig->pdAddrs(), getLogPath()));
    auto executionMessageFactory =
        std::make_shared<bcostars::protocol::ExecutionMessageFactoryImpl>();
    auto executorManager = std::make_shared<bcos::scheduler::RemoteExecutorManager>(
        m_nodeConfig->executorServiceName());

    m_scheduler = SchedulerInitializer::build(executorManager, ledger,
        StorageInitializer::build(m_nodeConfig->pdAddrs(), getLogPath()), executionMessageFactory, blockFactory,
        m_protocolInitializer->txResultFactory(), m_protocolInitializer->cryptoSuite()->hashImpl(),
        m_nodeConfig->isAuthCheck(), m_nodeConfig->isWasm());
    auto scheduler = std::dynamic_pointer_cast<bcos::scheduler::SchedulerImpl>(m_scheduler);
    // handler for notify block number
    scheduler->registerBlockNumberReceiver([this](bcos::protocol::BlockNumber number) {
        BCOS_LOG(INFO) << "Notify blocknumber: " << number;
        // Note: the interface will notify blockNumber to all rpc nodes in pro/max mode
        m_rpc->asyncNotifyBlockNumber(
            m_nodeConfig->groupId(), m_nodeConfig->nodeName(), number, [](bcos::Error::Ptr) {});
    });
    // handler for notify transactions
    scheduler->registerTransactionNotifier([this](bcos::protocol::BlockNumber _blockNumber,
                                               bcos::protocol::TransactionSubmitResultsPtr _result,
                                               std::function<void(bcos::Error::Ptr)> _callback) {
        // only response to the requester
        m_txpool->asyncNotifyBlockResult(_blockNumber, _result, _callback);
    });
}