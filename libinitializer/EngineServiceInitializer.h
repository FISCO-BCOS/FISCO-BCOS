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
/// Wires EthEngineService or OpEngineService for production Engine API use.
///
/// - build(...)   → EthEngineService
/// - buildOp(...) → OpEngineService
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
        // The split Eth service takes no LedgerConfigState: the process-wide snapshot is published
        // once at boot (Initializer) and then by MultiVersionScheduler::commitBlock on every
        // commit (#5535), which is what transaction admission reads. The legacy EngineServiceImpl
        // used to publish it from its own build path; that parameter is intentionally not carried
        // over here -- do not re-add it to the Eth service. It stays in the signature so the
        // boot call sites can pass it uniformly; the MPT-pruning commitObserver below IS wired
        // through (the release line's pruning hooks fire on the split service's commits).
        (void)ledgerConfigState;
        using ConcreteEngineService = bcos::engine::EthEngineService<bcos::txpool::MemPoolImpl,
            GlobalStateStorage, ExecutorType, SchedulerType>;
        auto holder =
            std::make_shared<ConcreteModel<SchedulerType, ExecutorType, ConcreteEngineService>>(
                std::move(storageInitializer), std::move(blockFactory), std::move(scheduler),
                std::move(transactionExecutor), memPool, std::move(ledger), blockTxCountLimit,
                std::move(commitObserver));
        initializer->m_holder = holder;
        initializer->m_engineService =
            std::shared_ptr<bcos::engine::AnyEngineService>(holder, &holder->m_any);
        return initializer;
    }

    /// OP path: OpSchedulerSeam + OpScheduler delegate. The OP engine keeps
    /// ledger=nullptr (the OP scheduler owns the ledger) and no maxEngineVersion field —
    /// the karst profile keys payload versions on the payload timestamp — so both
    /// parameters are accepted here only to keep the boot call site explicit, and are
    /// discarded by the holder below (merge note: engine-cutover wanted them removed
    /// from the signature; karst keeps them as documented no-ops).
    template <class SchedulerType>
    static Ptr buildOp(std::shared_ptr<GlobalStateStorageInitializer> storageInitializer,
        bcos::protocol::BlockFactory::Ptr blockFactory, std::shared_ptr<SchedulerType> scheduler,
        bcos::txpool::MemPoolImpl& memPool, bcos::ledger::LedgerInterface::Ptr ledger = nullptr,
        int64_t blockTxCountLimit = bcos::engine::c_defaultBlockTxCountLimit,
        bcos::scheduler::SchedulerInterface::Ptr delegate = nullptr,
        std::uint32_t maxEngineVersion = static_cast<std::uint32_t>(bcos::engine::ApiVersion::V4),
        std::shared_ptr<bcos::engine::DACaps> daCaps = nullptr,
        bool allowSynthesizedL1Attributes = false)
    {
        auto initializer = Ptr(new EngineServiceInitializer());
        using ConcreteEngineService = bcos::engine::OpEngineService<bcos::txpool::MemPoolImpl,
            GlobalStateStorage, SchedulerType>;
        auto holder = std::make_shared<ConcreteOpModel<SchedulerType, ConcreteEngineService>>(
            std::move(storageInitializer), std::move(blockFactory), std::move(scheduler), memPool,
            std::move(ledger), blockTxCountLimit, std::move(delegate), maxEngineVersion,
            std::move(daCaps), allowSynthesizedL1Attributes);
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
            std::shared_ptr<ledger::mpt::CommitObserver> commitObserver)
          : m_storageInitializer(std::move(storageInitializer)),
            m_memPool(memPool),
            m_transactionExecutor(std::move(transactionExecutor)),
            m_scheduler(std::move(scheduler)),
            m_any(std::in_place_type<ConcreteEngineService>, m_memPool,
                m_storageInitializer->storage(), *m_transactionExecutor, *m_scheduler,
                std::move(blockFactory), std::move(ledger), blockTxCountLimit,
                /*maxEngineVersion=*/static_cast<std::uint32_t>(bcos::engine::ApiVersion::V3),
                std::move(commitObserver))
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
            bcos::ledger::LedgerInterface::Ptr ledger, int64_t blockTxCountLimit,
            bcos::scheduler::SchedulerInterface::Ptr delegate, std::uint32_t maxEngineVersion,
            std::shared_ptr<bcos::engine::DACaps> daCaps, bool allowSynthesizedL1Attributes)
          : m_storageInitializer(std::move(storageInitializer)),
            m_memPool(memPool),
            m_scheduler(std::move(scheduler)),
            m_any(std::in_place_type<ConcreteEngineService>, m_memPool,
                m_storageInitializer->storage(), *m_scheduler, std::move(blockFactory),
                blockTxCountLimit, std::move(delegate), std::move(daCaps),
                allowSynthesizedL1Attributes)
        {
            (void)ledger;
            (void)maxEngineVersion;
        }

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
