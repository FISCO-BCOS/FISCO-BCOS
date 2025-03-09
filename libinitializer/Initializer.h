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
#include "tools/archive-tool/ArchiveService.h"
#include <bcos-executor/src/executor/SwitchExecutorManager.h>
#include <bcos-scheduler/src/SchedulerManager.h>
#include <bcos-utilities/BoostLogInitializer.h>
#include <memory>
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
namespace initializer
{
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

    bcos::ledger::LedgerInterface::Ptr ledger() { return m_ledger; }
    std::shared_ptr<bcos::scheduler::SchedulerInterface> scheduler() { return m_scheduler; }

    FrontServiceInitializer::Ptr frontService() { return m_frontServiceInitializer; }

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
    TxPoolInitializer::Ptr m_txpoolInitializer;
    PBFTInitializer::Ptr m_pbftInitializer;
#ifdef WITH_LIGHTNODE
    // Note: since LightNodeInitializer use weak_ptr of shared_from_this, this object must be exists
    // for the whole life time
    std::shared_ptr<LightNodeInitializer> m_lightNodeInitializer;
#endif
    bcos::ledger::LedgerInterface::Ptr m_ledger;
    std::shared_ptr<bcos::scheduler::SchedulerInterface> m_scheduler;
    std::weak_ptr<bcos::executor::SwitchExecutorManager> m_switchExecutorManager;
    std::string c_consensusStorageDBName = "consensus_log";
    std::string c_fileSeparator = "/";
    std::shared_ptr<bcos::archive::ArchiveService> m_archiveService = nullptr;
    bcos::storage::TransactionalStorageInterface::Ptr m_storage = nullptr;
    // if enable SeparateBlockAndState,txs and receipts will be stored in m_blockStorage
    bcos::storage::TransactionalStorageInterface::Ptr m_blockStorage = nullptr;

    std::function<std::shared_ptr<scheduler::SchedulerInterface>()> m_baselineSchedulerHolder;
    std::function<void(std::function<void(protocol::BlockNumber)>)>
        m_setBaselineSchedulerBlockNumberNotifier;

    protocol::BlockNumber getCurrentBlockNumber(
        bcos::storage::TransactionalStorageInterface::Ptr storage = nullptr);
};

bcos::Error::Ptr traverseRocksDB(const std::string& rockDBPath,
    const std::function<bcos::Error::Ptr(const rocksdb::Slice& key, const rocksdb::Slice& value)>&
        processor);
}  // namespace initializer
}  // namespace bcos
