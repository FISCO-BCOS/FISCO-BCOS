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
#include "bcos-utilities/Error.h"
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
#include <rocksdb/slice.h>
#include <rocksdb/sst_file_reader.h>
#include <util/tc_clientsocket.h>
#include <boost/filesystem.hpp>
#include <cstddef>
#include <memory>
#include <string>
#include <toml++/toml.hpp>
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

RocksDBOption getRocksDBOption(
    const tool::NodeConfig::Ptr& nodeConfig, bool optimizeLevelStyleCompaction = false)
{
    RocksDBOption option;
    option.maxWriteBufferNumber = nodeConfig->maxWriteBufferNumber();
    option.maxBackgroundJobs = nodeConfig->maxBackgroundJobs();
    option.writeBufferSize = nodeConfig->writeBufferSize();
    option.minWriteBufferNumberToMerge = nodeConfig->minWriteBufferNumberToMerge();
    option.blockCacheSize = nodeConfig->blockCacheSize();
    option.optimizeLevelStyleCompaction = optimizeLevelStyleCompaction;
    option.enable_blob_files = nodeConfig->enableRocksDBBlob();
    return option;
}

void Initializer::init(bcos::protocol::NodeArchitectureType _nodeArchType,
    std::string const& _configFilePath, std::string const& _genesisFile,
    bcos::gateway::GatewayInterface::Ptr _gateway, bool _airVersion, const std::string& _logPath)
{
    // build the front service
    m_frontServiceInitializer =
        std::make_shared<FrontServiceInitializer>(m_nodeConfig, m_protocolInitializer, _gateway);

    // build the storage
    auto stateDBPath = getStateDBPath(_airVersion);
    auto blockDBPath = getBlockDBPath(_airVersion);
    // build and init the pbft related modules
    auto consensusStoragePath = getConsensusStorageDBPath(_airVersion);
    INITIALIZER_LOG(INFO) << LOG_DESC("initNode") << LOG_KV("stateDBPath", stateDBPath)
                          << LOG_KV("enableSeparateBlockAndState",
                                 m_nodeConfig->enableSeparateBlockAndState())
                          << LOG_KV("storageType", m_nodeConfig->storageType())
                          << LOG_KV("consensusStoragePath", consensusStoragePath);

    bcos::storage::TransactionalStorageInterface::Ptr schedulerStorage = nullptr;
    bcos::storage::TransactionalStorageInterface::Ptr consensusStorage = nullptr;
    bcos::storage::TransactionalStorageInterface::Ptr airExecutorStorage = nullptr;

    if (boost::iequals(m_nodeConfig->storageType(), "RocksDB"))
    {
        RocksDBOption option = getRocksDBOption(m_nodeConfig);

        // m_protocolInitializer->dataEncryption() will return nullptr when storage_security = false
        m_storage = StorageInitializer::build(
            StorageInitializer::createRocksDB(
                stateDBPath, option, m_nodeConfig->enableStatistics(), m_nodeConfig->keyPageSize()),
            m_protocolInitializer->dataEncryption());
        schedulerStorage = m_storage;
        consensusStorage =
            StorageInitializer::build(StorageInitializer::createRocksDB(consensusStoragePath,
                                          option, m_nodeConfig->enableStatistics()),
                m_protocolInitializer->dataEncryption());
        airExecutorStorage = m_storage;
        if (m_nodeConfig->enableSeparateBlockAndState())
        {
            m_blockStorage =
                StorageInitializer::build(StorageInitializer::createRocksDB(blockDBPath, option,
                                              m_nodeConfig->enableStatistics()),
                    m_protocolInitializer->dataEncryption());
        }
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
    auto ledger = LedgerInitializer::build(
        m_protocolInitializer->blockFactory(), m_storage, m_nodeConfig, m_blockStorage);
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
        m_archiveService = std::make_shared<bcos::archive::ArchiveService>(m_storage, ledger,
            m_blockStorage, m_nodeConfig->archiveListenIP(), m_nodeConfig->archiveListenPort());
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


protocol::BlockNumber Initializer::getCurrentBlockNumber(
    bcos::storage::TransactionalStorageInterface::Ptr _storage)
{
    auto storage = _storage ? _storage : m_storage;
    std::promise<protocol::BlockNumber> blockNumberFuture;
    storage->asyncGetRow(ledger::SYS_CURRENT_STATE, ledger::SYS_KEY_CURRENT_NUMBER,
        [&blockNumberFuture](Error::Ptr error, std::optional<bcos::storage::Entry>&& entry) {
            if (error)
            {
                INITIALIZER_LOG(ERROR)
                    << LOG_DESC("get block number failed") << LOG_DESC(error->errorMessage());
                blockNumberFuture.set_value(0);
            }
            else
            {
                try
                {
                    auto blockNumber =
                        boost::lexical_cast<bcos::protocol::BlockNumber>(entry->getField(0));
                    blockNumberFuture.set_value(blockNumber);
                }
                catch (boost::bad_lexical_cast& e)
                {
                    // Ignore the exception
                    LEDGER_LOG(INFO)
                        << "Cast blockNumber failed, may be empty, set to default value 0"
                        << LOG_KV("blockNumber str", entry->getField(0));
                    blockNumberFuture.set_value(0);
                }
            }
        });
    return blockNumberFuture.get_future().get();
    // std::promise<protocol::BlockNumber> blockNumberFuture;
    // m_ledger->asyncGetBlockNumber(
    //     [&blockNumberFuture](Error::Ptr error, protocol::BlockNumber number) {
    //         if (error)
    //         {
    //             INITIALIZER_LOG(ERROR)
    //                 << LOG_DESC("get block number failed") << LOG_DESC(error->errorMessage());
    //             blockNumberFuture.set_value(0);
    //         }
    //         else
    //         {
    //             blockNumberFuture.set_value(number);
    //         }
    //     });
    // return blockNumberFuture.get_future().get();
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

std::unique_ptr<rocksdb::DB> createReadOnlyRocksDB(const std::string& path)
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
    return std::unique_ptr<rocksdb::DB>(db);
}

fs::path getSstFileName(const std::string& path, size_t index)
{
    const size_t SST_NUMBER_LENGTH = 6;
    // return fs::path(std::format("{}/{:#06d}.sst", path, index));
    std::stringstream ss;
    ss << path << "/" << std::setw(SST_NUMBER_LENGTH) << std::setfill('0') << index << ".sst";
    return {ss.str()};
}

bcos::Error::Ptr checkOrCreateDir(const fs::path& dir)
{
    if (!fs::exists(dir))
    {  // create directory
        if (!fs::create_directories(dir))
        {
            std::cerr << "failed to create directory " << dir << std::endl;
            return BCOS_ERROR_PTR(-1, "failed to create directory " + dir.string());
        }
    }
    else
    {
        if (!fs::is_directory(dir))
        {
            std::cerr << dir << " exists but not a directory" << std::endl;
            return BCOS_ERROR_PTR(-1, dir.string() + " exists but is not a directory");
        }
        if (!fs::is_empty(dir))
        {  // check sstPath is empty
            std::cerr << dir << " is not empty, please specific an empty directory" << std::endl;
            return BCOS_ERROR_PTR(-1, dir.string() + " is not empty");
        }
    }
    return nullptr;
}

bcos::Error::Ptr Initializer::generateSnapshot(const std::string& snapshotPath,
    bool withTxAndReceipts, const tool::NodeConfig::Ptr& nodeConfig)
{
    if (!boost::iequals(nodeConfig->storageType(), "RocksDB"))
    {  // TODO: support TiKV
        std::cerr << "only support RocksDB storage" << std::endl;
        return BCOS_ERROR_PTR(-1, "only support RocksDB storage");
    }
    auto separatedBlockAndState = nodeConfig->enableSeparateBlockAndState();

    auto stateDBPath = nodeConfig->storagePath();
    if (separatedBlockAndState)
    {
        stateDBPath = nodeConfig->stateDBPath();
    }
    auto blockDBPath = nodeConfig->blockDBPath();
    auto snapshotRoot = snapshotPath + "/snapshot";
    fs::path stateSstPath = snapshotRoot + "/state";
    fs::path blockSstPath = snapshotRoot + "/block";
    auto err = checkOrCreateDir(stateSstPath);
    if (err)
    {
        return err;
    }
    err = checkOrCreateDir(blockSstPath);
    if (err)
    {
        return err;
    }
    // write info to meta, meta file is toml format
    std::ofstream metaFile(snapshotRoot + "/meta");
    if (!metaFile.is_open())
    {
        std::cerr << "Failed to open meta file" << std::endl;
        return BCOS_ERROR_PTR(-1, "Failed to open meta file");
    }
    metaFile << "snapshot.withTxAndReceipts = " << withTxAndReceipts << std::endl;
    metaFile << "snapshot.separatedBlockAndState = " << nodeConfig->enableSeparateBlockAndState()
             << std::endl;

    auto db = createReadOnlyRocksDB(stateDBPath);
    if (!db)
    {
        return BCOS_ERROR_PTR(-1, "open rocksDB failed");
    }
    auto stateStorage =
        StorageInitializer::build(std::move(db), m_protocolInitializer->dataEncryption());
    auto blockLimit = (protocol::BlockNumber)nodeConfig->blockLimit();
    bcos::protocol::BlockNumber currentBlockNumber = getCurrentBlockNumber(stateStorage);
    stateStorage.reset();
    metaFile << "snapshot.blockNumber = " << currentBlockNumber << std::endl;
    std::cout << "current block number: " << currentBlockNumber << std::endl;
    auto nonceStartNumber = currentBlockNumber > blockLimit ? currentBlockNumber - blockLimit : 0;
    auto validNonceStartKey = bcos::storage::toDBKey(
        bcos::ledger::SYS_BLOCK_NUMBER_2_NONCES, std::to_string(blockLimit + 1));
    auto validNonceEndKey = bcos::storage::toDBKey(
        bcos::ledger::SYS_BLOCK_NUMBER_2_NONCES, std::to_string(currentBlockNumber));
    const size_t MAX_SST_FILE_BYTE = 128 << 20;  // 128MB
    rocksdb::Options options;
    options.compression = rocksdb::kZSTD;
    auto stateSstFileWriter = rocksdb::SstFileWriter(rocksdb::EnvOptions(), options);
    auto blockSstFileWriter = rocksdb::SstFileWriter(rocksdb::EnvOptions(), options);
    size_t blockSstIndex = 0;
    auto blockSstFileName = getSstFileName(blockSstPath.string(), blockSstIndex);
    auto status = blockSstFileWriter.Open(blockSstFileName.string());
    if (!status.ok())
    {
        std::cerr << "open file " << blockSstFileName << " failed, reason: " << status.ToString()
                  << std::endl;
        return BCOS_ERROR_PTR(-1,
            "open file " + blockSstFileName.string() + " failed , reason: " + status.ToString());
    }
    size_t stateSstIndex = 0;
    std::vector<rocksdb::ExternalSstFileInfo> sstFiles;
    auto stateSstFileName = getSstFileName(stateSstPath.string(), stateSstIndex);
    status = stateSstFileWriter.Open(stateSstFileName.string());
    if (!status.ok())
    {
        std::cerr << "open file " << stateSstFileName << " failed, reason: " << status.ToString()
                  << std::endl;
        return BCOS_ERROR_PTR(-1,
            "open file " + stateSstFileName.string() + " failed , reason: " + status.ToString());
    }

    auto checkSstFileWriter = [&sstFiles](const fs::path& sstPath,
                                  rocksdb::SstFileWriter& sstFileWriter, fs::path& sstFileName,
                                  size_t& sstIndex) -> Error::Ptr {
        if (sstFileWriter.FileSize() >= MAX_SST_FILE_BYTE)
        {
            sstFiles.emplace_back();
            std::cout << sstFileName << " Finished. count: " << sstFiles.size() << std::endl;
            auto status = sstFileWriter.Finish(&sstFiles.back());
            if (!status.ok())
            {
                std::cout << "Error while finish file " << sstFileName
                          << ", Error: " << status.ToString() << std::endl;
                return BCOS_ERROR_PTR(-1, "Error while finish file " + sstFileName.string() +
                                              ", Error: " + status.ToString());
            }
            // sstFileList.emplace_back(sstFiles.back().file_path);
            ++sstIndex;
            sstFileName = getSstFileName(sstPath.string(), sstIndex);
            status = sstFileWriter.Open(sstFileName.string());
            if (!status.ok())
            {
                std::cout << "Error while opening file " << sstFileName
                          << ", Error: " << status.ToString() << std::endl;
                return BCOS_ERROR_PTR(-1, "Error while opening file " + sstFileName.string() +
                                              ", Error: " + status.ToString());
            }
        }
        return nullptr;
    };
    auto checkAndFinishSStFileWriter = [&](rocksdb::SstFileWriter& sstFileWriter,
                                           const fs::path& sstFileName,
                                           size_t& sstIndex) -> Error::Ptr {
        if (sstFileWriter.FileSize() > 0)
        {
            sstFiles.emplace_back();
            std::cout << sstFileName << " Finished. " << sstFiles.size() << std::endl;
            auto status = sstFileWriter.Finish(&sstFiles.back());
            if (!status.ok())
            {
                std::cout << "Error while finish file " << sstFileName
                          << ", Error: " << status.ToString() << std::endl;
                return BCOS_ERROR_PTR(-1, "Error while finish file " + sstFileName.string() +
                                              ", Error: " + status.ToString());
            }
            // sstFileList.emplace_back(sstFiles.back().file_path);
        }
        else
        {
            --sstIndex;
        }
        return nullptr;
    };

    {  // export state to sst
        auto error = traverseRocksDB(stateDBPath,
            [&](const rocksdb::Slice& key, const rocksdb::Slice& value) -> bcos::Error::Ptr {
                // if return true, skip the key
                if (key.starts_with("s_block_number_2_nonces"))
                {
                    if (key.compare(validNonceStartKey) < 0)
                    {  // useless nonce
                        return nullptr;
                    }
                }
                if (!separatedBlockAndState &&
                    (key.starts_with("s_hash_2_receipt") || key.starts_with("s_hash_2_tx")))
                {  // only separatedBlockAndState = false, stateDB has tx and receipt
                    if (!withTxAndReceipts)
                    {  // if not withTxAndReceipts, skip tx and receipt in state
                        return nullptr;
                    }
                    // store tx and receipt in different sst file
                    rocksdb::Status status = blockSstFileWriter.Put(key, value);
                    if (!status.ok())
                    {
                        std::cerr << "Error while adding Key: " << key.ToString()
                                  << ", Error: " << status.ToString() << std::endl;
                        return BCOS_ERROR_PTR(-1, "Error while adding Key: " + key.ToString() +
                                                      ", Error: " + status.ToString());
                    }
                    return checkSstFileWriter(
                        blockSstPath, blockSstFileWriter, blockSstFileName, blockSstIndex);
                }
                rocksdb::Status status = stateSstFileWriter.Put(key, value);
                if (!status.ok())
                {
                    std::cerr << "Error while adding Key: " << key.ToString()
                              << ", Error: " << status.ToString() << std::endl;
                    return BCOS_ERROR_PTR(-1, "Error while adding Key: " + key.ToString() +
                                                  ", Error: " + status.ToString());
                }
                return checkSstFileWriter(
                    stateSstPath, stateSstFileWriter, stateSstFileName, stateSstIndex);
            });
        if (error)
        {
            metaFile.close();
            return error;
        }
        error = checkAndFinishSStFileWriter(stateSstFileWriter, stateSstFileName, stateSstIndex);
        if (error)
        {
            metaFile.close();
            return error;
        }
        if (withTxAndReceipts && !separatedBlockAndState)
        {
            error =
                checkAndFinishSStFileWriter(blockSstFileWriter, blockSstFileName, blockSstIndex);
            if (error)
            {
                metaFile.close();
                return error;
            }
        }
        // write index to meta file
        metaFile << "snapshot.stateSstCount = " << stateSstIndex << std::endl;
    }
    if (withTxAndReceipts && separatedBlockAndState)
    {
        // open blockDB
        auto error = traverseRocksDB(blockDBPath,
            [&](const rocksdb::Slice& key, const rocksdb::Slice& value) -> bcos::Error::Ptr {
                if (key.starts_with("s_hash_2_receipt") || key.starts_with("s_hash_2_tx"))
                {
                    rocksdb::Status status = blockSstFileWriter.Put(key, value);
                    if (!status.ok())
                    {
                        std::cerr << "Error while adding Key: " << key.ToString()
                                  << ", Error: " << status.ToString() << std::endl;
                        return BCOS_ERROR_PTR(-1, "Error while adding Key: " + key.ToString() +
                                                      ", Error: " + status.ToString());
                    }

                    return checkSstFileWriter(
                        blockSstPath, blockSstFileWriter, blockSstFileName, blockSstIndex);
                }
                return nullptr;
            });
        error = checkAndFinishSStFileWriter(blockSstFileWriter, blockSstFileName, blockSstIndex);
        if (error)
        {
            metaFile.close();
            return error;
        }
    }
    if (!withTxAndReceipts)
    {  // delete the block sst path
        fs::remove_all(blockSstPath);
    }
    metaFile << "snapshot.blockSstCount = " << blockSstIndex << std::endl;
    metaFile.close();
    std::cout << "generate snapshot success, the snapshot is in " << snapshotRoot << std::endl;
    return nullptr;
}

bcos::Error::Ptr bcos::initializer::traverseRocksDB(const std::string& rockDBPath,
    const std::function<bcos::Error::Ptr(const rocksdb::Slice& key, const rocksdb::Slice& value)>&
        processor)
{
    using namespace rocksdb;
    if (!fs::exists(rockDBPath))
    {
        std::cerr << "rocksDB path " << rockDBPath << " does not exist" << std::endl;
        return BCOS_ERROR_PTR(-1, rockDBPath + " does not exist");
    }
    auto db = createReadOnlyRocksDB(rockDBPath);
    if (db == nullptr)
    {
        std::cerr << "open readonly rocksDB failed" << std::endl;
        return BCOS_ERROR_PTR(-1, "open readonly rocksDB failed");
    }
    std::cout << "Traverse RocksDB: " << rockDBPath << std::endl;
    ReadOptions readOptions;
    readOptions.snapshot = db->GetSnapshot();
    std::unique_ptr<Iterator> it(db->NewIterator(readOptions));
    for (it->SeekToFirst(); it->Valid(); it->Next())
    {
        auto err = processor(it->key(), it->value());
        if (err)
        {
            db->ReleaseSnapshot(readOptions.snapshot);
            return err;
        }
    }
    db->ReleaseSnapshot(readOptions.snapshot);
    return nullptr;
}

bcos::Error::Ptr Initializer::importSnapshot(
    const std::string& snapshotPath, const tool::NodeConfig::Ptr& nodeConfig)
{
    if (!boost::iequals(nodeConfig->storageType(), "RocksDB"))
    {  // TODO: support TiKV
        std::cerr << "only support RocksDB storage" << std::endl;
        return BCOS_ERROR_PTR(-1, "only support RocksDB storage");
    }
    return importSnapshotToRocksDB(snapshotPath, nodeConfig);
}

bcos::Error::Ptr ingestIntoRocksDB(
    rocksdb::DB& rocksDB, const std::vector<std::string>& sstFiles, bool moveFiles)
{
    rocksdb::Options options;
    options.compression = rocksdb::kZSTD;
    auto sstFileReader = rocksdb::SstFileReader(options);
    for (const auto& sstFileName : sstFiles)
    {  // check sst file
        auto status = sstFileReader.Open(sstFileName);
        if (!status.ok())
        {
            std::cerr << "open file " << sstFileName << " failed, reason: " << status.ToString()
                      << std::endl;
            return BCOS_ERROR_PTR(
                -1, "open file " + sstFileName + " failed , reason: " + status.ToString());
        }
        status = sstFileReader.VerifyChecksum();
        if (!status.ok())
        {
            std::cerr << "verify file " << sstFileName << " failed, reason: " << status.ToString()
                      << std::endl;
            return BCOS_ERROR_PTR(
                -1, "verify file " + sstFileName + " failed , reason: " + status.ToString());
        }
    }
    std::cout << "check sst files success, ingest sst files" << std::endl;
    rocksdb::IngestExternalFileOptions info;
    info.move_files = moveFiles;
    // Ingest SST files into the DB
    rocksdb::Status status = rocksDB.IngestExternalFile(sstFiles, info);
    if (!status.ok())
    {
        std::cerr << "Error while adding file, " << status.ToString() << std::endl;
        return BCOS_ERROR_PTR(-1, "Error while adding file, " + status.ToString());
    }
    // compaction
    status = rocksDB.CompactRange(rocksdb::CompactRangeOptions(), nullptr, nullptr);
    if (!status.ok())
    {
        std::cerr << "compaction failed, " << status.ToString() << std::endl;
        return BCOS_ERROR_PTR(-1, "compaction failed, " + status.ToString());
    }
    return nullptr;
}

bcos::Error::Ptr Initializer::importSnapshotToRocksDB(
    const std::string& snapshotPath, const tool::NodeConfig::Ptr& nodeConfig)
{
    // check snapshot file and meta file
    fs::path sstPath = snapshotPath + "/state";
    if (!fs::exists(sstPath))
    {
        std::cerr << "snapshot path " << sstPath << " does not exist" << std::endl;
        return BCOS_ERROR_PTR(-1, sstPath.string() + " does not exist");
    }
    // read meta file
    fs::path metaFilePath = snapshotPath + "/meta";
    if (!fs::exists(metaFilePath))
    {
        std::cerr << "meta file " << metaFilePath << " does not exist" << std::endl;
        return BCOS_ERROR_PTR(-1, metaFilePath.string() + " does not exist");
    }
    auto tomlTable = toml::parse_file(metaFilePath.string());
    auto snapshotWithTxAndReceipts = tomlTable["snapshot"]["withTxAndReceipts"].value<bool>();
    auto snapshotBlockNumber = tomlTable["snapshot"]["blockNumber"].value<protocol::BlockNumber>();
    auto stateSstCount = tomlTable["snapshot"]["stateSstCount"].value<size_t>();
    auto blockSstCount = tomlTable["snapshot"]["blockSstCount"].value<size_t>();

    if (snapshotBlockNumber.has_value())
    {
        std::cout << "The block number of snapshot: " << snapshotBlockNumber.value() << std::endl;
    }

    // import state
    // check the destination db is empty
    auto stateDBPath = getStateDBPath(true);
    if (fs::exists(stateDBPath) && !fs::is_empty(stateDBPath))
    {
        std::cerr << "db path " << stateDBPath << " is not empty" << std::endl;
        return BCOS_ERROR_PTR(-1, stateDBPath + " is not empty");
    }
    size_t sstIndex = 0;
    if (stateSstCount.has_value())
    {
        sstIndex = stateSstCount.value();
    }
    else
    {
        std::cerr << "stateSstCount is not set" << std::endl;
        return BCOS_ERROR_PTR(-1, "stateSstCount is not set");
    }
    std::vector<std::string> sstFiles;
    for (size_t i = 0; i <= sstIndex; ++i)
    {
        auto sstFileName = getSstFileName(sstPath.string(), i);
        if (!fs::exists(sstFileName))
        {
            std::cerr << "sst file " << sstFileName << " does not exist" << std::endl;
            return BCOS_ERROR_PTR(-1, sstFileName.string() + " does not exist");
        }
        sstFiles.emplace_back(sstFileName.string());
    }
    bool moveSSTFiles = true;
    std::cout << "the snapshot will be ingested into " << stateDBPath
              << ", if yes the snapshot will be moved, if no the snapshot will be copy(yes/no)"
              << std::endl;
    std::string input;
    std::cin >> input;
    if (boost::iequals(input, "no"))
    {
        moveSSTFiles = false;
    }
    auto rocksdbOption = getRocksDBOption(nodeConfig, true);
    auto rocksDB = StorageInitializer::createRocksDB(
        stateDBPath, rocksdbOption, nodeConfig->enableStatistics(), nodeConfig->keyPageSize());
    ingestIntoRocksDB(*rocksDB, sstFiles, moveSSTFiles);
    bcos::storage::TransactionalStorageInterface::Ptr stateStorage = nullptr;
    // import tx and receipt
    if (snapshotWithTxAndReceipts.has_value() && snapshotWithTxAndReceipts.value())
    {  // snapshot has tx and receipt
        if (blockSstCount.has_value())
        {
            sstIndex = blockSstCount.value();
        }
        else
        {
            std::cerr << "blockSstCount is not set" << std::endl;
            return BCOS_ERROR_PTR(-1, "blockSstCount is not set");
        }
        fs::path blockSstPath = snapshotPath + "/block";
        if (!fs::exists(blockSstPath))
        {
            std::cerr << "snapshot path " << blockSstPath << " does not exist" << std::endl;
            return BCOS_ERROR_PTR(-1, blockSstPath.string() + " does not exist");
        }
        std::vector<std::string> blockSstFiles;
        for (size_t i = 0; i <= sstIndex; ++i)
        {
            auto sstFileName = getSstFileName(blockSstPath.string(), i);
            if (!fs::exists(sstFileName))
            {
                std::cerr << "sst file " << sstFileName << " does not exist" << std::endl;
                return BCOS_ERROR_PTR(-1, sstFileName.string() + " does not exist");
            }
            blockSstFiles.emplace_back(sstFileName.string());
        }
        if (nodeConfig->enableSeparateBlockAndState())
        {
            auto blockDBPath = getBlockDBPath(true);
            auto blockRocksDB = StorageInitializer::createRocksDB(
                blockDBPath, rocksdbOption, nodeConfig->enableStatistics());
            if (blockRocksDB)
            {
                ingestIntoRocksDB(*blockRocksDB, blockSstFiles, moveSSTFiles);
            }
            else
            {
                std::cerr << "create blockStorage failed" << std::endl;
                return BCOS_ERROR_PTR(-1, "create blockStorage failed");
            }
        }
        else
        {  // import block into state db
            ingestIntoRocksDB(*rocksDB, blockSstFiles, moveSSTFiles);
        }
        stateStorage =
            StorageInitializer::build(std::move(rocksDB), m_protocolInitializer->dataEncryption());
        auto currentBlockNumber = getCurrentBlockNumber(stateStorage);
        std::cout << "The block number of this node: " << currentBlockNumber << std::endl;
        return nullptr;
    }
    stateStorage =
        StorageInitializer::build(std::move(rocksDB), m_protocolInitializer->dataEncryption());
    auto currentBlockNumber = getCurrentBlockNumber(stateStorage);
    std::cout << "The block number of this node: " << currentBlockNumber << std::endl;
    {  // snapshot without tx and receipt
        storage::Entry archivedNumber;
        // the archived number is the first block has full tx and receipt
        archivedNumber.importFields({std::to_string(currentBlockNumber + 1)});
        std::promise<Error::UniquePtr> setPromise;

        stateStorage->asyncSetRow(ledger::SYS_CURRENT_STATE, ledger::SYS_KEY_ARCHIVED_NUMBER,
            archivedNumber, [&](Error::UniquePtr err) { setPromise.set_value(std::move(err)); });
        auto setError = setPromise.get_future().get();
        if (setError)
        {
            std::cerr << "set archived number failed: " << setError->errorMessage() << std::endl;
            return BCOS_ERROR_PTR(-1, "set archived number failed: " + setError->errorMessage());
        }
        std::cout << "The snapshot doesn't contain transactions and receipts, if you want the new "
                     "node to sync historic blocks, please set storage.sync_archived_blocks = true "
                     "in the configuration file."
                  << std::endl;
    }
    return nullptr;
}

std::string Initializer::getStateDBPath(bool _airVersion) const
{
    std::string stateDBPath = m_nodeConfig->enableSeparateBlockAndState() ?
                                  m_nodeConfig->stateDBPath() :
                                  m_nodeConfig->storagePath();
    if (_airVersion)
    {
        return stateDBPath;
    }
    // if the stateDBPath is absolute path, the result stateDBPath will deep
    return tars::ServerConfig::BasePath + ".." + c_fileSeparator + m_nodeConfig->groupId() +
           c_fileSeparator + stateDBPath;
}

std::string Initializer::getBlockDBPath(bool _airVersion) const
{
    std::string blockDBPath = m_nodeConfig->blockDBPath();
    if (_airVersion)
    {
        return blockDBPath;
    }
    // if the stateDBPath is absolute path, the result stateDBPath will deep
    return tars::ServerConfig::BasePath + ".." + c_fileSeparator + m_nodeConfig->groupId() +
           c_fileSeparator + blockDBPath;
}

std::string Initializer::getConsensusStorageDBPath(bool _airVersion) const
{
    std::string consensusStorageDBPath =
        m_nodeConfig->storagePath() + c_fileSeparator + c_consensusStorageDBName;
    if (_airVersion)
    {
        return consensusStorageDBPath;
    }
    // if the stateDBPath is absolute path, the result stateDBPath will deep
    return tars::ServerConfig::BasePath + ".." + c_fileSeparator + m_nodeConfig->groupId() +
           c_fileSeparator + c_consensusStorageDBName;
}
