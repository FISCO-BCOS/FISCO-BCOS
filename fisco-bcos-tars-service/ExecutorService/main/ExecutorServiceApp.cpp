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
#include "../../Common/TarsUtils.h"
#include "../ExecutorServiceServer.h"
#include "bcos-executor/src/executor/SwitchExecutorManager.h"
#include "libinitializer/CommandHelper.h"
#include "libinitializer/ExecutorInitializer.h"
#include "libinitializer/StorageInitializer.h"
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework/ledger/LedgerInterface.h>
#include <bcos-framework/protocol/ServiceDesc.h>
#include <bcos-ledger/src/libledger/Ledger.h>
#include <bcos-table/src/StateStorageFactory.h>
#include <bcos-tars-protocol/client/SchedulerServiceClient.h>
#include <bcos-tars-protocol/client/TxPoolServiceClient.h>
#include <bcos-tars-protocol/protocol/ExecutionMessageImpl.h>

using namespace bcostars;
using namespace bcos::storage;
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
        exit(-1);
    }
}

void ExecutorServiceApp::createAndInitExecutor()
{
    // fetch config
    m_iniConfigPath = tars::ServerConfig::BasePath + "/config.ini";
    m_genesisConfigPath = tars::ServerConfig::BasePath + "/config.genesis";
    addConfig("config.ini");
    addConfig("config.genesis");
    EXECUTOR_SERVICE_LOG(INFO) << LOG_DESC("createAndInitExecutor: fetch config success")
                               << LOG_KV("iniConfigPath", m_iniConfigPath)
                               << LOG_KV("genesisConfigPath", m_genesisConfigPath);

    m_nodeConfig =
        std::make_shared<bcos::tool::NodeConfig>(std::make_shared<bcos::crypto::KeyFactoryImpl>());

    // init log
    boost::property_tree::ptree pt;
    boost::property_tree::read_ini(m_iniConfigPath, pt);

    // init service.without_tars_framework first for determine the log path
    m_nodeConfig->loadWithoutTarsFrameworkConfig(pt);

    m_logInitializer = std::make_shared<bcos::BoostLogInitializer>();
    if (!m_nodeConfig->withoutTarsFramework())
    {
        m_logInitializer->setLogPath(getLogPath());
    }
    m_logInitializer->initLog(m_iniConfigPath);

    boost::property_tree::ptree genesisPt;
    boost::property_tree::read_ini(m_genesisConfigPath, genesisPt);

    // load protocolInitializer
    EXECUTOR_SERVICE_LOG(INFO) << LOG_DESC("loadNodeConfig");

    m_nodeConfig->loadGenesisConfig(genesisPt);
    m_nodeConfig->loadConfig(pt);
    m_nodeConfig->loadNodeServiceConfig(m_nodeConfig->nodeName(), pt, true);
    // init the protocol
    m_protocolInitializer = std::make_shared<ProtocolInitializer>();
    m_protocolInitializer->init(m_nodeConfig);
    EXECUTOR_SERVICE_LOG(INFO) << LOG_DESC("loadNodeConfig success");
    // for stat the nodeVersion
    bcos::initializer::showNodeVersionMetric();

    auto withoutTarsFramework = m_nodeConfig->withoutTarsFramework();

    // create txpool client
    auto txpoolServiceName = m_nodeConfig->txpoolServiceName();
    EXECUTOR_SERVICE_LOG(INFO) << LOG_DESC("create TxPoolServiceClient")
                               << LOG_KV("txpoolServiceName", txpoolServiceName)
                               << LOG_KV("withoutTarsFramework", withoutTarsFramework);

    std::vector<tars::TC_Endpoint> endPoints;
    m_nodeConfig->getTarsClientProxyEndpoints(bcos::protocol::TXPOOL_NAME, endPoints);

    auto txpoolServicePrx = createServantProxy<bcostars::TxPoolServicePrx>(
        withoutTarsFramework, txpoolServiceName, endPoints);

    m_txpool = std::make_shared<bcostars::TxPoolServiceClient>(txpoolServicePrx,
        m_protocolInitializer->cryptoSuite(), m_protocolInitializer->blockFactory());

    auto schedulerServiceName = m_nodeConfig->schedulerServiceName();

    m_nodeConfig->getTarsClientProxyEndpoints(bcos::protocol::SCHEDULER_NAME, endPoints);

    EXECUTOR_SERVICE_LOG(INFO) << LOG_DESC("create SchedulerServiceClient")
                               << LOG_KV("schedulerServiceName", schedulerServiceName);

    auto schedulerPrx = createServantProxy<bcostars::SchedulerServicePrx>(
        withoutTarsFramework, schedulerServiceName, endPoints);

    m_scheduler = std::make_shared<bcostars::SchedulerServiceClient>(
        schedulerPrx, m_protocolInitializer->cryptoSuite());

    // create executor
    auto storage = StorageInitializer::build(m_nodeConfig->pdAddrs(), getLogPath(),
        m_nodeConfig->pdCaPath(), m_nodeConfig->pdCertPath(), m_nodeConfig->pdKeyPath());

    bcos::storage::CacheStorageFactory::Ptr cacheFactory = nullptr;
    if (m_nodeConfig->enableLRUCacheStorage())
    {
        cacheFactory = std::make_shared<bcos::storage::CacheStorageFactory>(
            storage, m_nodeConfig->cacheSize());
        EXECUTOR_SERVICE_LOG(INFO)
            << "createAndInitExecutor: enableLRUCacheStorage, size: " << m_nodeConfig->cacheSize();
    }
    else
    {
        EXECUTOR_SERVICE_LOG(INFO) << LOG_DESC("createAndInitExecutor: disableLRUCacheStorage");
    }

    auto executionMessageFactory =
        std::make_shared<bcostars::protocol::ExecutionMessageFactoryImpl>();
    auto stateStorageFactory =
        std::make_shared<bcos::storage::StateStorageFactory>(m_nodeConfig->keyPageSize());

    auto blockFactory = m_protocolInitializer->blockFactory();
    auto ledger = std::make_shared<bcos::ledger::Ledger>(
        blockFactory, storage, m_nodeConfig->blockLimit(), nullptr);

    auto executorFactory = std::make_shared<bcos::executor::TransactionExecutorFactory>(ledger,
        m_txpool, cacheFactory, storage, executionMessageFactory, stateStorageFactory,
        m_protocolInitializer->cryptoSuite()->hashImpl(), m_nodeConfig->isWasm(),
        m_nodeConfig->vmCacheSize(), m_nodeConfig->isAuthCheck(), "executor");

    m_executor = std::make_shared<bcos::executor::SwitchExecutorManager>(executorFactory);

    std::weak_ptr<bcos::executor::SwitchExecutorManager> executorWeakPtr = m_executor;
    std::weak_ptr<bcos::storage::TiKVStorage> storageWeakPtr =
        dynamic_pointer_cast<bcos::storage::TiKVStorage>(storage);
    auto switchHandler = [executor = executorWeakPtr, storageWeakPtr]() {
        if (executor.lock())
        {
            executor.lock()->triggerSwitch();
        }
    };
    dynamic_pointer_cast<bcos::storage::TiKVStorage>(storage)->setSwitchHandler(switchHandler);


    ExecutorServiceParam param;
    param.executor = m_executor;
    param.cryptoSuite = m_protocolInitializer->cryptoSuite();
    addServantWithParams<ExecutorServiceServer, ExecutorServiceParam>(
        getProxyDesc(EXECUTOR_SERVANT_NAME), param);
    auto ret = getEndPointDescByAdapter(this, EXECUTOR_SERVANT_NAME);
    if (!ret.first)
    {
        throw std::runtime_error("load endpoint information failed");
    }
    m_executorName = ret.second;
    EXECUTOR_SERVICE_LOG(INFO) << LOG_DESC("createAndInitExecutor success");
}
