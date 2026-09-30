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
 * @file EngineServiceInitializer.h
 * @brief Engine service composition root (Eth/Op wiring)
 */

#pragma once

#include "GlobalStateStorageInitializer.h"
#include "bcos-framework/engine/AnyEngineService.h"
#include "bcos-framework/engine/DACaps.h"
#include "bcos-framework/engine/OpCanonicalReader.h"
#include "bcos-framework/ledger/LedgerConfigState.h"
#include "bcos-mempool/MemPoolImpl.h"
#include "bcos-transaction-executor/TransactionExecutorImpl.h"
#include "engine/bcos-engine/EthEngineService.h"
#include "engine/bcos-engine/OpCanonicalReaderImpl.h"
#include "engine/bcos-engine/OpEngineService.h"
#include <bcos-ledger/mpt/CommitObserver.h>
#include <functional>
#include <memory>
#include <utility>

namespace bcos::initializer
{
/// Wires the Engine API (forkchoiceUpdated / getPayload / newPayload) to a scheduler +
/// executor pipeline. Called from Initializer when engine-driven block production is enabled:
///   * build(...)   → EthEngineService (executor_version==2 + single-node consensus or
///   op_engine_rpc, and also the executor_version<2 engineApiForV1Only escape, which boots
///   the same service over the v1 TransactionExecutorImpl)
///   * buildOp(...) → OpEngineService (executor_version==3, the genesis-frozen OPSTACK slot)
/// executor_version alone does not enable the Engine API on v2 chains. Both lanes bypass
/// MultiVersionScheduler's publishing wrapper, so each keeps the LedgerConfigState holder
/// current itself — transaction admission reads it and nowhere else: build() hands the holder
/// to EthEngineService, which republishes after every durable commit; buildOp() does not take
/// one at all, and the initializer instead installs the republish notifier OpScheduler fires
/// after every OP commit (OpLedgerConfigRepublish.h explains why the engine must not publish
/// there).
class EngineServiceInitializer
{
public:
    using Ptr = std::shared_ptr<EngineServiceInitializer>;

    template <class SchedulerType, class ExecutorType>
    static Ptr build(std::shared_ptr<GlobalStateStorageInitializer> storageInitializer,
        bcos::protocol::BlockFactory::Ptr blockFactory, std::shared_ptr<SchedulerType> scheduler,
        std::shared_ptr<ExecutorType> transactionExecutor, bcos::txpool::MemPoolImpl& memPool,
        bcos::ledger::LedgerInterface::Ptr ledger = nullptr,
        int64_t blockTxCountLimit = bcos::engine::c_defaultBlockTxCountLimit,
        bcos::ledger::LedgerConfigState::Ptr ledgerConfigState = nullptr,
        std::shared_ptr<ledger::mpt::CommitObserver> commitObserver = nullptr,
        std::shared_ptr<engine::engine_common::IExternalPayloadVerifier<GlobalStateStorage>>
            externalPayloadVerifier = nullptr,
        std::shared_ptr<engine::engine_common::ClSyncCoordination> clSync = nullptr,
        bool allowBlobTransactions = false)
    {
        auto initializer = Ptr(new EngineServiceInitializer());
        using ConcreteEngineService = bcos::engine::EthEngineService<bcos::txpool::MemPoolImpl,
            GlobalStateStorage, ExecutorType, SchedulerType>;
        auto holder =
            std::make_shared<ConcreteModel<SchedulerType, ExecutorType, ConcreteEngineService>>(
                std::move(storageInitializer), std::move(blockFactory), std::move(scheduler),
                std::move(transactionExecutor), memPool, std::move(ledger), blockTxCountLimit,
                std::move(ledgerConfigState), std::move(commitObserver),
                std::move(externalPayloadVerifier), std::move(clSync), allowBlobTransactions);
        initializer->m_holder = holder;
        initializer->m_engineService =
            std::shared_ptr<bcos::engine::AnyEngineService>(holder, &holder->m_any);
        return initializer;
    }

    /// A block-commit delegate that keeps an unfinalized window (OpScheduler<MLS>): the read
    /// plane (D1 §10.2) is wired only for such a delegate — a plain SchedulerInterface (or
    /// nullptr) leaves every read on the finalized plane.
    template <class DelegateType>
    static constexpr bool c_hasUnfinalizedWindow =
        requires(DelegateType& delegate, bcos::h256 const& hash) {
            delegate.viewAt(hash);
            delegate.setCanonicalHeadProvider(nullptr);
        };

    /// OP path: OpSchedulerSeam + OpScheduler delegate. The ledger and the max Engine API
    /// version are not parameters: the engine keeps ledger=nullptr (the OP scheduler owns
    /// the ledger) and OpEngineService has no maxEngineVersion field.
    ///
    /// With an OpScheduler delegate this also wires the read plane: the scheduler's own reads
    /// (call/getCode/getPendingStorageAt) follow the tracker head, the payload build seals
    /// against the parent's chain view, and opCanonicalReader() answers the RPC endpoints.
    template <class SchedulerType, class DelegateType = bcos::scheduler::SchedulerInterface>
    static Ptr buildOp(std::shared_ptr<GlobalStateStorageInitializer> storageInitializer,
        bcos::protocol::BlockFactory::Ptr blockFactory, std::shared_ptr<SchedulerType> scheduler,
        bcos::txpool::MemPoolImpl& memPool,
        int64_t blockTxCountLimit = bcos::engine::c_defaultBlockTxCountLimit,
        std::shared_ptr<DelegateType> delegate = nullptr,
        std::shared_ptr<bcos::engine::DACaps> daCaps = nullptr,
        bool allowSynthesizedL1Attributes = false,
        int64_t unfinalizedWindow = bcos::engine::c_defaultUnfinalizedWindow)
    {
        auto initializer = Ptr(new EngineServiceInitializer());
        using ConcreteEngineService = bcos::engine::OpEngineService<bcos::txpool::MemPoolImpl,
            GlobalStateStorage, SchedulerType>;
        auto holder = std::make_shared<ConcreteOpModel<SchedulerType, ConcreteEngineService>>(
            std::move(storageInitializer), blockFactory, std::move(scheduler), memPool,
            blockTxCountLimit, delegate, std::move(daCaps), allowSynthesizedL1Attributes,
            unfinalizedWindow);
        initializer->m_holder = holder;
        initializer->m_engineService =
            std::shared_ptr<bcos::engine::AnyEngineService>(holder, &holder->m_any);
        if constexpr (c_hasUnfinalizedWindow<DelegateType>)
        {
            if (delegate)
            {
                auto& service = *holder->m_service;
                // The scheduler is also owned by MultiVersionScheduler and may outlive this
                // holder at shutdown: a head read after the engine is gone answers "no head"
                // (finalized plane) instead of touching a destroyed tracker.
                delegate->setCanonicalHeadProvider(
                    [weak = std::weak_ptr<Holder>(holder),
                        &service]() -> std::optional<bcos::crypto::HashType> {
                        auto keepAlive = weak.lock();
                        if (!keepAlive)
                        {
                            return std::nullopt;
                        }
                        auto head = service.trackedHead();
                        return head ? std::optional(head->hash) : std::nullopt;
                    });
                service.setChainViewProvider(
                    [delegate](bcos::h256 const& hash) { return delegate->viewAt(hash); });
                initializer->m_opCanonicalReader = std::make_shared<
                    bcos::engine::OpCanonicalReaderImpl<ConcreteEngineService, DelegateType>>(
                    std::shared_ptr<ConcreteEngineService>(holder, holder->m_service.get()),
                    delegate, std::move(blockFactory));
            }
        }
        return initializer;
    }

    std::shared_ptr<bcos::engine::AnyEngineService> engineService() const
    {
        return m_engineService;
    }

    /// The OP lane's read facade (D1 §10.2); null on the Eth lane and when buildOp got no
    /// OpScheduler delegate. Keeps the holder (and so the engine service) alive through an
    /// aliasing shared_ptr, like the engineService() handle does.
    bcos::engine::OpCanonicalReader::Ptr opCanonicalReader() const { return m_opCanonicalReader; }

private:
    struct Holder
    {
        virtual ~Holder() = default;
    };

    /// Non-owning EngineServiceConcept adapter: lets the OP holder keep its concrete service
    /// addressable (the read-plane wiring above needs trackedHead / setChainViewProvider,
    /// which the type-erased AnyEngineService does not expose) while m_any still serves the
    /// RPC layer. The pointee is owned by the same holder and destroyed after m_any.
    template <class ConcreteEngineService>
    struct EngineServiceRef
    {
        ConcreteEngineService* service = nullptr;

        task::Task<std::vector<std::string>> exchangeCapabilities(
            std::vector<std::string> remoteCapabilities)
        {
            co_return co_await service->exchangeCapabilities(std::move(remoteCapabilities));
        }
        task::Task<bcos::engine::ForkchoiceUpdatedResult> updateForkchoice(
            const bcos::engine::ForkchoiceState& forkchoiceState,
            const bcos::engine::PayloadAttributes* payloadAttributes, std::uint32_t version)
        {
            co_return co_await service->updateForkchoice(
                forkchoiceState, payloadAttributes, version);
        }
        task::Task<bcos::engine::GetPayloadResult> getPayload(
            const bcos::engine::PayloadID& payloadId, std::uint32_t version)
        {
            co_return co_await service->getPayload(payloadId, version);
        }
        task::Task<bcos::engine::PayloadStatus> newPayload(
            const bcos::engine::NewPayloadRequest& request, std::uint32_t version)
        {
            co_return co_await service->newPayload(request, version);
        }
        std::optional<bcos::protocol::BlockNumber> getSafeBlockNumber() const
        {
            return service->getSafeBlockNumber();
        }
        std::optional<bcos::protocol::BlockNumber> getFinalizedBlockNumber() const
        {
            return service->getFinalizedBlockNumber();
        }
        std::optional<bcos::protocol::BlockNumber> getHeadBlockNumber() const
        {
            return service->getHeadBlockNumber();
        }
    };

    template <class SchedulerType, class ExecutorType, class ConcreteEngineService>
    struct ConcreteModel final : Holder
    {
        ConcreteModel(std::shared_ptr<GlobalStateStorageInitializer> storageInitializer,
            bcos::protocol::BlockFactory::Ptr blockFactory,
            std::shared_ptr<SchedulerType> scheduler,
            std::shared_ptr<ExecutorType> transactionExecutor, bcos::txpool::MemPoolImpl& memPool,
            bcos::ledger::LedgerInterface::Ptr ledger, int64_t blockTxCountLimit,
            bcos::ledger::LedgerConfigState::Ptr ledgerConfigState,
            std::shared_ptr<ledger::mpt::CommitObserver> commitObserver,
            std::shared_ptr<engine::engine_common::IExternalPayloadVerifier<GlobalStateStorage>>
                externalPayloadVerifier,
            std::shared_ptr<engine::engine_common::ClSyncCoordination> clSync,
            bool allowBlobTransactions)
          : m_storageInitializer(std::move(storageInitializer)),
            m_memPool(memPool),
            m_transactionExecutor(std::move(transactionExecutor)),
            m_scheduler(std::move(scheduler)),
            m_any(std::in_place_type<ConcreteEngineService>, m_memPool,
                m_storageInitializer->storage(), *m_transactionExecutor, *m_scheduler,
                std::move(blockFactory), std::move(ledger), blockTxCountLimit,
                /*maxEngineVersion=*/static_cast<std::uint32_t>(bcos::engine::ApiVersion::V3),
                std::move(commitObserver), std::move(ledgerConfigState),
                std::move(externalPayloadVerifier), std::move(clSync), allowBlobTransactions)
        {}

        std::shared_ptr<GlobalStateStorageInitializer> m_storageInitializer;
        std::reference_wrapper<bcos::txpool::MemPoolImpl> m_memPool;
        std::shared_ptr<ExecutorType> m_transactionExecutor;
        std::shared_ptr<SchedulerType> m_scheduler;
        bcos::engine::AnyEngineService m_any;
    };

    template <class SchedulerType, class ConcreteEngineService>
    struct ConcreteOpModel final : Holder
    {
        ConcreteOpModel(std::shared_ptr<GlobalStateStorageInitializer> storageInitializer,
            bcos::protocol::BlockFactory::Ptr blockFactory,
            std::shared_ptr<SchedulerType> scheduler, bcos::txpool::MemPoolImpl& memPool,
            int64_t blockTxCountLimit, bcos::scheduler::SchedulerInterface::Ptr delegate,
            std::shared_ptr<bcos::engine::DACaps> daCaps, bool allowSynthesizedL1Attributes,
            int64_t unfinalizedWindow)
          : m_storageInitializer(std::move(storageInitializer)),
            m_memPool(memPool),
            m_scheduler(std::move(scheduler)),
            m_service(
                std::make_unique<ConcreteEngineService>(m_memPool, m_storageInitializer->storage(),
                    *m_scheduler, std::move(blockFactory), blockTxCountLimit, std::move(delegate),
                    std::move(daCaps), allowSynthesizedL1Attributes, unfinalizedWindow)),
            m_any(std::in_place_type<EngineServiceRef<ConcreteEngineService>>,
                EngineServiceRef<ConcreteEngineService>{m_service.get()})
        {}

        std::shared_ptr<GlobalStateStorageInitializer> m_storageInitializer;
        std::reference_wrapper<bcos::txpool::MemPoolImpl> m_memPool;
        std::shared_ptr<SchedulerType> m_scheduler;
        /// Declared before m_any: the adapter inside m_any points at it.
        std::unique_ptr<ConcreteEngineService> m_service;
        bcos::engine::AnyEngineService m_any;
    };

    EngineServiceInitializer() = default;

    std::shared_ptr<Holder> m_holder;
    std::shared_ptr<bcos::engine::AnyEngineService> m_engineService;
    bcos::engine::OpCanonicalReader::Ptr m_opCanonicalReader;
};
}  // namespace bcos::initializer
