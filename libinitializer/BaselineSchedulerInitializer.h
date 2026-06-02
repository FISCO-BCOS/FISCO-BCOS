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
        std::shared_ptr<SchedulerType> scheduler, std::shared_ptr<txpool::TxPoolInterface> txpool,
        std::shared_ptr<protocol::TransactionSubmitResultFactory> transactionSubmitResultFactory,
        std::shared_ptr<ledger::LedgerInterface> ledger,
        std::shared_ptr<executor_v1::TransactionExecutorImpl> transactionExecutor)
    {
        auto baselineScheduler = std::make_shared<BaselineScheduler<initializer::GlobalStateStorage,
            executor_v1::TransactionExecutorImpl, SchedulerType, ledger::LedgerInterface>>(
            storageInitializer->storage(), *scheduler, *transactionExecutor, *blockFactory, *ledger,
            *txpool, *transactionSubmitResultFactory, *blockFactory->cryptoSuite()->hashImpl());
        baselineScheduler->registerTransactionNotifier(
            [txpool](bcos::protocol::BlockNumber blockNumber,
                bcos::protocol::TransactionSubmitResultsPtr result,
                std::function<void(bcos::Error::Ptr)> callback) mutable {
                txpool->asyncNotifyBlockResult(blockNumber, std::move(result), std::move(callback));
            });

        return std::make_tuple(
            [scheduler = std::move(scheduler), baselineScheduler,
                storageInitializer = std::move(storageInitializer),
                transactionExecutor = std::move(transactionExecutor),
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