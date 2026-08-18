#pragma once

#include "GlobalStateStorageInitializer.h"
#include "bcos-framework/engine/AnyEngineService.h"
#include "bcos-mempool/MemPoolImpl.h"
#include "bcos-transaction-executor/TransactionExecutorImpl.h"
#include "engine/bcos-engine/EngineServiceImpl.h"
#include <functional>
#include <memory>
#include <utility>

namespace bcos::initializer
{
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
        bcos::scheduler::SchedulerInterface::Ptr delegate = nullptr,
        /// Tier-2: the OP composition root raises the Engine API ceiling to V4 (Isthmus+
        /// payloads are V4-only, EngineServiceImpl.h's opIsthmusPayloadVersion gate).
        std::uint32_t maxEngineVersion = static_cast<std::uint32_t>(bcos::engine::ApiVersion::V3))
    {
        auto initializer = Ptr(new EngineServiceInitializer());
        using ConcreteEngineService = bcos::engine::EngineServiceImpl<bcos::txpool::MemPoolImpl,
            GlobalStateStorage, ExecutorType, SchedulerType>;
        auto holder =
            std::make_shared<ConcreteModel<SchedulerType, ExecutorType, ConcreteEngineService>>(
                std::move(storageInitializer), std::move(blockFactory), std::move(scheduler),
                std::move(transactionExecutor), memPool, std::move(ledger), blockTxCountLimit,
                std::move(delegate), maxEngineVersion);
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
            bcos::scheduler::SchedulerInterface::Ptr delegate, std::uint32_t maxEngineVersion)
          : m_storageInitializer(std::move(storageInitializer)),
            m_memPool(memPool),
            m_transactionExecutor(std::move(transactionExecutor)),
            m_scheduler(std::move(scheduler)),
            m_any(std::in_place_type<ConcreteEngineService>, m_memPool,
                m_storageInitializer->storage(), *m_transactionExecutor, *m_scheduler,
                std::move(blockFactory), std::move(ledger), blockTxCountLimit, maxEngineVersion,
                std::move(delegate))
        {}

        std::shared_ptr<GlobalStateStorageInitializer> m_storageInitializer;
        std::reference_wrapper<bcos::txpool::MemPoolImpl> m_memPool;
        std::shared_ptr<ExecutorType> m_transactionExecutor;
        std::shared_ptr<SchedulerType> m_scheduler;
        bcos::engine::AnyEngineService m_any;
    };

    EngineServiceInitializer() = default;

    std::shared_ptr<Holder> m_holder;
    std::shared_ptr<bcos::engine::AnyEngineService> m_engineService;
};
}  // namespace bcos::initializer