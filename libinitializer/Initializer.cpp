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
 * @file Initializer.cpp
 * @author: yujiechen
 * @date 2021-06-11

 * @brief Initializer for all the modules
 * @file Initializer.cpp
 * @author: ancelmo
 * @date 2021-10-23
 */

#include "Initializer.h"
#include "AuthInitializer.h"
#include "ExecutorInitializer.h"
#include "LedgerInitializer.h"
#include "ParallelExecutor.h"
#include "SchedulerInitializer.h"
#include "StorageInitializer.h"
#include "bcos-framework/interfaces/executor/NativeExecutionMessage.h"
#include "bcos-framework/interfaces/executor/ParallelTransactionExecutorInterface.h"
#include "bcos-framework/interfaces/protocol/GlobalConfig.h"
#include "bcos-framework/interfaces/protocol/Protocol.h"
#include "bcos-framework/interfaces/protocol/ProtocolTypeDef.h"
#include "bcos-framework/interfaces/rpc/RPCInterface.h"
#include "bcos-protocol/TransactionSubmitResultFactoryImpl.h"
#include "bcos-protocol/TransactionSubmitResultImpl.h"
#include "bcos-scheduler/src/RemoteExecutorManager.h"
#include "bcos-tars-protocol/protocol/ExecutionMessageImpl.h"

#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework/interfaces/protocol/GlobalConfig.h>
#include <bcos-scheduler/src/SchedulerManager.h>
#include <bcos-sync/BlockSync.h>
#include <bcos-tars-protocol/client/GatewayServiceClient.h>
#include <bcos-tool/NodeConfig.h>
#include <bcos-tool/LedgerConfigFetcher.cpp>

using namespace bcos;
using namespace bcos::tool;
using namespace bcos::initializer;

void Initializer::initAirNode(std::string const& _configFilePath, std::string const& _genesisFile,
    bcos::gateway::GatewayInterface::Ptr _gateway)
{
    initConfig(_configFilePath, _genesisFile, "", true);
    init(bcos::protocol::NodeArchitectureType::AIR, _configFilePath, _genesisFile, _gateway, true);
}
void Initializer::initMicroServiceNode(bcos::protocol::NodeArchitectureType _nodeArchType,
    std::string const& _configFilePath, std::string const& _genesisFile,
    std::string const& _privateKeyPath)
{
    initConfig(_configFilePath, _genesisFile, _privateKeyPath, false);
    // get gateway client
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    auto gatewayPrx = Application::getCommunicator()->stringToProxy<bcostars::GatewayServicePrx>(
        m_nodeConfig->gatewayServiceName());
    auto gateWay = std::make_shared<bcostars::GatewayServiceClient>(
        gatewayPrx, m_nodeConfig->gatewayServiceName(), keyFactory);
    init(_nodeArchType, _configFilePath, _genesisFile, gateWay, false);
}

void Initializer::initConfig(std::string const& _configFilePath, std::string const& _genesisFile,
    std::string const& _privateKeyPath, bool _airVersion)
{
    m_nodeConfig = std::make_shared<NodeConfig>(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    m_nodeConfig->loadConfig(_configFilePath);
    m_nodeConfig->loadGenesisConfig(_genesisFile);

    // init the protocol
    m_protocolInitializer = std::make_shared<ProtocolInitializer>();
    m_protocolInitializer->init(m_nodeConfig);
    auto privateKeyPath = m_nodeConfig->privateKeyPath();
    if (!_airVersion)
    {
        privateKeyPath = _privateKeyPath;
    }
    m_protocolInitializer->loadKeyPair(privateKeyPath);
    boost::property_tree::ptree pt;
    boost::property_tree::read_ini(_configFilePath, pt);
    m_nodeConfig->loadNodeServiceConfig(
        m_protocolInitializer->keyPair()->publicKey()->hex(), pt, false);
    if (!_airVersion)
    {
        // load the service config
        m_nodeConfig->loadServiceConfig(pt);
    }
}

void Initializer::init(bcos::protocol::NodeArchitectureType _nodeArchType,
    std::string const& _configFilePath, std::string const& _genesisFile,
    bcos::gateway::GatewayInterface::Ptr _gateway, bool _airVersion)
{
    // build the front service
    m_frontServiceInitializer =
        std::make_shared<FrontServiceInitializer>(m_nodeConfig, m_protocolInitializer, _gateway);

    // build the storage
    auto storagePath = m_nodeConfig->storagePath();
    // build and init the pbft related modules
    auto consensusStoragePath =
        m_nodeConfig->storagePath() + c_fileSeparator + c_consensusStorageDBName;
    if (!_airVersion)
    {
        storagePath = ServerConfig::BasePath + ".." + c_fileSeparator + m_nodeConfig->groupId() +
                      c_fileSeparator + m_nodeConfig->storagePath();
        consensusStoragePath = ServerConfig::BasePath + ".." + c_fileSeparator +
                               m_nodeConfig->groupId() + c_fileSeparator + c_consensusStorageDBName;
    }
    INITIALIZER_LOG(INFO) << LOG_DESC("initNode") << LOG_KV("storagePath", storagePath)
                          << LOG_KV("storageType", m_nodeConfig->storageType())
                          << LOG_KV("consensusStoragePath", consensusStoragePath);
    bcos::storage::TransactionalStorageInterface::Ptr storage = nullptr;
    bcos::storage::TransactionalStorageInterface::Ptr schedulerStorage = nullptr;
    bcos::storage::TransactionalStorageInterface::Ptr consensusStorage = nullptr;
    if (boost::iequals(m_nodeConfig->storageType(), "RocksDB"))
    {
        // m_protocolInitializer->dataEncryption() will return nullptr when storage_security = false
        storage = StorageInitializer::build(
            storagePath, m_protocolInitializer->dataEncryption(), m_nodeConfig->keyPageSize());
        schedulerStorage = storage;
        consensusStorage = StorageInitializer::build(
            consensusStoragePath, m_protocolInitializer->dataEncryption());
    }
    else if (boost::iequals(m_nodeConfig->storageType(), "TiKV"))
    {
        storage = StorageInitializer::build(m_nodeConfig->pdAddrs());
        schedulerStorage = StorageInitializer::build(m_nodeConfig->pdAddrs());
        consensusStorage = StorageInitializer::build(m_nodeConfig->pdAddrs());
    }
    else
    {
        throw std::runtime_error("storage type not support");
    }

    // build ledger
    auto ledger =
        LedgerInitializer::build(m_protocolInitializer->blockFactory(), storage, m_nodeConfig);
    m_ledger = ledger;

    bcos::protocol::ExecutionMessageFactory::Ptr executionMessageFactory = nullptr;
    if (_nodeArchType == bcos::protocol::NodeArchitectureType::MAX)
    {
        executionMessageFactory =
            std::make_shared<bcostars::protocol::ExecutionMessageFactoryImpl>();
    }
    else
    {
        executionMessageFactory = std::make_shared<executor::NativeExecutionMessageFactory>();
    }
    auto executorManager = std::make_shared<bcos::scheduler::RemoteExecutorManager>(
        m_nodeConfig->executorServiceName());

    auto transactionSubmitResultFactory = std::make_shared<TransactionSubmitResultFactoryImpl>();

    auto factory = SchedulerInitializer::buildFactory(executorManager, ledger, schedulerStorage,
        executionMessageFactory, m_protocolInitializer->blockFactory(),
        m_protocolInitializer->txResultFactory(), m_protocolInitializer->cryptoSuite()->hashImpl(),
        m_nodeConfig->isAuthCheck(), m_nodeConfig->isWasm());

    int64_t schedulerSeq = 0;  // In Max node, this seq will be update after consensus module switch
                               // to a leader during startup
    m_scheduler =
        std::make_shared<bcos::scheduler::SchedulerManager>(schedulerSeq, factory, executorManager);

    // init the txpool
    m_txpoolInitializer = std::make_shared<TxPoolInitializer>(
        m_nodeConfig, m_protocolInitializer, m_frontServiceInitializer->front(), ledger);


    std::shared_ptr<bcos::storage::LRUStateStorage> cache = nullptr;
    if (m_nodeConfig->enableLRUCacheStorage())
    {
        cache = std::make_shared<bcos::storage::LRUStateStorage>(storage);
        cache->setMaxCapacity(m_nodeConfig->cacheSize());
        INITIALIZER_LOG(INFO) << "initNode: enableLRUCacheStorage, size: "
                              << m_nodeConfig->cacheSize();
    }
    else
    {
        INITIALIZER_LOG(INFO) << LOG_DESC("initNode: disableLRUCacheStorage");
    }

    if (_nodeArchType == bcos::protocol::NodeArchitectureType::MAX)
    {
        INITIALIZER_LOG(INFO) << LOG_DESC("connect executor")
                              << LOG_KV("nodeArchType", _nodeArchType);
        executorManager->start();
    }
    else
    {
        INITIALIZER_LOG(INFO) << LOG_DESC("create Executor")
                              << LOG_KV("nodeArchType", _nodeArchType);

        // Note: ensure that there has at least one executor before pbft/sync execute block

        std::string executorName = "executor-local";
        auto executorFactory = ExecutorInitializer::buildFactory(m_ledger,
            m_txpoolInitializer->txpool(), cache, storage, executionMessageFactory,
            m_protocolInitializer->cryptoSuite()->hashImpl(), m_nodeConfig->isWasm(),
            m_nodeConfig->isAuthCheck(), m_nodeConfig->keyPageSize(), executorName);
        auto parallelExecutor =
            std::make_shared<bcos::initializer::ParallelExecutor>(executorFactory);
        executorManager->addExecutor(executorName, parallelExecutor);
    }

    // build and init the pbft related modules
    if (_nodeArchType == NodeArchitectureType::AIR)
    {
        m_pbftInitializer = std::make_shared<PBFTInitializer>(_nodeArchType, m_nodeConfig,
            m_protocolInitializer, m_txpoolInitializer->txpool(), ledger, m_scheduler,
            consensusStorage, m_frontServiceInitializer->front());
        auto nodeID = m_protocolInitializer->keyPair()->publicKey();
        auto frontService = m_frontServiceInitializer->front();
        auto groupID = m_nodeConfig->groupId();
        auto blockSync =
            std::dynamic_pointer_cast<bcos::sync::BlockSync>(m_pbftInitializer->blockSync());

        auto nodeProtocolInfo = g_BCOSConfig.protocolInfo(ProtocolModuleID::NodeService);
        // registerNode when air node first start-up
        _gateway->registerNode(
            groupID, nodeID, blockSync->config()->nodeType(), frontService, nodeProtocolInfo);
        INITIALIZER_LOG(INFO) << LOG_DESC("registerNode") << LOG_KV("group", groupID)
                              << LOG_KV("node", nodeID->hex())
                              << LOG_KV("type", blockSync->config()->nodeType());
        // update the frontServiceInfo when nodeType changed
        blockSync->config()->registerOnNodeTypeChanged(
            [_gateway, groupID, nodeID, frontService, nodeProtocolInfo](NodeType _type) {
                _gateway->registerNode(groupID, nodeID, _type, frontService, nodeProtocolInfo);
                INITIALIZER_LOG(INFO) << LOG_DESC("registerNode") << LOG_KV("group", groupID)
                                      << LOG_KV("node", nodeID->hex()) << LOG_KV("type", _type);
            });
    }
    else
    {
        m_pbftInitializer = std::make_shared<ProPBFTInitializer>(_nodeArchType, m_nodeConfig,
            m_protocolInitializer, m_txpoolInitializer->txpool(), ledger, m_scheduler,
            consensusStorage, m_frontServiceInitializer->front());
    }

    // init the txpool
    m_txpoolInitializer->init(m_pbftInitializer->sealer());

    // Note: must init PBFT after txpool, in case of pbft calls txpool to verifyBlock before
    // txpool init finished
    m_pbftInitializer->init();

    // init the frontService
    m_frontServiceInitializer->init(
        m_pbftInitializer->pbft(), m_pbftInitializer->blockSync(), m_txpoolInitializer->txpool());
    initSysContract();
}

void Initializer::initNotificationHandlers(bcos::rpc::RPCInterface::Ptr _rpc)
{
    // init handlers
    auto nodeName = m_nodeConfig->nodeName();
    auto groupID = m_nodeConfig->groupId();
    auto schedulerFactory =
        dynamic_pointer_cast<scheduler::SchedulerManager>(m_scheduler)->getFactory();
    // notify blockNumber
    schedulerFactory->setBlockNumberReceiver(
        [_rpc, groupID, nodeName](bcos::protocol::BlockNumber number) {
            INITIALIZER_LOG(INFO) << "Notify blocknumber: " << number;
            // Note: the interface will notify blockNumber to all rpc nodes in pro/max mode
            _rpc->asyncNotifyBlockNumber(groupID, nodeName, number, [](bcos::Error::Ptr) {});
        });
    // notify transactions
    auto txpool = m_txpoolInitializer->txpool();
    schedulerFactory->setTransactionNotifier(
        [txpool](bcos::protocol::BlockNumber _blockNumber,
            bcos::protocol::TransactionSubmitResultsPtr _result,
            std::function<void(bcos::Error::Ptr)> _callback) {
            // only response to the requester
            txpool->asyncNotifyBlockResult(_blockNumber, _result, _callback);
        });
    m_pbftInitializer->initNotificationHandlers(_rpc);
}

void Initializer::initSysContract()
{
    if (!m_nodeConfig->isWasm() && m_nodeConfig->isAuthCheck())
    {
        // check is it deploy first time
        std::promise<std::tuple<Error::Ptr, protocol::BlockNumber>> getNumberPromise;
        m_ledger->asyncGetBlockNumber([&](Error::Ptr _error, protocol::BlockNumber _number) {
            getNumberPromise.set_value(std::make_tuple(std::move(_error), _number));
        });
        auto getNumberTuple = getNumberPromise.get_future().get();
        if (std::get<0>(getNumberTuple) != nullptr || std::get<1>(getNumberTuple) > 0)
        {
            return;
        }

        // add auth deploy func here
        AuthInitializer::init(0, m_protocolInitializer, m_nodeConfig, m_scheduler);
    }
}

void Initializer::start()
{
    if (m_txpoolInitializer)
    {
        m_txpoolInitializer->start();
    }
    if (m_pbftInitializer)
    {
        m_pbftInitializer->start();
    }

    if (m_frontServiceInitializer)
    {
        m_frontServiceInitializer->start();
    }
}

void Initializer::stop()
{
    try
    {
        if (m_frontServiceInitializer)
        {
            m_frontServiceInitializer->stop();
        }
        if (m_pbftInitializer)
        {
            m_pbftInitializer->stop();
        }
        if (m_txpoolInitializer)
        {
            m_txpoolInitializer->stop();
        }
    }
    catch (std::exception const& e)
    {
        std::cout << "stop bcos-node failed for " << boost::diagnostic_information(e);
        exit(-1);
    }
}