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
 * @file Initializer.h
 * @author: yujiechen
 * @date 2021-06-11
 */
#pragma once
#include "FrontServiceInitializer.h"
#include "PBFTInitializer.h"
#include "ProtocolInitializer.h"
#include "TxPoolInitializer.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-tool/NodeConfig.h"
#include "bcos-transaction-executor/precompiled/PrecompiledManager.h"
#include "libinitializer/MultiVersionScheduler.h"
#ifdef TOOLS
#include "tools/archive-tool/ArchiveService.h"
#endif
#include <bcos-executor/src/executor/SwitchExecutorManager.h>
#include <bcos-scheduler/src/SchedulerManager.h>
#include <bcos-utilities/BoostLogInitializer.h>
#include <bcos-utilities/IOServicePool.h>
#include <oneapi/tbb/global_control.h>
#include <memory>
#include <optional>
#ifdef WITH_LIGHTNODE
#include "LightNodeInitializer.h"
#endif

namespace rocksdb
{
class Slice;
}

namespace bcos
{
namespace gateway
{
class GatewayInterface;
}
namespace scheduler
{
class SchedulerInterface;
}
namespace engine
{
class AnyEngineService;
}
namespace single_consensus
{
class SingleNodeConsensus;
}
namespace storage2
{
template <class Key, class ValueT>
class AnyStorage;
}
namespace initializer
{
class GlobalStateStorageInitializer;
class MemPoolInitializer;
class EngineServiceInitializer;

class Initializer
{
public:
    using Ptr = std::shared_ptr<Initializer>;
    Initializer() = default;
    virtual ~Initializer() { stop(); }

    virtual void start();
    virtual void stop();
    virtual void prune();

    bcos::tool::NodeConfig::Ptr nodeConfig() { return m_nodeConfig; }
    ProtocolInitializer::Ptr protocolInitializer() { return m_protocolInitializer; }
    PBFTInitializer::Ptr pbftInitializer() { return m_pbftInitializer; }
    TxPoolInitializer::Ptr txPoolInitializer() { return m_txpoolInitializer; }
    std::shared_ptr<MemPoolInitializer> memPoolInitializer() { return m_memPoolInitializer; }
    std::shared_ptr<EngineServiceInitializer> engineServiceInitializer()
    {
        return m_engineServiceInitializer;
    }
    std::shared_ptr<bcos::engine::AnyEngineService> engineService();

    std::shared_ptr<bcos::single_consensus::SingleNodeConsensus> singleNodeConsensus()
    {
        return m_singleNodeConsensus;
    }

    bcos::ledger::LedgerInterface::Ptr ledger() { return m_ledger; }
    std::shared_ptr<bcos::scheduler::SchedulerInterface> scheduler() { return m_scheduler; }

    FrontServiceInitializer::Ptr frontService() { return m_frontServiceInitializer; }

    void setIOServicePool(bcos::IOServicePool::Ptr _ioServicePool)
    {
        m_ioServicePool = std::move(_ioServicePool);
    }

    void initAirNode(std::string const& _configFilePath, std::string const& _genesisFile,
        std::shared_ptr<bcos::gateway::GatewayInterface> _gateway, const std::string& _logPath);
    void initMicroServiceNode(bcos::protocol::NodeArchitectureType _nodeArchType,
        std::string const& _configFilePath, std::string const& _genesisFile,
        std::string const& _privateKeyPath, const std::string& _logPath);

    virtual void initNotificationHandlers(bcos::rpc::RPCInterface::Ptr _rpc);

    virtual void init(bcos::protocol::NodeArchitectureType _nodeArchType,
        std::string const& _configFilePath, std::string const& _genesisFile,
        std::shared_ptr<bcos::gateway::GatewayInterface> _gateway, bool _airVersion,
        const std::string& _logPath);

    virtual void initConfig(std::string const& _configFilePath, std::string const& _genesisFile,
        std::string const& _privateKeyPath, bool _airVersion);

    /// NOTE: this should be last called
    void initSysContract();
    bcos::storage::TransactionalStorageInterface::Ptr storage() { return m_storage; }

    /// Type-erased read handle over the committed MPT node rows for eth_getProof (M8.3
    /// wiring): each call builds a fresh AnyStorage view over GlobalStateStorage's backend
    /// (the committed plane node rows merge into at block commit) — the handle owns its
    /// key-translating adapter, but the backend stays owned by this Initializer's
    /// GlobalStateStorageInitializer, so the handle must not outlive this Initializer.
    /// nullptr before initNode() built the global state storage (e.g. config-only usage).
    std::shared_ptr<bcos::storage2::AnyStorage<bcos::h256, bcos::bytes>> mptNodeReader();

    /// Provider for eth_getStorageAt's latest-state path: each call forks a fresh latest view
    /// of GlobalStateStorage and returns an AnyStorage handle owning it (see
    /// forkLatestStateView). Captures a shared_ptr to the GlobalStateStorageInitializer, so
    /// the returned provider stays valid independently of this Initializer's lifetime.
    /// Empty (default-constructed) before initNode() built the global state storage.
    std::function<std::shared_ptr<
        bcos::storage2::AnyStorage<executor_v1::StateKey, executor_v1::StateValue>>()>
    stateStorageProvider();
    bcos::Error::Ptr generateSnapshot(const std::string& snapshotPath, bool withTxAndReceipts,
        const tool::NodeConfig::Ptr& nodeConfig);
    bcos::Error::Ptr importSnapshot(
        const std::string& snapshotPath, const tool::NodeConfig::Ptr& nodeConfig);
    bcos::Error::Ptr importSnapshotToRocksDB(
        const std::string& snapshotPath, const tool::NodeConfig::Ptr& nodeConfig);

    std::string getStateDBPath(bool _airVersion) const;
    std::string getBlockDBPath(bool _airVersion) const;
    std::string getConsensusStorageDBPath(bool _airVersion) const;

private:
    bcos::tool::NodeConfig::Ptr m_nodeConfig;
    ProtocolInitializer::Ptr m_protocolInitializer;
    FrontServiceInitializer::Ptr m_frontServiceInitializer;
    bcos::IOServicePool::Ptr m_ioServicePool;
    TxPoolInitializer::Ptr m_txpoolInitializer;
    PBFTInitializer::Ptr m_pbftInitializer;
#ifdef WITH_LIGHTNODE
    // Note: since LightNodeInitializer use weak_ptr of shared_from_this, this object must be exists
    // for the whole life time
    std::shared_ptr<LightNodeInitializer> m_lightNodeInitializer;
#endif
    bcos::ledger::LedgerInterface::Ptr m_ledger;
    std::shared_ptr<scheduler_v1::MultiVersionScheduler> m_scheduler;
    std::weak_ptr<bcos::executor::SwitchExecutorManager> m_switchExecutorManager;
    std::string c_consensusStorageDBName = "consensus_log";
    std::string c_fileSeparator = "/";
#ifdef TOOLS
    std::shared_ptr<bcos::archive::ArchiveService> m_archiveService = nullptr;
#endif
    std::shared_ptr<GlobalStateStorageInitializer> m_globalStateStorageInitializer;
    std::shared_ptr<EngineServiceInitializer> m_engineServiceInitializer;
    std::shared_ptr<bcos::single_consensus::SingleNodeConsensus> m_singleNodeConsensus;
    std::shared_ptr<executor_v1::PrecompiledManager> m_precompiledManager;
    bcos::storage::TransactionalStorageInterface::Ptr m_storage = nullptr;
    // if enable SeparateBlockAndState,txs and receipts will be stored in m_blockStorage
    bcos::storage::TransactionalStorageInterface::Ptr m_blockStorage = nullptr;
    std::shared_ptr<MemPoolInitializer> m_memPoolInitializer;
    std::optional<oneapi::tbb::global_control> m_tbbGlobalControl;

    std::function<std::shared_ptr<scheduler::SchedulerInterface>()> m_baselineSchedulerHolder;
    std::function<void(std::function<void(protocol::BlockNumber)>)>
        m_setBaselineSchedulerBlockNumberNotifier;
    /// EthereumExecutor (executor_version=2) baseline scheduler holder + notifier setter. Kept
    /// as members so the holder lambda (which captures the EthereumExecutor shared_ptr) stays
    /// alive for the whole Initializer lifetime.
    std::function<std::shared_ptr<scheduler::SchedulerInterface>()> m_ethereumSchedulerHolder;
    std::function<void(std::function<void(protocol::BlockNumber)>)>
        m_setEthereumSchedulerBlockNumberNotifier;
    /// OP scheduler (executor_version>=3) for MultiVersionScheduler slot 3. Kept as a member so
    /// the OpBlockScheduler (which inherits OpSchedulerSeam's evmc::VM and holds the storage ref)
    /// stays alive for the whole Initializer lifetime.
    std::shared_ptr<scheduler::SchedulerInterface> m_opScheduler;
    /// OP-mode RPC block-number push setter: installs the callback into the concrete OpScheduler
    /// (typed, not the SchedulerInterface base); commitBlock fires it after a VALID OP block
    /// merges. Only set in OP mode (executor_version>=3).
    std::function<void(std::function<void(protocol::BlockNumber)>)>
        m_setOpSchedulerBlockNumberNotifier;
    /// Resolved executor version (0 = legacy SchedulerManager, 1 = TransactionExecutorImpl,
    /// 2 = EthereumExecutor). Cached during initNode so initSysContract can decide whether the
    /// FISCO system-contract deployment block applies (it does not for the ethereum executor).
    int m_executorVersion = 0;

    protocol::BlockNumber getCurrentBlockNumber(
        bcos::storage::TransactionalStorageInterface::Ptr storage = nullptr);
};

bcos::Error::Ptr traverseRocksDB(const std::string& rockDBPath,
    const std::function<bcos::Error::Ptr(const rocksdb::Slice& key, const rocksdb::Slice& value)>&
        processor);
}  // namespace initializer
}  // namespace bcos
