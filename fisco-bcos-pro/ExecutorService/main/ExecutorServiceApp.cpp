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
 * @brief Application for the ExecutorService
 * @file ExecutorServiceApp.cpp
 * @author: yujiechen
 * @date 2022-5-10
 */
#include "ExecutorServiceApp.h"
#include "Common/TarsUtils.h"
#include "fisco-bcos-pro/ExecutorService/ExecutorServiceServer.h"
#include "libinitializer/CommandHelper.h"
#include "libinitializer/ExecutorInitializer.h"
#include "libinitializer/ParallelExecutor.h"
#include "libinitializer/StorageInitializer.h"
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework/interfaces/protocol/ServiceDesc.h>
#include <bcos-tars-protocol/client/SchedulerServiceClient.h>
#include <bcos-tars-protocol/client/TxPoolServiceClient.h>
#include <bcos-tars-protocol/protocol/ExecutionMessageImpl.h>

using namespace bcostars;
using namespace bcos::initializer;
using namespace bcos::protocol;

void ExecutorServiceApp::initialize()
{
    try
    {
        createAndInitExecutor();
    }
    catch (std::exception const& e)
    {
        std::cout << "init ExecutorService failed, error: " << boost::diagnostic_information(e)
                  << std::endl;
        throw e;
    }
}

void ExecutorServiceApp::createAndInitExecutor()
{
    // fetch config
    m_iniConfigPath = ServerConfig::BasePath + "/config.ini";
    addConfig("config.ini");
    EXECUTOR_SERVICE_LOG(INFO) << LOG_DESC("createAndInitExecutor: fetch config success")
                               << LOG_KV("iniConfigPath", m_iniConfigPath);

    // init log
    boost::property_tree::ptree pt;
    boost::property_tree::read_ini(m_iniConfigPath, pt);
    m_logInitializer = std::make_shared<bcos::BoostLogInitializer>();
    m_logInitializer->setLogPath(getLogPath());
    m_logInitializer->initLog(pt);

    // load protocolInitializer
    EXECUTOR_SERVICE_LOG(INFO) << LOG_DESC("loadNodeConfig");
    m_nodeConfig =
        std::make_shared<bcos::tool::NodeConfig>(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    m_nodeConfig->loadConfig(m_iniConfigPath);
    m_nodeConfig->loadNodeServiceConfig(m_nodeConfig->nodeName(), pt);
    // init the protocol
    m_protocolInitializer = std::make_shared<ProtocolInitializer>();
    m_protocolInitializer->init(m_nodeConfig);
    EXECUTOR_SERVICE_LOG(INFO) << LOG_DESC("loadNodeConfig success");
    // for stat the nodeVersion
    bcos::initializer::showNodeVersionMetric();

    // create txpool client
    auto txpoolServiceName = m_nodeConfig->txpoolServiceName();
    EXECUTOR_SERVICE_LOG(INFO) << LOG_DESC("create TxPoolServiceClient")
                               << LOG_KV("txpoolServiceName", txpoolServiceName);
    auto txpoolServicePrx =
        Application::getCommunicator()->stringToProxy<bcostars::TxPoolServicePrx>(
            txpoolServiceName);
    m_txpool = std::make_shared<bcostars::TxPoolServiceClient>(txpoolServicePrx,
        m_protocolInitializer->cryptoSuite(), m_protocolInitializer->blockFactory());

    auto schedulerServiceName = m_nodeConfig->schedulerServiceName();
    EXECUTOR_SERVICE_LOG(INFO) << LOG_DESC("create SchedulerServiceClient")
                               << LOG_KV("schedulerServiceName", schedulerServiceName);
    auto schedulerPrx =
        Application::getCommunicator()->stringToProxy<bcostars::SchedulerServicePrx>(
            schedulerServiceName);
    m_scheduler = std::make_shared<bcostars::SchedulerServiceClient>(
        schedulerPrx, m_protocolInitializer->cryptoSuite());

    // create executor
    auto storage = StorageInitializer::build(m_nodeConfig->pdAddrs());
    std::shared_ptr<bcos::storage::LRUStateStorage> cache = nullptr;
    if (m_nodeConfig->enableLRUCacheStorage())
    {
        cache = std::make_shared<bcos::storage::LRUStateStorage>(storage);
        cache->setMaxCapacity(m_nodeConfig->cacheSize());
        EXECUTOR_SERVICE_LOG(INFO)
            << "createAndInitExecutor: enableLRUCacheStorage, size: " << m_nodeConfig->cacheSize();
    }
    else
    {
        EXECUTOR_SERVICE_LOG(INFO) << LOG_DESC("createAndInitExecutor: disableLRUCacheStorage");
    }

    auto executionMessageFactory =
        std::make_shared<bcostars::protocol::ExecutionMessageFactoryImpl>();
    auto executor = ExecutorInitializer::build(m_txpool, cache, storage, executionMessageFactory,
        m_protocolInitializer->cryptoSuite()->hashImpl(), m_nodeConfig->isWasm(),
        m_nodeConfig->isAuthCheck());

    m_executor = std::make_shared<bcos::initializer::ParallelExecutor>(executor);

    EXECUTOR_SERVICE_LOG(INFO) << LOG_DESC("addServant for executor");
    ExecutorServiceParam param;
    param.executor = m_executor;
    param.cryptoSuite = m_protocolInitializer->cryptoSuite();
    addServantWithParams<ExecutorServiceServer, ExecutorServiceParam>(
        getProxyDesc(EXECUTOR_SERVANT_NAME), param);
    // TODO: register to the scheduler

    auto ret = getEndPointDescByAdapter(this, EXECUTOR_SERVANT_NAME);
    if (!ret.first)
    {
        throw std::runtime_error("load endpoint information failed");
    }
    auto executorName = ret.second;
    m_scheduler->registerExecutor(executorName, nullptr, [executorName](bcos::Error::Ptr&& _error) {
        // TODO: exit when error
        if (_error)
        {
            EXECUTOR_SERVICE_LOG(ERROR)
                << LOG_DESC("registerExecutor error") << LOG_KV("name", executorName)
                << LOG_KV("code", _error->errorCode()) << LOG_KV("msg", _error->errorMessage());
            return;
        }
        EXECUTOR_SERVICE_LOG(INFO)
            << LOG_DESC("registerExecutor success") << LOG_KV("name", executorName);
    });
    EXECUTOR_SERVICE_LOG(INFO) << LOG_DESC("addServant for executor success");
}