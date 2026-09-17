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
#include "bcos-framework/ledger/LedgerConfigState.h"
#include "bcos-mempool/MemPoolImpl.h"
#include "bcos-transaction-executor/TransactionExecutorImpl.h"
#include "engine/bcos-engine/EthEngineService.h"
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
        std::shared_ptr<ledger::mpt::CommitObserver> commitObserver = nullptr)
    {
        auto initializer = Ptr(new EngineServiceInitializer());
        // Engine-driven commits bypass MultiVersionScheduler's publishing wrapper, so the split
        // Eth service republishes the LedgerConfigState itself after every durable commit —
        // transaction admission reads that holder and nowhere else, so an engine-produced block
        // must not leave it stale. The MPT-pruning commitObserver below is wired through as well
        // (the release line's pruning hooks fire on the split service's commits).
        using ConcreteEngineService = bcos::engine::EthEngineService<bcos::txpool::MemPoolImpl,
            GlobalStateStorage, ExecutorType, SchedulerType>;
        auto holder =
            std::make_shared<ConcreteModel<SchedulerType, ExecutorType, ConcreteEngineService>>(
                std::move(storageInitializer), std::move(blockFactory), std::move(scheduler),
                std::move(transactionExecutor), memPool, std::move(ledger), blockTxCountLimit,
                std::move(ledgerConfigState), std::move(commitObserver));
        initializer->m_holder = holder;
        initializer->m_engineService =
            std::shared_ptr<bcos::engine::AnyEngineService>(holder, &holder->m_any);
        return initializer;
    }

    /// OP path: OpSchedulerSeam + OpScheduler delegate. The OP engine takes neither a
    /// ledger (the OP scheduler owns it) nor a maxEngineVersion (the karst profile keys
    /// payload versions on the payload timestamp) — unlike the Eth-side build(), which
    /// forwards both into its holder.
    template <class SchedulerType>
    static Ptr buildOp(std::shared_ptr<GlobalStateStorageInitializer> storageInitializer,
        bcos::protocol::BlockFactory::Ptr blockFactory, std::shared_ptr<SchedulerType> scheduler,
        bcos::txpool::MemPoolImpl& memPool,
        int64_t blockTxCountLimit = bcos::engine::c_defaultBlockTxCountLimit,
        bcos::scheduler::SchedulerInterface::Ptr delegate = nullptr,
        std::shared_ptr<bcos::engine::DACaps> daCaps = nullptr,
        bool allowSynthesizedL1Attributes = false,
        bcos::engine::OpEip1559Params eip1559 = bcos::engine::kLegacyOpEip1559Params)
    {
        auto initializer = Ptr(new EngineServiceInitializer());
        using ConcreteEngineService = bcos::engine::OpEngineService<bcos::txpool::MemPoolImpl,
            GlobalStateStorage, SchedulerType>;
        auto holder = std::make_shared<ConcreteOpModel<SchedulerType, ConcreteEngineService>>(
            std::move(storageInitializer), std::move(blockFactory), std::move(scheduler), memPool,
            blockTxCountLimit, std::move(delegate), std::move(daCaps),
            allowSynthesizedL1Attributes, eip1559);
        initializer->m_holder = holder;
        initializer->m_engineService =
            std::shared_ptr<bcos::engine::AnyEngineService>(holder, &holder->m_any);
        return initializer;
    }

    std::shared_ptr<bcos::engine::AnyEngineService> engineService() const
    {
        return m_engineService;
    }

private:
    struct Holder
    {
        virtual ~Holder() = default;
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
            std::shared_ptr<ledger::mpt::CommitObserver> commitObserver)
          : m_storageInitializer(std::move(storageInitializer)),
            m_memPool(memPool),
            m_transactionExecutor(std::move(transactionExecutor)),
            m_scheduler(std::move(scheduler)),
            m_any(std::in_place_type<ConcreteEngineService>, m_memPool,
                m_storageInitializer->storage(), *m_transactionExecutor, *m_scheduler,
                std::move(blockFactory), std::move(ledger), blockTxCountLimit,
                /*maxEngineVersion=*/static_cast<std::uint32_t>(bcos::engine::ApiVersion::V3),
                std::move(commitObserver), std::move(ledgerConfigState))
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
            bcos::engine::OpEip1559Params eip1559)
          : m_storageInitializer(std::move(storageInitializer)),
            m_memPool(memPool),
            m_scheduler(std::move(scheduler)),
            m_any(std::in_place_type<ConcreteEngineService>, m_memPool,
                m_storageInitializer->storage(), *m_scheduler, std::move(blockFactory),
                blockTxCountLimit, std::move(delegate), std::move(daCaps),
                allowSynthesizedL1Attributes, eip1559)
        {}

        std::shared_ptr<GlobalStateStorageInitializer> m_storageInitializer;
        std::reference_wrapper<bcos::txpool::MemPoolImpl> m_memPool;
        std::shared_ptr<SchedulerType> m_scheduler;
        bcos::engine::AnyEngineService m_any;
    };

    EngineServiceInitializer() = default;

    std::shared_ptr<Holder> m_holder;
    std::shared_ptr<bcos::engine::AnyEngineService> m_engineService;
};
}  // namespace bcos::initializer
