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
 * @brief NodeService.h
 * @file NodeService.h
 * @author: yujiechen
 * @date 2021-10-11
 */
#pragma once
#include "bcos-tool/NodeConfig.h"
#include "fisco-bcos-tars-service/Common/TarsUtils.h"
#include <bcos-framework/consensus/ConsensusInterface.h>
#include <bcos-framework/dispatcher/SchedulerInterface.h>
#include <bcos-framework/engine/AnyEngineService.h>
#include <bcos-framework/engine/DACaps.h>
#include <bcos-framework/ledger/LedgerInterface.h>
#include <bcos-framework/multigroup/ChainNodeInfo.h>
#include <bcos-framework/multigroup/GroupInfo.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/protocol/ServiceDesc.h>
#include <bcos-framework/storage2/AnyStorage.h>
#include <bcos-framework/sync/BlockSyncInterface.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-framework/txpool/TxPoolInterface.h>
#include <bcos-tars-protocol/client/LedgerServiceClient.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <servant/Application.h>
#include <functional>
#include <utility>

namespace bcos::txpool
{
class MemPoolImpl;
}  // namespace bcos::txpool

namespace bcos::rpc
{
class NodeService
{
public:
    using Ptr = std::shared_ptr<NodeService>;
    NodeService(bcos::ledger::LedgerInterface::Ptr _ledger,
        std::shared_ptr<bcos::scheduler::SchedulerInterface> _scheduler,
        bcos::txpool::TxPoolInterface::Ptr _txpool,
        bcos::consensus::ConsensusInterface::Ptr _consensus,
        bcos::sync::BlockSyncInterface::Ptr _sync, bcos::protocol::BlockFactory::Ptr _blockFactory,
        std::shared_ptr<bcos::engine::AnyEngineService> _engineService)
      : m_ledger(std::move(_ledger)),
        m_scheduler(std::move(_scheduler)),
        m_txpool(std::move(_txpool)),
        m_consensus(std::move(_consensus)),
        m_sync(std::move(_sync)),
        m_blockFactory(std::move(_blockFactory)),
        m_engineService(std::move(_engineService))
    {}
    ~NodeService() = default;

    bcos::ledger::LedgerInterface::Ptr ledger() { return m_ledger; }
    std::shared_ptr<bcos::scheduler::SchedulerInterface> scheduler() { return m_scheduler; }
    bcos::txpool::TxPoolInterface::Ptr txpool() { return m_txpool; }
    bcos::consensus::ConsensusInterface::Ptr consensus() { return m_consensus; }
    bcos::sync::BlockSyncInterface::Ptr sync() { return m_sync; }
    bcos::protocol::BlockFactory::Ptr blockFactory() { return m_blockFactory; }

    bcos::txpool::TxPoolInterface& txpoolRef() { return *m_txpool; }

    std::shared_ptr<bcos::engine::AnyEngineService>& engineService() { return m_engineService; }
    std::shared_ptr<bcos::engine::AnyEngineService> const& engineService() const
    {
        return m_engineService;
    }

    /// Single-node consensus mode (config [consensus] enable_single_node_consensus): the
    /// in-process mempool that sendRawTransaction routes to instead of txpool. Owned by the
    /// Initializer, which outlives this NodeService, so a raw pointer is safe. Unset
    /// (nullptr) in normal mode and on tars-built nodes.
    void setMemPool(bcos::txpool::MemPoolImpl& _memPool) noexcept { m_memPool = &_memPool; }
    bool memPoolAvailable() const noexcept { return m_memPool != nullptr; }
    bcos::txpool::MemPoolImpl* memPool() const noexcept { return m_memPool; }

    /// Type-erased read handle over the MPT node storage for eth_getProof (M8.3): key = node
    /// hash, value = the node's raw RLP encoding, physically stored as ordinary state rows —
    /// StateKey{"/mpt/", <32 raw digest bytes>}, i.e. "/mpt/:" + digest = 38 bytes in the
    /// default column family. Build those keys ONLY with bcos-storage
    /// KeyPrefixes.h::mptNodeStateKey; the physical form is produced and parsed solely by
    /// StateKeyResolver.
    using MPTNodeReader = bcos::storage2::AnyStorage<bcos::h256, bcos::bytes>;

    /// The handle owns its key-translating adapter (storage2::makeMPTNodeReader), but the
    /// storage underneath it is borrowed — owned by the Initializer, which must outlive this
    /// NodeService. AIR wires it in AirNodeInitializer (Initializer::mptNodeReader over the
    /// committed state backend); a tars-built NodeService has no local storage, leaves it
    /// unset, and eth_getProof answers "MPT not enabled on this node".
    /// shared_ptr because AnyStorage itself is move-only and not default-constructible.
    void setMPTNodeReader(std::shared_ptr<MPTNodeReader> _reader) noexcept
    {
        m_mptNodeReader = std::move(_reader);
    }
    std::shared_ptr<MPTNodeReader> mptNodeReader() const noexcept { return m_mptNodeReader; }

    /// Shared DA throttling caps (miner_setMaxDASize -> engine build path): ONE
    /// instance created by the Initializer, read by the engine's OP payload build.
    /// A tars-built NodeService leaves it unset and miner_setMaxDASize answers
    /// against a detached local instance (recorded, logged, never consumed).
    void setDACaps(std::shared_ptr<bcos::engine::DACaps> _caps) noexcept
    {
        m_daCaps = std::move(_caps);
    }
    std::shared_ptr<bcos::engine::DACaps> daCaps() const noexcept { return m_daCaps; }

    /// Type-erased read handle over the LATEST COMMITTED state plane of GlobalStateStorage
    /// (eth_getStorageAt's fork-a-view path): StateKey -> Entry, no MPT types.
    using StateStorage =
        bcos::storage2::AnyStorage<bcos::executor_v1::StateKey, bcos::executor_v1::StateValue>;

    /// Forks a fresh COMMITTED view of GlobalStateStorage and returns a handle that OWNS the
    /// forked view. Each call is a consistent point-in-time snapshot of the committed plane
    /// (cache -> committed backend) — in-flight uncommitted pending layers stay invisible, so
    /// "latest" means the last committed block, the same plane the ledger / scheduler serve
    /// for getBalance / getTransactionCount / getCode. The concrete storage is borrowed from
    /// the Initializer, so the provider must not outlive it (AIR wiring in
    /// AirNodeInitializer; a tars-built NodeService leaves it unset).
    using StateStorageProvider = std::function<std::shared_ptr<StateStorage>()>;

    void setStateStorageProvider(StateStorageProvider _provider) noexcept
    {
        m_stateStorageProvider = std::move(_provider);
    }
    StateStorageProvider const& stateStorageProvider() const noexcept
    {
        return m_stateStorageProvider;
    }

    /// blockTag semantics: how many blocks behind "latest" the "safe" / "finalized" tags
    /// point to (wired from [web3_rpc] safe_block_depth / finalized_block_depth). Default 0
    /// — PBFT commits are final, so safe/finalized equal "latest" unless configured.
    void setSafeBlockDepth(protocol::BlockNumber _depth) noexcept { m_safeBlockDepth = _depth; }
    protocol::BlockNumber safeBlockDepth() const noexcept { return m_safeBlockDepth; }
    void setFinalizedBlockDepth(protocol::BlockNumber _depth) noexcept
    {
        m_finalizedBlockDepth = _depth;
    }
    protocol::BlockNumber finalizedBlockDepth() const noexcept { return m_finalizedBlockDepth; }

    void setLedgerPrx(bcostars::LedgerServicePrx const& _ledgerPrx) { m_ledgerPrx = _ledgerPrx; }

    bool unreachable()
    {
        return !bcostars::checkConnection(
            "NodeService", "unreachable", m_ledgerPrx, nullptr, false);
    }

private:
    bcos::ledger::LedgerInterface::Ptr m_ledger;
    std::shared_ptr<bcos::scheduler::SchedulerInterface> m_scheduler;
    bcos::txpool::TxPoolInterface::Ptr m_txpool;
    bcos::consensus::ConsensusInterface::Ptr m_consensus;
    bcos::sync::BlockSyncInterface::Ptr m_sync;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;

    std::shared_ptr<bcos::engine::AnyEngineService> m_engineService;

    /// MPT node reader handle (owns its adapter, borrows the underlying storage); see
    /// setMPTNodeReader() for the lifetime contract.
    std::shared_ptr<MPTNodeReader> m_mptNodeReader;

    /// DA throttling caps shared with the engine (see setDACaps for the contract).
    std::shared_ptr<bcos::engine::DACaps> m_daCaps;

    /// Latest-state view provider (owns each forked view, borrows the GlobalStateStorage);
    /// see setStateStorageProvider() for the lifetime contract.
    StateStorageProvider m_stateStorageProvider;

    /// blockTag semantics: "safe"/"finalized" point latest - depth blocks behind. Default 0
    /// = "latest" (PBFT: a committed block is final); operators opt into a lag via config.
    protocol::BlockNumber m_safeBlockDepth = 0;
    protocol::BlockNumber m_finalizedBlockDepth = 0;

    /// Raw pointer to the single-node-consensus mempool (see setMemPool for lifetime).
    bcos::txpool::MemPoolImpl* m_memPool = nullptr;

    bcostars::LedgerServicePrx m_ledgerPrx;
};

class NodeServiceFactory
{
public:
    using Ptr = std::shared_ptr<NodeServiceFactory>;
    NodeServiceFactory() = default;
    virtual ~NodeServiceFactory() = default;
    NodeService::Ptr buildNodeService(std::string const& _chainID, std::string const& _groupID,
        bcos::group::ChainNodeInfo::Ptr _nodeInfo, bcos::tool::NodeConfig::Ptr _nodeConfig);

    void setEngineService(std::shared_ptr<bcos::engine::AnyEngineService> _engineService)
    {
        m_engineService = std::move(_engineService);
    }

    template <typename T, typename S, typename... Args>
    std::pair<std::shared_ptr<T>, S> createServicePrx(bcos::protocol::ServiceType _type,
        bcos::group::ChainNodeInfo::Ptr _nodeInfo, bcos::tool::NodeConfig::Ptr _nodeConfig,
        const Args&... _args)
    {
        auto withoutTarsFramework = _nodeConfig->withoutTarsFramework();
        auto serviceName = _nodeInfo->serviceName(_type);
        if (serviceName.size() == 0)
        {
            return std::make_pair(nullptr, nullptr);
        }
        auto prx = bcostars::createServantProxy<S>(serviceName);
        auto client = std::make_shared<T>(prx, _args...);

        return std::make_pair(client, prx);
    }

private:
    std::shared_ptr<bcos::engine::AnyEngineService> m_engineService;
};
}  // namespace bcos::rpc