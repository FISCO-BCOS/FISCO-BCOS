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
#include "BfsInitializer.h"
#include "LedgerInitializer.h"
#include "SchedulerInitializer.h"
#include "StorageInitializer.h"
#include "bcos-executor/src/executor/SwitchExecutorManager.h"
#include "bcos-framework/storage/StorageInterface.h"
#include "bcos-scheduler/src/TarsExecutorManager.h"
#include "bcos-storage/RocksDBStorage.h"
#include "bcos-task/Wait.h"
#include "fisco-bcos-tars-service/Common/TarsUtils.h"
#include "libinitializer/BaselineSchedulerInitializer.h"
#include "libinitializer/ProPBFTInitializer.h"
#include <bcos-crypto/hasher/AnyHasher.h>
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework/executor/NativeExecutionMessage.h>
#include <bcos-framework/executor/ParallelTransactionExecutorInterface.h>
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-framework/protocol/GlobalConfig.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-framework/rpc/RPCInterface.h>
#include <bcos-protocol/TransactionSubmitResultFactoryImpl.h>
#include <bcos-protocol/TransactionSubmitResultImpl.h>
#include <bcos-scheduler/src/ExecutorManager.h>
#include <bcos-scheduler/src/SchedulerManager.h>
#include <bcos-sync/BlockSync.h>
#include <bcos-table/src/KeyPageStorage.h>
#include <bcos-table/src/StateStorageFactory.h>
#include <bcos-tars-protocol/client/GatewayServiceClient.h>
#include <bcos-tars-protocol/impl/TarsSerializable.h>
#include <bcos-tars-protocol/protocol/ExecutionMessageImpl.h>
#include <bcos-tool/LedgerConfigFetcher.h>
#include <bcos-tool/NodeConfig.h>
#include <bcos-tool/NodeTimeMaintenance.h>
#include <rocksdb/sst_file_reader.h>
#include <util/tc_clientsocket.h>
#include <boost/filesystem.hpp>
#include <memory>
#include <vector>

using namespace bcos;
using namespace bcos::tool;
using namespace bcos::protocol;
using namespace bcos::initializer;
namespace fs = boost::filesystem;

void Initializer::initAirNode(std::string const& _configFilePath, std::string const& _genesisFile,
    bcos::gateway::GatewayInterface::Ptr _gateway, const std::string& _logPath)
{
    initConfig(_configFilePath, _genesisFile, "", true);
    init(bcos::protocol::NodeArchitectureType::AIR, _configFilePath, _genesisFile, _gateway, true,
        _logPath);
}
void Initializer::initMicroServiceNode(bcos::protocol::NodeArchitectureType _nodeArchType,
    std::string const& _configFilePath, std::string const& _genesisFile,
    std::string const& _privateKeyPath, const std::string& _logPath)
{
    initConfig(_configFilePath, _genesisFile, _privateKeyPath, false);
    // get gateway client
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();

    auto gatewayServiceName = m_nodeConfig->gatewayServiceName();
    auto withoutTarsFramework = m_nodeConfig->withoutTarsFramework();

    std::vector<tars::TC_Endpoint> endPoints;
    m_nodeConfig->getTarsClientProxyEndpoints(bcos::protocol::GATEWAY_NAME, endPoints);

    auto gatewayPrx = bcostars::createServantProxy<bcostars::GatewayServicePrx>(
        withoutTarsFramework, gatewayServiceName, endPoints);

    auto gateWay = std::make_shared<bcostars::GatewayServiceClient>(
        gatewayPrx, m_nodeConfig->gatewayServiceName(), keyFactory);
    init(_nodeArchType, _configFilePath, _genesisFile, gateWay, false, _logPath);
}

void Initializer::initConfig(std::string const& _configFilePath, std::string const& _genesisFile,
    std::string const& _privateKeyPath, bool _airVersion)
{
    m_nodeConfig = std::make_shared<NodeConfig>(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    m_nodeConfig->loadGenesisConfig(_genesisFile);
    m_nodeConfig->loadConfig(_configFilePath);

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
    bcos::gateway::GatewayInterface::Ptr _gateway, bool _airVersion, const std::string& _logPath)
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
        storagePath = tars::ServerConfig::BasePath + ".." + c_fileSeparator +
                      m_nodeConfig->groupId() + c_fileSeparator + m_nodeConfig->storagePath();
        consensusStoragePath = tars::ServerConfig::BasePath + ".." + c_fileSeparator +
                               m_nodeConfig->groupId() + c_fileSeparator + c_consensusStorageDBName;
    }
    INITIALIZER_LOG(INFO) << LOG_DESC("initNode") << LOG_KV("storagePath", storagePath)
                          << LOG_KV("storageType", m_nodeConfig->storageType())
                          << LOG_KV("consensusStoragePath", consensusStoragePath);

    bcos::storage::TransactionalStorageInterface::Ptr schedulerStorage = nullptr;
    bcos::storage::TransactionalStorageInterface::Ptr consensusStorage = nullptr;
    bcos::storage::TransactionalStorageInterface::Ptr airExecutorStorage = nullptr;

    if (boost::iequals(m_nodeConfig->storageType(), "RocksDB"))
    {
        RocksDBOption option;
        option.maxWriteBufferNumber = m_nodeConfig->maxWriteBufferNumber();
        option.maxBackgroundJobs = m_nodeConfig->maxBackgroundJobs();
        option.writeBufferSize = m_nodeConfig->writeBufferSize();
        option.minWriteBufferNumberToMerge = m_nodeConfig->minWriteBufferNumberToMerge();
        option.blockCacheSize = m_nodeConfig->blockCacheSize();
        option.enable_blob_files = m_nodeConfig->enableRocksDBBlob();

        // m_protocolInitializer->dataEncryption() will return nullptr when storage_security = false
        m_storage =
            StorageInitializer::build(storagePath, option, m_protocolInitializer->dataEncryption(),
                m_nodeConfig->keyPageSize(), m_nodeConfig->enableStatistics());
        schedulerStorage = m_storage;
        consensusStorage = StorageInitializer::build(
            consensusStoragePath, option, m_protocolInitializer->dataEncryption(), 0);
        airExecutorStorage = m_storage;
    }
#ifdef WITH_TIKV
    else if (boost::iequals(m_nodeConfig->storageType(), "TiKV"))
    {
        m_storage = StorageInitializer::build(m_nodeConfig->pdAddrs(), _logPath,
            m_nodeConfig->pdCaPath(), m_nodeConfig->pdCertPath(), m_nodeConfig->pdKeyPath());
        if (_nodeArchType == bcos::protocol::NodeArchitectureType::MAX)
        {  // TODO: in max node, scheduler will use storage to commit but the ledger only use
           // storage to read, the storage which ledger use should not trigger the switch when the
           // scheduler is committing block
            schedulerStorage = StorageInitializer::build(m_nodeConfig->pdAddrs(), _logPath,
                m_nodeConfig->pdCaPath(), m_nodeConfig->pdCertPath(), m_nodeConfig->pdKeyPath());
            consensusStorage = m_storage;
            airExecutorStorage = m_storage;
        }
        else
        {  // in AIR/PRO node, scheduler and executor in one process so need different storage
            schedulerStorage = StorageInitializer::build(m_nodeConfig->pdAddrs(), _logPath,
                m_nodeConfig->pdCaPath(), m_nodeConfig->pdCertPath(), m_nodeConfig->pdKeyPath());
            consensusStorage = StorageInitializer::build(m_nodeConfig->pdAddrs(), _logPath,
                m_nodeConfig->pdCaPath(), m_nodeConfig->pdCertPath(), m_nodeConfig->pdKeyPath());
            airExecutorStorage = StorageInitializer::build(m_nodeConfig->pdAddrs(), _logPath,
                m_nodeConfig->pdCaPath(), m_nodeConfig->pdCertPath(), m_nodeConfig->pdKeyPath());
        }
    }
#endif
    else
    {
        throw std::runtime_error("storage type not support");
    }

    // build ledger
    auto ledger =
        LedgerInitializer::build(m_protocolInitializer->blockFactory(), m_storage, m_nodeConfig);
    ledger->setKeyPageSize(m_nodeConfig->keyPageSize());
    m_ledger = ledger;

    bcos::protocol::ExecutionMessageFactory::Ptr executionMessageFactory = nullptr;
    // Note: since tikv-storage store txs with transaction, batch writing is more efficient than
    // writing one by one
    if (_nodeArchType == bcos::protocol::NodeArchitectureType::MAX)
    {
        executionMessageFactory =
            std::make_shared<bcostars::protocol::ExecutionMessageFactoryImpl>();
    }
    else
    {
        executionMessageFactory = std::make_shared<executor::NativeExecutionMessageFactory>();
    }

    auto transactionSubmitResultFactory =
        std::make_shared<protocol::TransactionSubmitResultFactoryImpl>();

    // init the txpool
    m_txpoolInitializer = std::make_shared<TxPoolInitializer>(
        m_nodeConfig, m_protocolInitializer, m_frontServiceInitializer->front(), ledger);

    std::shared_ptr<bcos::scheduler::TarsExecutorManager> executorManager;  // Only use when

    auto useBaselineScheduler = m_nodeConfig->enableBaselineScheduler();
    if (useBaselineScheduler)
    {
        // auto hasher = m_protocolInitializer->cryptoSuite()->hashImpl()->hasher();
        bcos::executor::GlobalHashImpl::g_hashImpl =
            m_protocolInitializer->cryptoSuite()->hashImpl();
        // using Hasher = std::remove_cvref_t<decltype(hasher)>;
        auto existsRocksDB = std::dynamic_pointer_cast<storage::RocksDBStorage>(m_storage);

        auto baselineSchedulerConfig = m_nodeConfig->baselineSchedulerConfig();
        task::syncWait(transaction_scheduler::BaselineSchedulerInitializer::checkRequirements(
            *ledger, !m_nodeConfig->genesisConfig().m_isSerialExecute,
            m_nodeConfig->genesisConfig().m_isWasm));
        std::tie(m_baselineSchedulerHolder, m_setBaselineSchedulerBlockNumberNotifier) =
            transaction_scheduler::BaselineSchedulerInitializer::build(existsRocksDB->rocksDB(),
                m_protocolInitializer->blockFactory(), m_txpoolInitializer->txpool(),
                transactionSubmitResultFactory, ledger, baselineSchedulerConfig);
        m_scheduler = m_baselineSchedulerHolder();
    }
    else
    {
        executorManager = std::make_shared<bcos::scheduler::TarsExecutorManager>(
            m_nodeConfig->executorServiceName(), m_nodeConfig);
        auto factory = SchedulerInitializer::buildFactory(executorManager, ledger, schedulerStorage,
            executionMessageFactory, m_protocolInitializer->blockFactory(),
            m_txpoolInitializer->txpool(), m_protocolInitializer->txResultFactory(),
            m_protocolInitializer->cryptoSuite()->hashImpl(), m_nodeConfig->isAuthCheck(),
            m_nodeConfig->isWasm(), m_nodeConfig->isSerialExecute(), m_nodeConfig->keyPageSize());

        int64_t schedulerSeq = 0;  // In Max node, this seq will be update after consensus module
                                   // switch to a leader during startup
        m_scheduler = std::make_shared<bcos::scheduler::SchedulerManager>(
            schedulerSeq, factory, executorManager);
    }

    if (boost::iequals(m_nodeConfig->storageType(), "TiKV"))
    {
#ifdef WITH_TIKV
        std::weak_ptr<bcos::scheduler::SchedulerManager> schedulerWeakPtr =
            std::dynamic_pointer_cast<bcos::scheduler::SchedulerManager>(m_scheduler);
        auto switchHandler = [scheduler = schedulerWeakPtr]() {
            if (scheduler.lock())
            {
                scheduler.lock()->triggerSwitch();
            }
        };
        if (_nodeArchType != bcos::protocol::NodeArchitectureType::MAX)
        {
            dynamic_pointer_cast<bcos::storage::TiKVStorage>(airExecutorStorage)
                ->setSwitchHandler(switchHandler);
        }
        dynamic_pointer_cast<bcos::storage::TiKVStorage>(schedulerStorage)
            ->setSwitchHandler(switchHandler);
#endif
    }

    bcos::storage::CacheStorageFactory::Ptr cacheFactory = nullptr;
    if (m_nodeConfig->enableLRUCacheStorage())
    {
        cacheFactory = std::make_shared<bcos::storage::CacheStorageFactory>(
            m_storage, m_nodeConfig->cacheSize());
        INITIALIZER_LOG(INFO) << "initNode: enableLRUCacheStorage, size: "
                              << m_nodeConfig->cacheSize();
    }
    else
    {
        INITIALIZER_LOG(INFO) << LOG_DESC("initNode: disableLRUCacheStorage");
    }

    if (_nodeArchType == bcos::protocol::NodeArchitectureType::MAX)
    {
        INITIALIZER_LOG(INFO) << LOG_DESC("waiting for connect executor")
                              << LOG_KV("nodeArchType", _nodeArchType);
        executorManager->start();  // will waiting for connecting some executors

        // init scheduler
        dynamic_pointer_cast<scheduler::SchedulerManager>(m_scheduler)->initSchedulerIfNotExist();
    }
    else
    {
        INITIALIZER_LOG(INFO) << LOG_DESC("create Executor")
                              << LOG_KV("nodeArchType", _nodeArchType);

        // Note: ensure that there has at least one executor before pbft/sync execute block
        if (!useBaselineScheduler)
        {
            auto storageFactory =
                std::make_shared<storage::StateStorageFactory>(m_nodeConfig->keyPageSize());
            std::string executorName = "executor-local";
            auto executorFactory = std::make_shared<bcos::executor::TransactionExecutorFactory>(
                m_ledger, m_txpoolInitializer->txpool(), cacheFactory, airExecutorStorage,
                executionMessageFactory, storageFactory,
                m_protocolInitializer->cryptoSuite()->hashImpl(), m_nodeConfig->isWasm(),
                m_nodeConfig->vmCacheSize(), m_nodeConfig->isAuthCheck(), executorName);
            auto switchExecutorManager =
                std::make_shared<bcos::executor::SwitchExecutorManager>(executorFactory);
            executorManager->addExecutor(executorName, switchExecutorManager);
            m_switchExecutorManager = switchExecutorManager;
        }
    }

    // build node time synchronization tool
    auto nodeTimeMaintenance = std::make_shared<NodeTimeMaintenance>();

    // build and init the pbft related modules
    if (_nodeArchType == protocol::NodeArchitectureType::AIR)
    {
        m_pbftInitializer = std::make_shared<PBFTInitializer>(_nodeArchType, m_nodeConfig,
            m_protocolInitializer, m_txpoolInitializer->txpool(), ledger, m_scheduler,
            consensusStorage, m_frontServiceInitializer->front(), nodeTimeMaintenance);
        auto nodeID = m_protocolInitializer->keyPair()->publicKey();
        auto frontService = m_frontServiceInitializer->front();
        auto groupID = m_nodeConfig->groupId();
        auto blockSync =
            std::dynamic_pointer_cast<bcos::sync::BlockSync>(m_pbftInitializer->blockSync());

        auto nodeProtocolInfo = g_BCOSConfig.protocolInfo(protocol::ProtocolModuleID::NodeService);
        // registerNode when air node first start-up
        _gateway->registerNode(
            groupID, nodeID, blockSync->config()->nodeType(), frontService, nodeProtocolInfo);
        INITIALIZER_LOG(INFO) << LOG_DESC("registerNode") << LOG_KV("group", groupID)
                              << LOG_KV("node", nodeID->hex())
                              << LOG_KV("type", blockSync->config()->nodeType());
        // update the frontServiceInfo when nodeType changed
        blockSync->config()->registerOnNodeTypeChanged(
            [_gateway, groupID, nodeID, frontService, nodeProtocolInfo](protocol::NodeType _type) {
                _gateway->registerNode(groupID, nodeID, _type, frontService, nodeProtocolInfo);
                INITIALIZER_LOG(INFO) << LOG_DESC("registerNode") << LOG_KV("group", groupID)
                                      << LOG_KV("node", nodeID->hex()) << LOG_KV("type", _type);
            });
    }
    else
    {
        m_pbftInitializer = std::make_shared<ProPBFTInitializer>(_nodeArchType, m_nodeConfig,
            m_protocolInitializer, m_txpoolInitializer->txpool(), ledger, m_scheduler,
            consensusStorage, m_frontServiceInitializer->front(), nodeTimeMaintenance);
    }
    if (_nodeArchType == bcos::protocol::NodeArchitectureType::MAX)
    {
        INITIALIZER_LOG(INFO) << LOG_DESC("Register switch handler in scheduler manager");
        // PBFT and scheduler are in the same process here, we just cast m_scheduler to
        // SchedulerService
        auto schedulerServer =
            std::dynamic_pointer_cast<bcos::scheduler::SchedulerManager>(m_scheduler);
        auto consensus = m_pbftInitializer->pbft();
        schedulerServer->registerOnSwitchTermHandler([consensus](
                                                         bcos::protocol::BlockNumber blockNumber) {
            INITIALIZER_LOG(DEBUG)
                << LOG_BADGE("Switch")
                << "Receive scheduler switch term notify of number " + std::to_string(blockNumber);
            consensus->clearExceptionProposalState(blockNumber);
        });
    }
    // init the txpool
    m_txpoolInitializer->init(m_pbftInitializer->sealer());

    // Note: must init PBFT after txpool, in case of pbft calls txpool to verifyBlock before
    // txpool init finished
    m_pbftInitializer->init();

    // init the frontService
    m_frontServiceInitializer->init(
        m_pbftInitializer->pbft(), m_pbftInitializer->blockSync(), m_txpoolInitializer->txpool());

    if (m_nodeConfig->enableArchive())
    {
        INITIALIZER_LOG(INFO) << LOG_BADGE("create archive service");
        m_archiveService = std::make_shared<bcos::archive::ArchiveService>(
            m_storage, ledger, m_nodeConfig->archiveListenIP(), m_nodeConfig->archiveListenPort());
    }
#ifdef WITH_LIGHTNODE
    bcos::storage::StorageImpl<bcos::storage::StorageInterface::Ptr> storageWrapper(m_storage);

    auto hasher = m_protocolInitializer->cryptoSuite()->hashImpl()->hasher();
    using Hasher = std::remove_cvref_t<decltype(hasher)>;
    auto lightNodeLedger =
        std::make_shared<bcos::ledger::LedgerImpl<Hasher, decltype(storageWrapper)>>(hasher.clone(),
            std::move(storageWrapper), m_protocolInitializer->blockFactory(), m_storage,
            m_nodeConfig->blockLimit());
    lightNodeLedger->setKeyPageSize(m_nodeConfig->keyPageSize());

    auto txpool = m_txpoolInitializer->txpool();
    auto transactionPool =
        std::make_shared<bcos::transaction_pool::TransactionPoolImpl<decltype(txpool)>>(
            m_protocolInitializer->cryptoSuite(), txpool);
    auto scheduler = std::make_shared<bcos::scheduler::SchedulerWrapperImpl<
        std::shared_ptr<bcos::scheduler::SchedulerInterface>>>(
        m_scheduler, m_protocolInitializer->cryptoSuite());

    m_lightNodeInitializer = std::make_shared<LightNodeInitializer>();
    m_lightNodeInitializer->initLedgerServer(
        std::dynamic_pointer_cast<bcos::front::FrontService>(m_frontServiceInitializer->front()),
        lightNodeLedger, transactionPool, scheduler);
#endif
}

void Initializer::initNotificationHandlers(bcos::rpc::RPCInterface::Ptr _rpc)
{
    // init handlers
    auto nodeName = m_nodeConfig->nodeName();
    auto groupID = m_nodeConfig->groupId();

    auto useBaselineScheduler = m_nodeConfig->enableBaselineScheduler();
    if (!useBaselineScheduler)
    {
        auto schedulerFactory =
            dynamic_pointer_cast<scheduler::SchedulerManager>(m_scheduler)->getFactory();
        // notify blockNumber
        schedulerFactory->setBlockNumberReceiver(
            [_rpc, groupID, nodeName](bcos::protocol::BlockNumber number) {
                INITIALIZER_LOG(DEBUG) << "Notify blocknumber: " << number;
                // Note: the interface will notify blockNumber to all rpc nodes in pro/max mode
                _rpc->asyncNotifyBlockNumber(groupID, nodeName, number, [](bcos::Error::Ptr) {});
            });
        // notify transactions
        schedulerFactory->setTransactionNotifier(
            [txpool = m_txpoolInitializer->txpool()](bcos::protocol::BlockNumber _blockNumber,
                bcos::protocol::TransactionSubmitResultsPtr _result,
                std::function<void(bcos::Error::Ptr)> _callback) {
                // only response to the requester
                txpool->asyncNotifyBlockResult(
                    _blockNumber, std::move(_result), std::move(_callback));
            });
    }
    else
    {
        m_setBaselineSchedulerBlockNumberNotifier(
            [_rpc, groupID, nodeName](bcos::protocol::BlockNumber number) {
                INITIALIZER_LOG(DEBUG) << "Notify blocknumber: " << number;
                // Note: the interface will notify blockNumber to all rpc nodes in pro/max mode
                _rpc->asyncNotifyBlockNumber(groupID, nodeName, number, [](bcos::Error::Ptr) {});
            });
    }

    m_pbftInitializer->initNotificationHandlers(_rpc);
}

void Initializer::initSysContract()
{
    // check is it deploy first time
    std::promise<std::tuple<Error::Ptr, protocol::BlockNumber>> getNumberPromise;
    m_ledger->asyncGetBlockNumber([&](Error::Ptr _error, protocol::BlockNumber _number) {
        getNumberPromise.set_value(std::make_tuple(std::move(_error), _number));
    });
    auto getNumberTuple = getNumberPromise.get_future().get();
    if (std::get<0>(getNumberTuple) != nullptr ||
        std::get<1>(getNumberTuple) > SYS_CONTRACT_DEPLOY_NUMBER)
    {
        return;
    }
    auto block = m_protocolInitializer->blockFactory()->createBlock();
    block->blockHeader()->setNumber(SYS_CONTRACT_DEPLOY_NUMBER);
    block->blockHeader()->setVersion(m_nodeConfig->compatibilityVersion());
    block->blockHeader()->calculateHash(
        *m_protocolInitializer->blockFactory()->cryptoSuite()->hashImpl());

    if (m_nodeConfig->compatibilityVersion() >= static_cast<uint32_t>(BlockVersion::V3_1_VERSION))
    {
        BfsInitializer::init(
            SYS_CONTRACT_DEPLOY_NUMBER, m_protocolInitializer, m_nodeConfig, block);
    }

    if ((!m_nodeConfig->isWasm() && m_nodeConfig->isAuthCheck()) ||
        versionCompareTo(m_nodeConfig->compatibilityVersion(), BlockVersion::V3_3_VERSION) >= 0)
    {
        // add auth deploy func here
        AuthInitializer::init(
            SYS_CONTRACT_DEPLOY_NUMBER, m_protocolInitializer, m_nodeConfig, block);
    }


    if (block->transactionsSize() > 0) [[likely]]
    {
        std::promise<std::tuple<bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr>> executedHeader;
        m_scheduler->executeBlock(block, false,
            [&](bcos::Error::Ptr&& _error, bcos::protocol::BlockHeader::Ptr&& _header, bool) {
                if (_error)
                {
                    executedHeader.set_value({std::move(_error), nullptr});
                    return;
                }
                INITIALIZER_LOG(INFO)
                    << LOG_BADGE("SysInitializer") << LOG_DESC("scheduler execute block success!")
                    << LOG_KV("blockHash", block->blockHeader()->hash().hex());
                executedHeader.set_value({nullptr, std::move(_header)});
            });
        auto [executeError, header] = executedHeader.get_future().get();
        if (executeError || header == nullptr) [[unlikely]]
        {
            std::stringstream errorMessage("SysInitializer: scheduler executeBlock failed");
            int64_t errorCode = -1;
            if (executeError) [[likely]]
            {
                errorMessage << executeError->errorMessage();
                errorCode = executeError->errorCode();
            }
            INITIALIZER_LOG(ERROR)
                << LOG_BADGE("SysInitializer") << LOG_DESC("scheduler execute block failed")
                << LOG_KV("msg", errorMessage.str());
            BOOST_THROW_EXCEPTION(BCOS_ERROR(errorCode, errorMessage.str()));
        }

        std::promise<std::tuple<Error::Ptr, bcos::ledger::LedgerConfig::Ptr>> committedConfig;
        m_scheduler->commitBlock(
            header, [&](Error::Ptr&& _error, bcos::ledger::LedgerConfig::Ptr&& _config) {
                if (_error)
                {
                    INITIALIZER_LOG(ERROR)
                        << LOG_BADGE("SysInitializer") << LOG_KV("msg", _error->errorMessage());
                    committedConfig.set_value(std::make_tuple(std::move(_error), nullptr));
                    return;
                }
                committedConfig.set_value(std::make_tuple(nullptr, std::move(_config)));
            });
        auto [error, newConfig] = committedConfig.get_future().get();
        if (error != nullptr || newConfig->blockNumber() != SYS_CONTRACT_DEPLOY_NUMBER)
        {
            INITIALIZER_LOG(ERROR)
                << LOG_BADGE("SysInitializer") << LOG_DESC("Error in commitBlock")
                << (error ? "errorMsg" + error->errorMessage() : "")
                << LOG_KV("configNumber", newConfig->blockNumber());
            BOOST_THROW_EXCEPTION(BCOS_ERROR(-1, "SysInitializer commitBlock failed"));
        }
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
    if (m_archiveService)
    {
        m_archiveService->start();
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
        if (m_scheduler)
        {
            m_scheduler->stop();
        }
        if (m_archiveService)
        {
            m_archiveService->stop();
        }
    }
    catch (std::exception const& e)
    {
        std::cout << "stop bcos-node failed for " << boost::diagnostic_information(e);
        exit(-1);
    }
}


protocol::BlockNumber Initializer::getCurrentBlockNumber()
{
    std::promise<protocol::BlockNumber> blockNumberFuture;
    m_ledger->asyncGetBlockNumber(
        [&blockNumberFuture](Error::Ptr error, protocol::BlockNumber number) {
            if (error)
            {
                INITIALIZER_LOG(ERROR)
                    << LOG_DESC("get block number failed") << LOG_DESC(error->errorMessage());
                blockNumberFuture.set_value(0);
            }
            else
            {
                blockNumberFuture.set_value(number);
            }
        });
    return blockNumberFuture.get_future().get();
}

void Initializer::prune()
{
    auto blockLimit = (protocol::BlockNumber)m_nodeConfig->blockLimit();
    bcos::protocol::BlockNumber currentBlockNumber = getCurrentBlockNumber();

    if (currentBlockNumber <= blockLimit)
    {
        return;
    }
    auto endBlockNumber = currentBlockNumber - blockLimit;
    for (bcos::protocol::BlockNumber i = blockLimit + 1; i < endBlockNumber; i++)
    {
        m_ledger->removeExpiredNonce(i, true);
        if (i % 1000 == 0 || i == endBlockNumber)
        {
            std::cout << "removed nonces of block " << i << "\r";
        }
    }
    std::cout << std::endl;
    // rocksDB compaction
    if (boost::iequals("rocksdb", m_nodeConfig->storageType()))
    {
        auto storage = std::dynamic_pointer_cast<storage::RocksDBStorage>(m_storage);
        auto& rocksDB = storage->rocksDB();
        auto startKey = rocksdb::Slice(bcos::storage::toDBKey(
            bcos::ledger::SYS_BLOCK_NUMBER_2_NONCES, std::to_string(blockLimit + 1)));
        auto endKey = rocksdb::Slice(bcos::storage::toDBKey(
            bcos::ledger::SYS_BLOCK_NUMBER_2_NONCES, std::to_string(endBlockNumber)));
        auto status = rocksDB.CompactRange(rocksdb::CompactRangeOptions(), &startKey, &endKey);
        if (!status.ok())
        {
            std::cerr << LOG_DESC("rocksDB compact range failed") << LOG_DESC(status.ToString());
        }
        std::cout << "rocksDB compact range success" << std::endl;
    }
}

rocksdb::DB* createReadOnlyRocksDB(const std::string& path)
{
    rocksdb::Options options;
    options.create_if_missing = false;
    rocksdb::DB* db = nullptr;
    rocksdb::Status status = rocksdb::DB::OpenForReadOnly(options, path, &db);
    if (!status.ok())
    {
        std::cout << "open read only rocksDB failed: " << status.ToString() << std::endl;
        return nullptr;
    }
    return db;
}

fs::path getSstFileName(const std::string& path, size_t index)
{
    const size_t SST_NUMBER_LENGTH = 6;
    // return fs::path(std::format("{}/{:#06d}.sst", path, index));
    std::stringstream ss;
    ss << path << "/" << std::setw(SST_NUMBER_LENGTH) << std::setfill('0') << index << ".sst";
    return {ss.str()};
}

bcos::Error::Ptr Initializer::generateSnapshot(
    const std::string& snapshotPath, bool withTxAndReceipts)
{
    if (!boost::iequals(m_nodeConfig->storageType(), "RocksDB"))
    {  // TODO: support TiKV
        std::cerr << "only support RocksDB storage" << std::endl;
        return BCOS_ERROR_PTR(-1, "only support RocksDB storage");
    }
    auto rockDBPath = m_nodeConfig->storagePath();
    return generateSnapshotFromRocksDB(rockDBPath, snapshotPath, withTxAndReceipts);
}

bcos::Error::Ptr Initializer::generateSnapshotFromRocksDB(const std::string& rockDBPath,
    const std::string& snapshotPath, bool withTxAndReceipts, size_t snapshotFileSize)
{
    using namespace rocksdb;

    if (!fs::exists(rockDBPath))
    {
        std::cerr << "rocksDB path " << rockDBPath << " does not exist" << std::endl;
        return BCOS_ERROR_PTR(-1, rockDBPath + " does not exist");
    }

    DB* db = createReadOnlyRocksDB(rockDBPath);
    if (db == nullptr)
    {
        std::cerr << "open readonly rocksDB failed" << std::endl;
        return BCOS_ERROR_PTR(-1, "open readonly rocksDB failed");
    }
    fs::path sstPath = snapshotPath + "/snapshot";
    if (!fs::exists(sstPath))
    {  // create directory
        if (!fs::create_directories(sstPath))
        {
            std::cerr << "failed to create directory " << sstPath << std::endl;
            return BCOS_ERROR_PTR(-1, "failed to create directory " + sstPath.string());
        }
    }
    else
    {
        if (!fs::is_directory(sstPath))
        {
            std::cerr << sstPath << " exists but not a directory" << std::endl;
            return BCOS_ERROR_PTR(-1, sstPath.string() + " exists but is not a directory");
        }
        if (!fs::is_empty(sstPath))
        {  // check sstPath is empty
            std::cerr << "Path of sst file is not empty" << std::endl;
            return BCOS_ERROR_PTR(-1, sstPath.string() + " is not empty");
        }
    }
    rocksdb::Options options;
    options.compression = rocksdb::kZSTD;
    auto sstFileWriter = rocksdb::SstFileWriter(rocksdb::EnvOptions(), options);
    size_t sstIndex = 0;
    auto sstFileName = getSstFileName(sstPath.string(), sstIndex);
    rocksdb::Status status = sstFileWriter.Open(sstFileName.string());
    if (!status.ok())
    {
        std::cerr << "open file " << sstFileName << " failed, reason: " << status.ToString()
                  << std::endl;
        return BCOS_ERROR_PTR(
            -1, "open file " + sstFileName.string() + " failed , reason: " + status.ToString());
    }
    ReadOptions readOptions;
    readOptions.snapshot = db->GetSnapshot();
    std::unique_ptr<Iterator> it(db->NewIterator(readOptions));
    std::vector<ExternalSstFileInfo> sstFiles;
    std::vector<fs::path> sstFileList;
    auto blockLimit = (protocol::BlockNumber)m_nodeConfig->blockLimit();
    bcos::protocol::BlockNumber currentBlockNumber = getCurrentBlockNumber();
    std::cout << "current block number: " << currentBlockNumber << std::endl;
    auto nonceStartNumber = currentBlockNumber > blockLimit ? currentBlockNumber - blockLimit : 0;
    auto validNonceStartKey = bcos::storage::toDBKey(
        bcos::ledger::SYS_BLOCK_NUMBER_2_NONCES, std::to_string(blockLimit + 1));
    auto validNonceEndKey = bcos::storage::toDBKey(
        bcos::ledger::SYS_BLOCK_NUMBER_2_NONCES, std::to_string(currentBlockNumber));
    for (it->SeekToFirst(); it->Valid(); it->Next())
    {
        if (it->key().starts_with("s_block_number_2_nonces"))
        {
            if (it->key().compare(validNonceStartKey) < 0)
            {
                continue;
            }
        }
        if (!withTxAndReceipts &&
            (it->key().starts_with("s_hash_2_receipt") || it->key().starts_with("s_hash_2_tx")))
        {
            continue;
        }
        Status s = sstFileWriter.Put(it->key(), it->value());
        if (!s.ok())
        {
            std::cerr << "Error while adding Key: " << it->key().ToString()
                      << ", Error: " << s.ToString() << std::endl;
            return BCOS_ERROR_PTR(
                -1, "Error while adding Key: " + it->key().ToString() + ", Error: " + s.ToString());
        }
        const size_t MAX_SST_FILE_BYTE = snapshotFileSize * 1024 * 1024;
        if (sstFileWriter.FileSize() >= MAX_SST_FILE_BYTE)
        {
            sstFiles.emplace_back();
            std::cout << sstFiles.size() << " Finishing file " << sstFileName << std::endl;
            s = sstFileWriter.Finish(&sstFiles.back());
            if (!s.ok())
            {
                std::cout << "Error while finishing file " << sstFileName
                          << ", Error: " << s.ToString() << std::endl;
                return BCOS_ERROR_PTR(-1, "Error while finishing file " + sstFileName.string() +
                                              ", Error: " + s.ToString());
            }
            sstFileList.emplace_back(sstFiles.back().file_path);
            ++sstIndex;
            sstFileName = getSstFileName(sstPath.string(), sstIndex);
            s = sstFileWriter.Open(sstFileName.string());
            if (!s.ok())
            {
                std::cout << "Error while opening file " << sstFileName
                          << ", Error: " << s.ToString() << std::endl;
                return BCOS_ERROR_PTR(-1, "Error while opening file " + sstFileName.string() +
                                              ", Error: " + s.ToString());
            }
        }
    }
    if (sstFileWriter.FileSize() > 0)
    {
        sstFiles.emplace_back();
        std::cout << sstFiles.size() << " Finishing file " << sstFileName << std::endl;
        auto status = sstFileWriter.Finish(&sstFiles.back());
        if (!status.ok())
        {
            std::cout << "Error while finishing file " << sstFileName
                      << ", Error: " << status.ToString() << std::endl;
            return BCOS_ERROR_PTR(-1, "Error while finishing file " + sstFileName.string() +
                                          ", Error: " + status.ToString());
        }
        sstFileList.emplace_back(sstFiles.back().file_path);
    }
    // write max index to meta
    std::ofstream metaFile(sstPath.string() + "/meta");
    if (!metaFile.is_open())
    {
        std::cerr << "Failed to open meta file" << std::endl;
        return BCOS_ERROR_PTR(-1, "Failed to open meta file");
    }
    metaFile << sstIndex;
    metaFile.close();
    db->ReleaseSnapshot(readOptions.snapshot);
    delete db;
    return nullptr;
}

bcos::Error::Ptr Initializer::importSnapshot(const std::string& snapshotPath)
{
    if (!boost::iequals(m_nodeConfig->storageType(), "RocksDB"))
    {  // TODO: support TiKV
        std::cerr << "only support RocksDB storage" << std::endl;
        return BCOS_ERROR_PTR(-1, "only support RocksDB storage");
    }
    return importSnapshotToRocksDB(snapshotPath, m_nodeConfig->storagePath());
}

bcos::Error::Ptr Initializer::importSnapshotToRocksDB(
    const std::string& snapshotPath, const std::string& rockDBPath)
{
    // check snapshot file and meta file
    fs::path sstPath = snapshotPath;
    if (!fs::exists(sstPath))
    {
        std::cerr << "snapshot path " << sstPath << " does not exist" << std::endl;
        return BCOS_ERROR_PTR(-1, sstPath.string() + " does not exist");
    }
    // read meta file
    std::ifstream metaFile(sstPath.string() + "/meta");
    if (!metaFile.is_open())
    {
        std::cerr << "Failed to open meta file" << std::endl;
        return BCOS_ERROR_PTR(-1, "Failed to open meta file");
    }
    size_t sstIndex = 0;
    metaFile >> sstIndex;
    metaFile.close();
    rocksdb::Options options;
    options.compression = rocksdb::kZSTD;
    auto sstFileReader = rocksdb::SstFileReader(options);
    std::vector<std::string> sstFiles;

    for (size_t i = 0; i <= sstIndex; ++i)
    {
        auto sstFileName = getSstFileName(sstPath.string(), i);
        auto status = sstFileReader.Open(sstFileName.string());
        if (!status.ok())
        {
            std::cerr << "open file " << sstFileName << " failed, reason: " << status.ToString()
                      << std::endl;
            return BCOS_ERROR_PTR(
                -1, "open file " + sstFileName.string() + " failed , reason: " + status.ToString());
        }
        status = sstFileReader.VerifyChecksum();
        if (!status.ok())
        {
            std::cerr << "verify file " << sstFileName << " failed, reason: " << status.ToString()
                      << std::endl;
            return BCOS_ERROR_PTR(-1,
                "verify file " + sstFileName.string() + " failed , reason: " + status.ToString());
        }
        sstFiles.emplace_back(sstFileName.string());
    }
    std::cout << "check sst files success, ingest sst files" << std::endl;
    auto storage = std::dynamic_pointer_cast<storage::RocksDBStorage>(m_storage);
    auto& rocksDB = storage->rocksDB();

    rocksdb::IngestExternalFileOptions info;
    info.move_files = true;
    // Ingest SST files into the DB
    rocksdb::Status status = rocksDB.IngestExternalFile(sstFiles, info);
    if (!status.ok())
    {
        std::cerr << "Error while adding file, " << status.ToString() << std::endl;
        return BCOS_ERROR_PTR(-1, "Error while adding file, " + status.ToString());
    }

    std::cout << "current block number: " << getCurrentBlockNumber() << std::endl;
    return nullptr;
}
