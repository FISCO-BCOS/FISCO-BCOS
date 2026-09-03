#pragma once

#include "GlobalStateStorageInitializer.h"
#include "bcos-framework/engine/AnyEngineService.h"
#include "bcos-framework/engine/DACaps.h"
#include "bcos-mempool/MemPoolImpl.h"
#include "bcos-transaction-executor/TransactionExecutorImpl.h"
#include "engine/bcos-engine/EthEngineService.h"
#include "engine/bcos-engine/OpEngineService.h"
#include <functional>
#include <memory>
#include <utility>

namespace bcos::initializer
{
/// Production composition root for the Engine API service.
///
/// - build(...)  → EthEngineService (executor_version < 3 / single-node Eth / op_engine_rpc
/// harness)
/// - buildOp(...) → OpEngineService (executor_version >= 3 OP composition root)
///
/// EngineServiceImpl remains in-tree for parity / regression tests only.
class EngineServiceInitializer
{
public:
    using Ptr = std::shared_ptr<EngineServiceInitializer>;

    template <class SchedulerType, class ExecutorType>
    static Ptr build(std::shared_ptr<GlobalStateStorageInitializer> storageInitializer,
        bcos::protocol::BlockFactory::Ptr blockFactory, std::shared_ptr<SchedulerType> scheduler,
        std::shared_ptr<ExecutorType> transactionExecutor, bcos::txpool::MemPoolImpl& memPool,
        bcos::ledger::LedgerInterface::Ptr ledger = nullptr,
        int64_t blockTxCountLimit = bcos::engine::c_defaultBlockTxCountLimit)
    {
        auto initializer = Ptr(new EngineServiceInitializer());
        using ConcreteEngineService = bcos::engine::EthEngineService<bcos::txpool::MemPoolImpl,
            GlobalStateStorage, ExecutorType, SchedulerType>;
        auto holder =
            std::make_shared<ConcreteModel<SchedulerType, ExecutorType, ConcreteEngineService>>(
                std::move(storageInitializer), std::move(blockFactory), std::move(scheduler),
                std::move(transactionExecutor), memPool, std::move(ledger), blockTxCountLimit);
        initializer->m_holder = holder;
        initializer->m_engineService =
            std::shared_ptr<bcos::engine::AnyEngineService>(holder, &holder->m_any);
        return initializer;
    }

    /// OP composition: OpSchedulerSeam as SchedulerType + OpScheduler as execution delegate.
    template <class SchedulerType, class ExecutorType>
    static Ptr buildOp(std::shared_ptr<GlobalStateStorageInitializer> storageInitializer,
        bcos::protocol::BlockFactory::Ptr blockFactory, std::shared_ptr<SchedulerType> scheduler,
        std::shared_ptr<ExecutorType> transactionExecutor, bcos::txpool::MemPoolImpl& memPool,
        bcos::ledger::LedgerInterface::Ptr ledger = nullptr,
        int64_t blockTxCountLimit = bcos::engine::c_defaultBlockTxCountLimit,
        bcos::scheduler::SchedulerInterface::Ptr delegate = nullptr,
        std::uint32_t maxEngineVersion = static_cast<std::uint32_t>(bcos::engine::ApiVersion::V4),
        std::shared_ptr<bcos::engine::DACaps> daCaps = nullptr,
        bool allowSynthesizedL1Attributes = true)
    {
        auto initializer = Ptr(new EngineServiceInitializer());
        using ConcreteEngineService = bcos::engine::OpEngineService<bcos::txpool::MemPoolImpl,
            GlobalStateStorage, ExecutorType, SchedulerType>;
        auto holder =
            std::make_shared<ConcreteOpModel<SchedulerType, ExecutorType, ConcreteEngineService>>(
                std::move(storageInitializer), std::move(blockFactory), std::move(scheduler),
                std::move(transactionExecutor), memPool, std::move(ledger), blockTxCountLimit,
                std::move(delegate), maxEngineVersion, std::move(daCaps),
                allowSynthesizedL1Attributes);
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
            bcos::ledger::LedgerInterface::Ptr ledger, int64_t blockTxCountLimit)
          : m_storageInitializer(std::move(storageInitializer)),
            m_memPool(memPool),
            m_transactionExecutor(std::move(transactionExecutor)),
            m_scheduler(std::move(scheduler)),
            m_any(std::in_place_type<ConcreteEngineService>, m_memPool,
                m_storageInitializer->storage(), *m_transactionExecutor, *m_scheduler,
                std::move(blockFactory), std::move(ledger), blockTxCountLimit)
        {}

        std::shared_ptr<GlobalStateStorageInitializer> m_storageInitializer;
        std::reference_wrapper<bcos::txpool::MemPoolImpl> m_memPool;
        std::shared_ptr<ExecutorType> m_transactionExecutor;
        std::shared_ptr<SchedulerType> m_scheduler;
        bcos::engine::AnyEngineService m_any;
    };

    template <class SchedulerType, class ExecutorType, class ConcreteEngineService>
    struct ConcreteOpModel final : Holder
    {
        ConcreteOpModel(std::shared_ptr<GlobalStateStorageInitializer> storageInitializer,
            bcos::protocol::BlockFactory::Ptr blockFactory,
            std::shared_ptr<SchedulerType> scheduler,
            std::shared_ptr<ExecutorType> transactionExecutor, bcos::txpool::MemPoolImpl& memPool,
            bcos::ledger::LedgerInterface::Ptr ledger, int64_t blockTxCountLimit,
            bcos::scheduler::SchedulerInterface::Ptr delegate, std::uint32_t maxEngineVersion,
            std::shared_ptr<bcos::engine::DACaps> daCaps, bool allowSynthesizedL1Attributes)
          : m_storageInitializer(std::move(storageInitializer)),
            m_memPool(memPool),
            m_transactionExecutor(std::move(transactionExecutor)),
            m_scheduler(std::move(scheduler)),
            m_any(std::in_place_type<ConcreteEngineService>, m_memPool,
                m_storageInitializer->storage(), *m_scheduler, std::move(blockFactory),
                std::move(ledger), blockTxCountLimit, maxEngineVersion, std::move(delegate),
                std::move(daCaps), allowSynthesizedL1Attributes)
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
