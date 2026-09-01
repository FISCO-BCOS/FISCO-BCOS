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
#include "EngineServiceInitializer.h"
#include "EthereumBlockHashLookup.h"
#include "GlobalStateStorageInitializer.h"
#include "LedgerInitializer.h"
#include "MemPoolInitializer.h"
#include "SchedulerInitializer.h"
#include "StorageInitializer.h"
#include "bcos-executor/src/executor/SwitchExecutorManager.h"
#include "bcos-framework/dispatcher/SchedulerInterface.h"
#include "bcos-framework/ledger/Ledger.h"
#include "bcos-framework/storage/StorageInterface.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-scheduler/src/TarsExecutorManager.h"
#include "bcos-single-consensus/SingleNodeConsensus.h"
#include "bcos-storage/MPTNodeReadStorage.h"
#include "bcos-storage/RocksDBStorage.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/Error.h"
#include "ethereum-executor/EthereumExecutor.h"
#include "fisco-bcos-tars-service/Common/TarsUtils.h"
#include "libinitializer/BaselineSchedulerInitializer.h"
#include "libinitializer/ProPBFTInitializer.h"
#include <TxPool.h>
#include <bcos-crypto/hash/Keccak256.h>
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
#include <bcos-tool/NodeConfig.h>
#include <bcos-tool/NodeTimeMaintenance.h>
#include <bcos-transaction-executor/TransactionExecutorImpl.h>
#include <bcos-transaction-executor/precompiled/PrecompiledManager.h>
#include <bcos-transaction-scheduler/SchedulerParallelImpl.h>
#include <bcos-transaction-scheduler/SchedulerSerialImpl.h>
#include <legacy/bcos-storage/StorageWrapperImpl.h>
#include <opstack-executor/OpScheduler.h>
#include <opstack-executor/OpSchedulerSeam.h>
#include <algorithm>
#include <rocksdb/slice.h>
#include <rocksdb/sst_file_reader.h>
#include <txpool/validator/TxValidator.h>
#include <util/tc_clientsocket.h>
#include <boost/algorithm/string.hpp>
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

namespace
{
/// Stub for MultiVersionScheduler slot 3 when OP mode is off.
class OpRefusingStubScheduler : public bcos::scheduler::SchedulerInterface
{
public:
    void executeBlock(bcos::protocol::Block::Ptr, bool,
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool)> cb) override
    {
        cb(BCOS_ERROR_PTR(bcos::scheduler::SchedulerError::UnknownError,
               "OpRefusingStubScheduler: OP scheduler not assembled (executor_version<3)"),
            nullptr, false);
    }
    void commitBlock(bcos::protocol::BlockHeader::Ptr,
        std::function<void(bcos::Error::Ptr, bcos::ledger::LedgerConfig::Ptr)> cb) override
    {
        cb(BCOS_ERROR_PTR(bcos::scheduler::SchedulerError::UnknownError,
               "OpRefusingStubScheduler: OP scheduler not assembled (executor_version<3)"),
            nullptr);
    }
    void call(bcos::protocol::Transaction::Ptr,
        std::function<void(bcos::Error::Ptr, bcos::protocol::TransactionReceipt::Ptr)> cb) override
    {
        cb(BCOS_ERROR_PTR(bcos::scheduler::SchedulerError::UnknownError,
               "OpRefusingStubScheduler: OP scheduler not assembled (executor_version<3)"),
            nullptr);
    }
    void preExecuteBlock(
        bcos::protocol::Block::Ptr, bool, std::function<void(bcos::Error::Ptr)> cb) override
    {
        cb(BCOS_ERROR_PTR(bcos::scheduler::SchedulerError::UnknownError,
            "OpRefusingStubScheduler: OP scheduler not assembled (executor_version<3)"));
    }
    void getCode(std::string_view, std::function<void(bcos::Error::Ptr, bcos::bytes)> cb) override
    {
        cb(BCOS_ERROR_PTR(bcos::scheduler::SchedulerError::UnknownError,
               "OpRefusingStubScheduler: OP scheduler not assembled (executor_version<3)"),
            {});
    }
    void getABI(std::string_view, std::function<void(bcos::Error::Ptr, std::string)> cb) override
    {
        cb(BCOS_ERROR_PTR(bcos::scheduler::SchedulerError::UnknownError,
               "OpRefusingStubScheduler: OP scheduler not assembled (executor_version<3)"),
            {});
    }
    task::Task<std::optional<bcos::storage::Entry>> getPendingStorageAt(
        std::string_view, std::string_view, bcos::protocol::BlockNumber) override
    {
        co_return std::nullopt;
    }
    void status(
        std::function<void(bcos::Error::Ptr, bcos::protocol::Session::ConstPtr)> cb) override
    {
        cb({}, {});
    }
    void reset(std::function<void(bcos::Error::Ptr)> cb) override { cb({}); }
    // callAtBlock uses the SchedulerInterface default (forwards to call).
};
}  // namespace

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
    option.enableBlobFiles = nodeConfig->enableRocksDBBlob();
    option.enableDBStatistics = nodeConfig->enableStatistics();
    return option;
}

std::shared_ptr<bcos::engine::AnyEngineService> Initializer::engineService()
{
    return m_engineServiceInitializer ? m_engineServiceInitializer->engineService() : nullptr;
}

void Initializer::init(bcos::protocol::NodeArchitectureType _nodeArchType,
    std::string const& _configFilePath, std::string const& _genesisFile,
    bcos::gateway::GatewayInterface::Ptr _gateway, bool _airVersion, const std::string& _logPath)
{
    // Single-node / OP engine production is AIR-only (in-process mempool).
    if (m_nodeConfig->engineDrivenBlockProduction() &&
        _nodeArchType != bcos::protocol::NodeArchitectureType::AIR)
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment(
                "enable_single_node_consensus / op_engine_rpc.enable are only supported on "
                "AIR nodes (in-process mempool/scheduler); not supported on MAX/tars "
                "deployments"));
    }

    // TBB global thread control
    auto tbbThreadCount = m_nodeConfig->tbbThreadCount();
    if (tbbThreadCount > 0)
    {
        m_tbbGlobalControl.emplace(
            oneapi::tbb::global_control::max_allowed_parallelism, tbbThreadCount);
        INITIALIZER_LOG(INFO) << LOG_DESC("TBB global_control set")
                              << LOG_KV("maxAllowedParallelism", tbbThreadCount);
    }

    // build the front service
    m_frontServiceInitializer = std::make_shared<FrontServiceInitializer>(
        m_nodeConfig, m_protocolInitializer, _gateway, m_ioServicePool);

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

    // CheckpointRocksDBStorage owns the state RocksDB lifecycle.
    // Create it before the legacy storage so the old TransactionalStorageInterface
    // can share the same underlying ::rocksdb::DB.
    auto rocksDBOption = getRocksDBOption(m_nodeConfig);
    m_globalStateStorageInitializer =
        GlobalStateStorageInitializer::build(m_nodeConfig->storagePath(), rocksDBOption);

    if (boost::iequals(m_nodeConfig->storageType(), "RocksDB"))
    {
        // Share CheckpointRocksDBStorage's RocksDB with the legacy storage layer.
        // Ownership stays with GlobalStateStorage (MultiLayerStorage::m_latestBackend).
        m_storage = StorageInitializer::build(
            std::unique_ptr<::rocksdb::DB, std::function<void(::rocksdb::DB*)>>(
                &m_globalStateStorageInitializer->rocksDB(),
                [](::rocksdb::DB*) { /* lifetime managed by GlobalStateStorage */ }),
            m_protocolInitializer->dataEncryption());
        schedulerStorage = m_storage;
        consensusStorage = StorageInitializer::build(
            StorageInitializer::createRocksDB(consensusStoragePath, rocksDBOption),
            m_protocolInitializer->dataEncryption());
        airExecutorStorage = m_storage;
        if (m_nodeConfig->enableSeparateBlockAndState())
        {
            m_blockStorage = StorageInitializer::build(
                StorageInitializer::createRocksDB(blockDBPath, rocksDBOption),
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
    auto ledger = LedgerInitializer::build(m_protocolInitializer->blockFactory(), m_storage,
        m_nodeConfig, m_blockStorage, m_ioServicePool);
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
    m_txpoolInitializer = std::make_shared<TxPoolInitializer>(m_nodeConfig, m_protocolInitializer,
        m_frontServiceInitializer->front(), ledger, *m_ioServicePool->getIOService(),
        m_ioServicePool);
    m_memPoolInitializer = MemPoolInitializer::build();

    std::shared_ptr<bcos::scheduler::TarsExecutorManager> executorManager;

    bcos::executor::GlobalHashImpl::g_hashImpl = m_protocolInitializer->cryptoSuite()->hashImpl();

    auto baselineSchedulerConfig = m_nodeConfig->baselineSchedulerConfig();

    // Create shared PrecompiledManager and TransactionExecutorImpl
    // NOTE: The same scheduler and transactionExecutor shared_ptr is passed to both
    // BaselineSchedulerInitializer (legacy consensus path) and EngineServiceInitializer
    // (Engine API path). This assumes that only ONE path drives the scheduler at any
    // given time. If both paths need concurrent access, SchedulerParallelImpl::executeBlock
    // and TransactionExecutorImpl::executeTransaction must be audited for thread-safety
    // and guarded with a lock.
    m_precompiledManager = std::make_shared<executor_v1::PrecompiledManager>(
        m_protocolInitializer->cryptoSuite()->hashImpl());
    auto transactionExecutor = std::make_shared<executor_v1::TransactionExecutorImpl>(
        *m_protocolInitializer->blockFactory()->receiptFactory(),
        m_protocolInitializer->cryptoSuite()->hashImpl(), *m_precompiledManager);

    // EthereumExecutor (executor_version=2): pure-Ethereum execution layer driven through the
    // same scheduler prepare()/execute()/finish() pipeline as TransactionExecutorImpl.
    // The injected block-hash lookup resolves BLOCKHASH against the committed
    // global-storage backend (SYS_NUMBER_2_HASH via the ledger::getBlockHash
    // LedgerMethod), so it outlives any single block's execute view — matching the
    // semantics documented in EthereumExecutor.h. The lookup fails closed: a broken
    // backend or an out-of-window request reports an unknown block (zero hash).
    // It reads storage directly (no cache): one getBlockHash read per BLOCKHASH.
    // The 256-ancestor window is bounded by the current block height, which the
    // executor's execution context already knows (EthereumHost passes
    // m_block.number), so no storage read for the current height is needed.
    auto ethereumBlockHashLookup =
        [&backend = m_globalStateStorageInitializer->storage().latestBackend()](
            int64_t blockNumber, int64_t currentHeight) -> evmc::bytes32 {
        // The body lives in EthereumBlockHashLookup.h (shared with the
        // scheduler integration test so they exercise the same provider).
        return ethBlockHashLookupFromStorage(backend, blockNumber, currentHeight);
    };
    auto ethereumExecutor = std::make_shared<executor_v1::eth::EthereumExecutor>(
        *m_protocolInitializer->blockFactory()->receiptFactory(),
        std::move(ethereumBlockHashLookup));

    // Resolve the effective executor version BEFORE gating Engine API / wiring the
    // schedulers. The on-chain value overrides the genesis-file value and can move to >= 2
    // at runtime (executor_version is runtime-settable via SystemConfigPrecompiled, and
    // MultiVersionScheduler::setVersion saturates any version >= 2 onto the v2
    // EthereumExecutor), so the gate below must read the ledger rather than the node
    // config — a node whose genesis said v1 but whose ledger says v2 would otherwise build
    // the Engine API on the v1 executor, the state-root divergence the gate exists to
    // prevent. The residual risk of a runtime switch to v2 without genesis config is handled
    // by the boot refusal below (no on-chain evmc_revision row), not by a per-block
    // validator. Genesis is already built (LedgerInitializer), so m_ledger is readable at
    // this point.
    auto executorVersion = m_nodeConfig->executorVersion();
    if (auto versionConfig = task::syncWait(ledger::getSystemConfig(
            *m_ledger, magic_enum::enum_name(ledger::SystemConfig::executor_version))))
    {
        executorVersion = boost::lexical_cast<int>(std::get<0>(*versionConfig));
        INITIALIZER_LOG(INFO) << "Use ledger executor version: " << executorVersion;
    }
    m_executorVersion = executorVersion;

    // v2+/OP write raw table:key rows; force Ledger::getStorageAt off KeyPage.
    if (auto concreteLedger = std::dynamic_pointer_cast<bcos::ledger::Ledger>(m_ledger);
        m_executorVersion >= scheduler_v1::ETHEREUM_EXECUTOR_VERSION && concreteLedger)
    {
        concreteLedger->setKeyPageSize(0);
    }

    // Engine API (OP-Stack engine endpoints) is wired to the v1 TransactionExecutorImpl.
    // It must not be built for executor_version >= 2: a v2 chain's state transitions run
    // through the pure-Ethereum EthereumExecutor, and an Engine API driven through the v1
    // executor would produce blocks with v1 semantics that diverge from the v2 main chain
    // (a state-root fork). v2 chains therefore have no Engine API; engine RPC endpoints
    // respond "engine service not available" (see EngineEndpoint.cpp).
    const bool engineApiForV1Only = (m_executorVersion < scheduler_v1::ETHEREUM_EXECUTOR_VERSION);

    // OP engine RPC requires executor_version >= 2 (test-only escape: unsafe_allow_v1_executor).
    if (m_nodeConfig->enableOpEngineRpc() && engineApiForV1Only)
    {
        if (!m_nodeConfig->opEngineAllowV1Executor())
        {
            BOOST_THROW_EXCEPTION(
                InvalidConfig() << errinfo_comment(
                    "op_engine_rpc requires executor_version >= " +
                    std::to_string(scheduler_v1::ETHEREUM_EXECUTOR_VERSION) +
                    " (the pure-Ethereum executor): on executor_version < 2 the endpoint "
                    "would serve v1 semantics that diverge from an OP Stack chain. For the "
                    "v1 Engine API test harness only, set [op_engine_rpc] "
                    "unsafe_allow_v1_executor=true"));
        }
        INITIALIZER_LOG(WARNING) << LOG_DESC(
            "op_engine_rpc serving the v1 EngineService (unsafe_allow_v1_executor=true): "
            "test-harness mode, never drive this endpoint with a production op-node");
    }

    if (baselineSchedulerConfig.parallel)
    {
        auto parallelScheduler =
            std::make_shared<scheduler_v1::SchedulerParallelImpl<GlobalStateMutableStorage>>(
                m_ioServicePool);
        parallelScheduler->m_grainSize = baselineSchedulerConfig.grainSize;
        if (tbbThreadCount > 0)
        {
            parallelScheduler->m_maxConcurrency = tbbThreadCount;
        }
        std::tie(m_baselineSchedulerHolder, m_setBaselineSchedulerBlockNumberNotifier) =
            scheduler_v1::BaselineSchedulerInitializer::build(m_globalStateStorageInitializer,
                m_protocolInitializer->blockFactory(), parallelScheduler,
                m_txpoolInitializer->txpool(), transactionSubmitResultFactory, ledger,
                transactionExecutor, !m_nodeConfig->engineDrivenBlockProduction());
        if (engineApiForV1Only)
        {
            m_engineServiceInitializer = EngineServiceInitializer::build(
                m_globalStateStorageInitializer, m_protocolInitializer->blockFactory(),
                parallelScheduler, transactionExecutor, m_memPoolInitializer->memPool(), ledger);
        }

        // executor_version=2: a dedicated pipeline instance for the EthereumExecutor baseline
        // scheduler. Only one scheduler version is active at a time (selected via
        // MultiVersionScheduler), so a dedicated instance avoids any cross-version state.
        //
        // The v2 pipeline is deliberately SERIAL-ONLY even when the node is configured for
        // parallel baseline scheduling: EthereumState::has_storage scans the account table via
        // storage2::range(), which ReadWriteSetStorage does not record in the read/write set,
        // so on SchedulerParallelImpl a chunk writing an account's storage would not be
        // ordered against a chunk reading that account's has_storage (an EIP-7610
        // CREATE-collision input) — a consensus divergence. Revisit (record the range read)
        // before v2 is allowed to run on a parallel scheduler.
        auto ethereumSerialScheduler =
            std::make_shared<scheduler_v1::SchedulerSerialImpl>(m_ioServicePool);
        std::tie(m_ethereumSchedulerHolder, m_setEthereumSchedulerBlockNumberNotifier) =
            scheduler_v1::BaselineSchedulerInitializer::build(m_globalStateStorageInitializer,
                m_protocolInitializer->blockFactory(), ethereumSerialScheduler,
                m_txpoolInitializer->txpool(), transactionSubmitResultFactory, ledger,
                ethereumExecutor, !m_nodeConfig->engineDrivenBlockProduction());
        // Single-node consensus mode on the v2 EthereumExecutor: build the Engine API service
        // wired to the ethereum scheduler + EthereumExecutor so blocks are built with
        // Ethereum-compliant semantics. In this mode the EngineService is the sole block
        // producer, so the v1-only gate above does not apply.
        if (!engineApiForV1Only && m_nodeConfig->enableSingleNodeConsensus())
        {
            m_engineServiceInitializer = EngineServiceInitializer::build(
                m_globalStateStorageInitializer, m_protocolInitializer->blockFactory(),
                ethereumSerialScheduler, ethereumExecutor, m_memPoolInitializer->memPool(), ledger);
        }
    }
    else
    {
        auto serialScheduler = std::make_shared<scheduler_v1::SchedulerSerialImpl>(m_ioServicePool);
        std::tie(m_baselineSchedulerHolder, m_setBaselineSchedulerBlockNumberNotifier) =
            scheduler_v1::BaselineSchedulerInitializer::build(m_globalStateStorageInitializer,
                m_protocolInitializer->blockFactory(), serialScheduler,
                m_txpoolInitializer->txpool(), transactionSubmitResultFactory, ledger,
                transactionExecutor, !m_nodeConfig->engineDrivenBlockProduction());
        if (engineApiForV1Only)
        {
            m_engineServiceInitializer = EngineServiceInitializer::build(
                m_globalStateStorageInitializer, m_protocolInitializer->blockFactory(),
                serialScheduler, transactionExecutor, m_memPoolInitializer->memPool(), ledger);
        }

        // executor_version=2 baseline scheduler, driven by a dedicated serial pipeline.
        auto ethereumSerialScheduler =
            std::make_shared<scheduler_v1::SchedulerSerialImpl>(m_ioServicePool);
        std::tie(m_ethereumSchedulerHolder, m_setEthereumSchedulerBlockNumberNotifier) =
            scheduler_v1::BaselineSchedulerInitializer::build(m_globalStateStorageInitializer,
                m_protocolInitializer->blockFactory(), ethereumSerialScheduler,
                m_txpoolInitializer->txpool(), transactionSubmitResultFactory, ledger,
                ethereumExecutor, !m_nodeConfig->engineDrivenBlockProduction());
        // Single-node consensus mode on the v2 EthereumExecutor (serial pipeline).
        if (!engineApiForV1Only && m_nodeConfig->enableSingleNodeConsensus())
        {
            m_engineServiceInitializer = EngineServiceInitializer::build(
                m_globalStateStorageInitializer, m_protocolInitializer->blockFactory(),
                ethereumSerialScheduler, ethereumExecutor, m_memPoolInitializer->memPool(), ledger);
        }
    }

    // executor_version >= 3: OP mode (OpSchedulerSeam / c_opMode).
    const bool opStackMode = (m_executorVersion >= scheduler_v1::OPSTACK_EXECUTOR_VERSION);
    if (opStackMode)
    {
        // Isthmus baseline; Jovian is feature-gated.
        auto forkFlags = bcos::evm::opstack::OpForkFlags{
            .jovianActive = m_nodeConfig->opJovianActive(),
        };
        // chainId must be numeric (decimal or 0x-hex).
        uint64_t opChainId = 0;
        try
        {
            opChainId = std::stoull(m_nodeConfig->chainId(), nullptr, 0);
        }
        catch (const std::invalid_argument&)
        {
            BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                                      "OP mode (executor_version>=3) requires a numeric chain_id "
                                      "(decimal or 0x-prefixed hex)"));
        }
        catch (const std::out_of_range&)
        {
            BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                                      "OP mode (executor_version>=3) requires a numeric chain_id "
                                      "(decimal or 0x-prefixed hex)"));
        }
        // L1 info for the built-in L1-attributes deposit synthesis ([op_l1] config).
        // All-zero when absent — the documented built-in-CL stand-in, never a production
        // L1 value (a real deployment's op-node supplies the deposit itself).
        bcos::evm::opstack::L1BlockInfo l1BlockInfo;
        const auto& opL1 = m_nodeConfig->opL1Info();
        if (!opL1.blockHashHex.empty())
        {
            bcos::bytes hashBytes;
            try
            {
                hashBytes = bcos::fromHex(opL1.blockHashHex);
            }
            catch (const std::exception&)
            {
                BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                                          "OP mode (executor_version>=3): op_l1.l1_block_hash "
                                          "must be a hex string"));
            }
            if (hashBytes.size() != sizeof(evmc::bytes32))
            {
                BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                                          "OP mode (executor_version>=3): op_l1.l1_block_hash "
                                          "must be a 32-byte (64 hex chars) block hash"));
            }
            std::copy_n(
                hashBytes.begin(), sizeof(evmc::bytes32), l1BlockInfo.blockHash.bytes);
        }
        l1BlockInfo.number = opL1.blockNumber;
        l1BlockInfo.time = opL1.timestamp;
        l1BlockInfo.baseFee = opL1.baseFee;
        l1BlockInfo.sequenceNumber = opL1.sequenceNumber;
        l1BlockInfo.blobBaseFee = opL1.blobBaseFee;
        auto opScheduler =
            std::make_shared<bcos::evm::engine::OpSchedulerSeam<GlobalStateStorage::ViewType>>(
                forkFlags, l1BlockInfo);
        // Same OpScheduler is the engine delegate and scheduler slot 3.
        auto opDelegate =
            std::make_shared<bcos::executor_v1::opstack::OpScheduler<GlobalStateStorage>>(
                m_protocolInitializer->blockFactory()->receiptFactory(),
                m_protocolInitializer->cryptoSuite()->hashImpl(), opChainId, forkFlags,
                m_protocolInitializer->blockFactory(), m_globalStateStorageInitializer->storage(),
                m_ledger, m_ioServicePool);
        m_daCaps = std::make_shared<bcos::engine::DACaps>();
        m_engineServiceInitializer = EngineServiceInitializer::build(
            m_globalStateStorageInitializer, m_protocolInitializer->blockFactory(), opScheduler,
            transactionExecutor, m_memPoolInitializer->memPool(), /*ledger=*/nullptr,
            bcos::engine::c_defaultBlockTxCountLimit, opDelegate,
            /*maxEngineVersion=*/static_cast<std::uint32_t>(bcos::engine::ApiVersion::V4),
            m_daCaps);
        // c_opMode needs the scheduler value type, not a reference.
        using OpEngineServiceT = bcos::engine::EngineServiceImpl<bcos::txpool::MemPoolImpl,
            GlobalStateStorage, executor_v1::TransactionExecutorImpl,
            std::remove_reference_t<decltype(*opScheduler)>>;
        static_assert(OpEngineServiceT::c_opMode,
            "OP EngineServiceImpl must enable c_opMode (computeTxRoot probe)");

        m_opScheduler = opDelegate;
        m_setOpSchedulerBlockNumberNotifier =
            [opDelegate](std::function<void(bcos::protocol::BlockNumber)> notifier) {
                opDelegate->setBlockNumberNotifier(std::move(notifier));
            };
    }

    executorManager = std::make_shared<bcos::scheduler::TarsExecutorManager>(
        *m_ioServicePool->getIOService(), m_nodeConfig->executorServiceName(), m_nodeConfig);
    auto factory = SchedulerInitializer::buildFactory(executorManager, ledger, schedulerStorage,
        executionMessageFactory, m_protocolInitializer->blockFactory(),
        m_txpoolInitializer->txpool(), m_protocolInitializer->txResultFactory(),
        m_protocolInitializer->cryptoSuite()->hashImpl(), m_nodeConfig->isAuthCheck(),
        m_nodeConfig->isSerialExecute(), m_nodeConfig->keyPageSize());

    int64_t schedulerSeq = 0;  // In Max node, this seq will be update after consensus module
                               // switch to a leader during startup
    m_scheduler = std::make_shared<scheduler_v1::MultiVersionScheduler>(
        std::to_array<scheduler::SchedulerInterface::Ptr>(
            {std::make_shared<bcos::scheduler::SchedulerManager>(
                 schedulerSeq, factory, executorManager, m_ioServicePool),
                m_baselineSchedulerHolder(), m_ethereumSchedulerHolder(),
                m_opScheduler ? m_opScheduler : std::make_shared<OpRefusingStubScheduler>()}));

    // m_executorVersion was resolved earlier (before the Engine API gate); apply it now.
    INITIALIZER_LOG(INFO) << "Set executor version to: " << m_executorVersion;
    m_scheduler->setVersion(m_executorVersion, {});

    // Parse and log the effective EVMC revision once at startup (v2+). It is fixed at genesis
    // (NodeConfig requires an explicit evm_revision for executor_version>=2), so a one-time
    // INFO line is accurate and stays off the per-block / per-RPC getLedgerConfig hot path.
    // The CI integration test greps this line to pin the effective revision.
    if (m_executorVersion >= scheduler_v1::ETHEREUM_EXECUTOR_VERSION)
    {
        if (auto evmcRev = task::syncWait(ledger::getSystemConfig(
                *m_ledger, magic_enum::enum_name(ledger::SystemConfig::evmc_revision))))
        {
            // Parse here so a corrupt persisted value fails at BOOT (an explicit startup
            // failure) instead of on the first block execution, where getLedgerConfig's
            // outer catch would turn it into a per-block "Execute block failed!" loop.
            ledger::LedgerConfig probe;
            ledger::applyEVMCRevisionConfig(probe, std::get<0>(*evmcRev));
            INITIALIZER_LOG(INFO) << LOG_DESC("Effective EVMC revision (v2)")
                                  << LOG_KV("evmcRevision", std::get<0>(*evmcRev));
        }
        else
        {
            // v2 with no evmc_revision row: the effective revision would fall back to a
            // binary-side default (EVMC_REVISION_DEFAULT), tying consensus to the binary.
            // A fresh v2 chain always has the row, so reaching here means a runtime switch
            // to v2 without genesis config. Refuse to start (node-local policy — no block
            // semantics touched) instead of defaulting.
            BOOST_THROW_EXCEPTION(
                InvalidConfig() << errinfo_comment(
                    "executor_version >= 2 but no evmc_revision system config is recorded "
                    "on-chain; the effective EVM revision would be a binary-side default. "
                    "Refusing to start — configure executor.evm_revision at genesis, or run "
                    "executor_version 0/1"));
        }
    }

    // Set scheduler to TxPoolInitializer after scheduler is created
    m_txpoolInitializer->setScheduler(m_scheduler);

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
        dynamic_cast<scheduler::SchedulerManager&>(m_scheduler->scheduler(0))
            .initSchedulerIfNotExist();
    }
    else
    {
        INITIALIZER_LOG(INFO) << LOG_DESC("create Executor")
                              << LOG_KV("nodeArchType", _nodeArchType);

        // Note: ensure that there has at least one executor before pbft/sync execute block
        auto storageFactory =
            std::make_shared<storage::StateStorageFactory>(m_nodeConfig->keyPageSize());
        std::string executorName = "executor-local";
        auto executorFactory = std::make_shared<bcos::executor::TransactionExecutorFactory>(
            m_ledger, m_txpoolInitializer->txpool(), cacheFactory, airExecutorStorage,
            executionMessageFactory, storageFactory,
            m_protocolInitializer->cryptoSuite()->hashImpl(), m_nodeConfig->vmCacheSize(),
            m_nodeConfig->isAuthCheck(), executorName, m_ioServicePool);
        auto switchExecutorManager = std::make_shared<bcos::executor::SwitchExecutorManager>(
            executorFactory, m_ioServicePool);
        executorManager->addExecutor(executorName, switchExecutorManager);
        m_switchExecutorManager = switchExecutorManager;
    }

    // build node time synchronization tool
    auto nodeTimeMaintenance = std::make_shared<NodeTimeMaintenance>();

    // build and init the pbft related modules
    if (_nodeArchType == protocol::NodeArchitectureType::AIR)
    {
        m_pbftInitializer =
            std::make_shared<PBFTInitializer>(_nodeArchType, m_nodeConfig, m_protocolInitializer,
                m_txpoolInitializer->txpool(), ledger, m_scheduler, consensusStorage,
                m_frontServiceInitializer->front(), nodeTimeMaintenance, m_ioServicePool);
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
        m_pbftInitializer =
            std::make_shared<ProPBFTInitializer>(_nodeArchType, m_nodeConfig, m_protocolInitializer,
                m_txpoolInitializer->txpool(), ledger, m_scheduler, consensusStorage,
                m_frontServiceInitializer->front(), nodeTimeMaintenance, m_ioServicePool);
    }
    if (_nodeArchType == bcos::protocol::NodeArchitectureType::MAX)
    {
        INITIALIZER_LOG(INFO) << LOG_DESC("Register switch handler in scheduler manager");
        // PBFT and scheduler are in the same process here, we just cast m_scheduler to
        // SchedulerService
        auto& schedulerServer =
            dynamic_cast<bcos::scheduler::SchedulerManager&>(m_scheduler->scheduler(0));
        auto consensus = m_pbftInitializer->pbft();
        schedulerServer.registerOnSwitchTermHandler([consensus](
                                                        bcos::protocol::BlockNumber blockNumber) {
            INITIALIZER_LOG(DEBUG)
                << LOG_BADGE("Switch")
                << "Receive scheduler switch term notify of number " + std::to_string(blockNumber);
            consensus->clearExceptionProposalState(blockNumber);
        });
    }
    // Engine-driven production skips txpool/PBFT start so only EngineService writes blocks.
    if (!m_nodeConfig->engineDrivenBlockProduction())
    {
        // init the txpool
        m_txpoolInitializer->init();

        // Note: must init PBFT after txpool, in case of pbft calls txpool to verifyBlock before
        // txpool init finished
        m_pbftInitializer->init();
    }
    else
    {
        INITIALIZER_LOG(INFO) << LOG_DESC(
            "EngineDrivenBlockProduction: skip txpool/pbft/sealer init (block production via "
            "EngineService + mempool / external op-node)");
    }

    // init the frontService
    m_frontServiceInitializer->init(
        m_pbftInitializer->pbft(), m_pbftInitializer->blockSync(), m_txpoolInitializer->txpool());

    // Single-node consensus mode: build the built-in single-node consensus driver that
    // produces blocks on a timer by driving the EngineService interface like an external CL
    // (forkchoiceUpdated -> getPayload -> newPayload), bypassing txpool/sealer/pbft. The
    // EngineService owns the mempool/scheduler/commit, so the driver only needs it plus the
    // ledger (to resolve the initial head). It is always built in this mode — for
    // executor_version < 2 on the v1 scheduler, and for executor_version >= 2 on the
    // EthereumExecutor scheduler — so this is also the in-process CL path for the Engine API.
    if (m_nodeConfig->enableSingleNodeConsensus())
    {
        // prevRandao: explicit 32-byte hex from config ([consensus] prev_randao), else a
        // deterministic hash of a fixed seed (for EEST the harness pins it to the fixture's
        // currentRandom so block.prevrandao matches).
        auto const& prevRandaoCfg = m_nodeConfig->singleNodeConsensusPrevRandao();
        auto prevRandao = [&]() -> bcos::crypto::HashType {
            if (!prevRandaoCfg.empty())
            {
                return bcos::crypto::HashType(prevRandaoCfg);
            }
            constexpr std::string_view seed = "single-node-consensus";
            return bcos::crypto::keccak256Hash(
                bytesConstRef(reinterpret_cast<byte const*>(seed.data()), seed.size()));
        }();
        if (!m_engineServiceInitializer)
        {
            BOOST_THROW_EXCEPTION(
                InvalidConfig() << errinfo_comment(
                    "enable_single_node_consensus requires the EngineService to be built"));
        }
        // OP Engine API is V4; the generic driver stays on V1.
        // OP mode (executor_version>=3) additionally mandates gasLimit + Holocene
        // eip1559Params in FCU attributes (validateOpPayloadAttributes); the generic V1
        // driver must keep them absent (pre-V3 attributes reject eip1559Params). Defaults:
        // 30M gas; Canyon EIP-1559 denominator 250 / elasticity 6.
        std::optional<std::uint64_t> driverGasLimit;
        std::optional<bcos::bytes> driverEip1559Params;
        if (opStackMode)
        {
            driverGasLimit = 30'000'000ull;
            driverEip1559Params =
                bcos::bytes{0x00, 0x00, 0x00, 0xfa, 0x00, 0x00, 0x00, 0x06};
        }
        m_singleNodeConsensus = std::make_shared<single_consensus::SingleNodeConsensus>(
            *m_engineServiceInitializer->engineService(), m_ledger,
            m_nodeConfig->singleNodeConsensusBlockInterval(),
            m_nodeConfig->singleNodeConsensusProduceEmptyBlocks(), prevRandao,
            m_nodeConfig->singleNodeConsensusFeeRecipient(),
            m_nodeConfig->singleNodeConsensusFixedTimestamp(), driverGasLimit,
            driverEip1559Params);
    }

#ifdef TOOLS
    if (m_nodeConfig->enableArchive())
    {
        INITIALIZER_LOG(INFO) << LOG_BADGE("create archive service");
        m_archiveService = std::make_shared<bcos::archive::ArchiveService>(m_storage, ledger,
            m_blockStorage, m_nodeConfig->archiveListenIP(), m_nodeConfig->archiveListenPort());
    }
#endif

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

    auto schedulerFactory =
        dynamic_cast<scheduler::SchedulerManager&>(m_scheduler->scheduler(0)).getFactory();
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
            txpool->asyncNotifyBlockResult(_blockNumber, std::move(_result), std::move(_callback));
        });

    m_setBaselineSchedulerBlockNumberNotifier(
        [_rpc, groupID, nodeName](bcos::protocol::BlockNumber number) {
            INITIALIZER_LOG(DEBUG) << "Notify blocknumber: " << number;
            // Note: the interface will notify blockNumber to all rpc nodes in pro/max mode
            _rpc->asyncNotifyBlockNumber(groupID, nodeName, number, [](bcos::Error::Ptr) {});
        });

    // executor_version=2 (EthereumExecutor): keep the RPC block-number notifications flowing
    // when the ethereum baseline scheduler is the active executor version.
    if (m_setEthereumSchedulerBlockNumberNotifier)
    {
        m_setEthereumSchedulerBlockNumberNotifier(
            [_rpc, groupID, nodeName](bcos::protocol::BlockNumber number) {
                INITIALIZER_LOG(DEBUG) << "Notify blocknumber: " << number;
                _rpc->asyncNotifyBlockNumber(groupID, nodeName, number, [](bcos::Error::Ptr) {});
            });
    }

    // OP: notify RPC block-number subscribers after a VALID commit.
    if (m_setOpSchedulerBlockNumberNotifier)
    {
        m_setOpSchedulerBlockNumberNotifier(
            [_rpc, groupID, nodeName](bcos::protocol::BlockNumber number) {
                INITIALIZER_LOG(DEBUG) << "Notify blocknumber: " << number;
                _rpc->asyncNotifyBlockNumber(groupID, nodeName, number, [](bcos::Error::Ptr) {});
            });
    }

    if (m_pbftInitializer)
    {
        m_pbftInitializer->initNotificationHandlers(_rpc);
    }
}

void Initializer::initSysContract()
{
    // The pure-Ethereum EthereumExecutor (executor_version >= 2 selects it) does not support
    // FISCO system contracts (BFS/Auth precompiles): its initSysContract block would be
    // rejected and the chain could not proceed to seal. Skip the system-contract deployment
    // entirely for v2+.
    if (m_executorVersion >= scheduler_v1::ETHEREUM_EXECUTOR_VERSION)
    {
        INITIALIZER_LOG(INFO) << LOG_DESC(
            "SysInitializer: skip system-contract deployment for ethereum executor "
            "(executor_version>=2)");
        return;
    }
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

    if (m_nodeConfig->isAuthCheck() ||
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
    // Engine-driven production: leave txpool/pbft stopped.
    if (!m_nodeConfig->engineDrivenBlockProduction())
    {
        if (m_txpoolInitializer)
        {
            m_txpoolInitializer->start();
        }
        if (m_pbftInitializer)
        {
            m_pbftInitializer->start();
        }
    }

    if (m_singleNodeConsensus)
    {
        m_singleNodeConsensus->start();
    }

    if (m_frontServiceInitializer)
    {
        m_frontServiceInitializer->start();
    }
#ifdef TOOLS
    if (m_archiveService)
    {
        m_archiveService->start();
    }
#endif
}

void Initializer::stop()
{
    try
    {
        if (m_singleNodeConsensus)
        {
            m_singleNodeConsensus->stop();
        }
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
#ifdef TOOLS
        if (m_archiveService)
        {
            m_archiveService->stop();
        }
#endif
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
                        boost::lexical_cast<bcos::protocol::BlockNumber>(entry->get());
                    blockNumberFuture.set_value(blockNumber);
                }
                catch (boost::bad_lexical_cast& e)
                {
                    // Ignore the exception
                    LEDGER_LOG(INFO)
                        << "Cast blockNumber failed, may be empty, set to default value 0"
                        << LOG_KV("blockNumber str", entry->get());
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
    auto rocksDB =
        StorageInitializer::createRocksDB(stateDBPath, rocksdbOption, nodeConfig->keyPageSize());
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
            auto blockRocksDB = StorageInitializer::createRocksDB(blockDBPath, rocksdbOption);
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
        archivedNumber.set(std::to_string(currentBlockNumber + 1));
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

std::shared_ptr<bcos::storage2::AnyStorage<bcos::h256, bcos::bytes>> Initializer::mptNodeReader()
{
    if (!m_globalStateStorageInitializer)
    {
        return nullptr;
    }
    // The committed plane only: block commit merges each block's node rows (with its flat
    // state, one WriteBatch) into latestBackend(), and eth_getProof targets committed
    // headers — the pending layers of the MultiLayerStorage belong to in-flight blocks and
    // must stay invisible to proofs.
    return bcos::storage2::makeMPTNodeReader(
        m_globalStateStorageInitializer->storage().latestBackend());
}

std::function<
    std::shared_ptr<bcos::storage2::AnyStorage<executor_v1::StateKey, executor_v1::StateValue>>()>
Initializer::stateStorageProvider()
{
    if (!m_globalStateStorageInitializer)
    {
        return {};
    }
    // Captures a shared_ptr (not `this`): the provider outlives this Initializer's
    // stateStorageProvider() call by design (it rides on the RPC NodeService), and the
    // GlobalStateStorageInitializer owns the storage the forked views borrow.
    auto storageInitializer = m_globalStateStorageInitializer;
    return [storageInitializer]() {
        // COMMITTED plane per request: a fresh view over cache -> committed backend, with
        // NO in-flight pending layers. eth_getStorageAt("latest") must observe the same
        // plane as getBalance / getTransactionCount / getCode (committed ledger /
        // scheduler) — Ethereum's "latest = last committed block". A fork() view (which
        // exposes in-flight uncommitted layers) stays available as an explicit opt-in for
        // operators who want the pending window visible.
        return forkCommittedStateView(storageInitializer->storage());
    };
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
