#pragma once
#include "GlobalStateStorageInitializer.h"
#include "bcos-framework/dispatcher/SchedulerInterface.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/protocol/BlockFactory.h"
#include "bcos-framework/protocol/TransactionSubmitResultFactory.h"
#include "bcos-framework/txpool/TxPoolInterface.h"
#include "bcos-transaction-executor/TransactionExecutorImpl.h"
#include "bcos-transaction-scheduler/BaselineScheduler.h"
#include <memory>

namespace bcos::scheduler_v1
{
class BaselineSchedulerInitializer
{
public:
    template <class SchedulerType>
    static std::tuple<std::function<std::shared_ptr<scheduler::SchedulerInterface>()>,
        std::function<void(std::function<void(protocol::BlockNumber)>)>>
    build(std::shared_ptr<initializer::GlobalStateStorageInitializer> storageInitializer,
        std::shared_ptr<protocol::BlockFactory> blockFactory,
        std::shared_ptr<SchedulerType> scheduler,
        std::shared_ptr<txpool::TxPoolInterface> txpool,
        std::shared_ptr<protocol::TransactionSubmitResultFactory> transactionSubmitResultFactory,
        std::shared_ptr<ledger::LedgerInterface> ledger)
    {
        struct Data
        {
            initializer::GlobalStateStorageInitializer::Ptr m_storageInitializer;
            executor_v1::PrecompiledManager m_precompiledManager;
            executor_v1::TransactionExecutorImpl m_transactionExecutor;

            Data(initializer::GlobalStateStorageInitializer::Ptr storageInitializer,
                protocol::BlockFactory& blockFactory)
              : m_storageInitializer(std::move(storageInitializer)),
                m_precompiledManager(blockFactory.cryptoSuite()->hashImpl()),
                m_transactionExecutor(*blockFactory.receiptFactory(),
                    blockFactory.cryptoSuite()->hashImpl(), m_precompiledManager)
            {}
        };
        auto data = std::make_shared<Data>(std::move(storageInitializer), *blockFactory);

        auto baselineScheduler =
            std::make_shared<BaselineScheduler<initializer::GlobalStateStorage,
                decltype(data->m_transactionExecutor), SchedulerType, ledger::LedgerInterface>>(
                data->m_storageInitializer->storage(), *scheduler, data->m_transactionExecutor,
                *blockFactory, *ledger, *txpool, *transactionSubmitResultFactory,
                *blockFactory->cryptoSuite()->hashImpl());
        baselineScheduler->registerTransactionNotifier(
            [txpool](bcos::protocol::BlockNumber blockNumber,
                bcos::protocol::TransactionSubmitResultsPtr result,
                std::function<void(bcos::Error::Ptr)> callback) mutable {
                txpool->asyncNotifyBlockResult(blockNumber, std::move(result), std::move(callback));
            });

        return std::make_tuple(
            [scheduler = std::move(scheduler), baselineScheduler, data = std::move(data),
                blockFactory = std::move(blockFactory), txpool = std::move(txpool),
                transactionSubmitResultFactory = std::move(transactionSubmitResultFactory),
                ledger = std::move(ledger)]() -> std::shared_ptr<scheduler::SchedulerInterface> {
                return baselineScheduler;
            },
            [baselineScheduler](std::function<void(protocol::BlockNumber)> notifier) {
                baselineScheduler->registerBlockNumberNotifier(std::move(notifier));
            });
    }
};
}  // namespace bcos::scheduler_v1