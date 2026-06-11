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

    template <class SchedulerType>
    static Ptr build(std::shared_ptr<GlobalStateStorageInitializer> storageInitializer,
        bcos::protocol::BlockFactory::Ptr blockFactory, std::shared_ptr<SchedulerType> scheduler,
        std::shared_ptr<executor_v1::TransactionExecutorImpl> transactionExecutor,
        bcos::txpool::MemPoolImpl& memPool,
        int64_t blockTxCountLimit = bcos::engine::c_defaultBlockTxCountLimit)
    {
        auto initializer = Ptr(new EngineServiceInitializer());
        using ConcreteEngineService = bcos::engine::EngineServiceImpl<bcos::txpool::MemPoolImpl,
            GlobalStateStorage, executor_v1::TransactionExecutorImpl, SchedulerType>;
        auto holder = std::make_shared<ConcreteModel<SchedulerType, ConcreteEngineService>>(
            std::move(storageInitializer), std::move(blockFactory), std::move(scheduler),
            std::move(transactionExecutor), memPool, blockTxCountLimit);
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

    template <class SchedulerType, class ConcreteEngineService>
    struct ConcreteModel final : Holder
    {
        ConcreteModel(std::shared_ptr<GlobalStateStorageInitializer> storageInitializer,
            bcos::protocol::BlockFactory::Ptr blockFactory,
            std::shared_ptr<SchedulerType> scheduler,
            std::shared_ptr<executor_v1::TransactionExecutorImpl> transactionExecutor,
            bcos::txpool::MemPoolImpl& memPool, int64_t blockTxCountLimit)
          : m_storageInitializer(std::move(storageInitializer)),
            m_memPool(memPool),
            m_transactionExecutor(std::move(transactionExecutor)),
            m_scheduler(std::move(scheduler)),
            m_engine(m_memPool, m_storageInitializer->storage(), *m_transactionExecutor,
                *m_scheduler, std::move(blockFactory), blockTxCountLimit),
            m_any(std::move(m_engine))
        {}

        std::shared_ptr<GlobalStateStorageInitializer> m_storageInitializer;
        std::reference_wrapper<bcos::txpool::MemPoolImpl> m_memPool;
        std::shared_ptr<executor_v1::TransactionExecutorImpl> m_transactionExecutor;
        std::shared_ptr<SchedulerType> m_scheduler;
        ConcreteEngineService m_engine;
        bcos::engine::AnyEngineService m_any;
    };

    EngineServiceInitializer() = default;

    std::shared_ptr<Holder> m_holder;
    std::shared_ptr<bcos::engine::AnyEngineService> m_engineService;
};
}  // namespace bcos::initializer