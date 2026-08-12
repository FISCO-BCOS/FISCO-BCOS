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
    template <class SchedulerType, class Executor>
    static std::tuple<std::function<std::shared_ptr<scheduler::SchedulerInterface>()>,
        std::function<void(std::function<void(protocol::BlockNumber)>)>>
    build(std::shared_ptr<initializer::GlobalStateStorageInitializer> storageInitializer,
        std::shared_ptr<protocol::BlockFactory> blockFactory,
        std::shared_ptr<SchedulerType> scheduler, std::shared_ptr<txpool::TxPoolInterface> txpool,
        std::shared_ptr<protocol::TransactionSubmitResultFactory> transactionSubmitResultFactory,
        std::shared_ptr<ledger::LedgerInterface> ledger,
        std::shared_ptr<Executor> transactionExecutor, bool notifyTransactions = true)
    {
        auto baselineScheduler = std::make_shared<BaselineScheduler<initializer::GlobalStateStorage,
            Executor, SchedulerType, ledger::LedgerInterface>>(
            storageInitializer->storage(), *scheduler, *transactionExecutor, *blockFactory, *ledger,
            *txpool, *transactionSubmitResultFactory, *blockFactory->cryptoSuite()->hashImpl());
        if (notifyTransactions)
        {
            baselineScheduler->registerTransactionNotifier(
                [txpool](bcos::protocol::BlockNumber blockNumber,
                    bcos::protocol::TransactionSubmitResultsPtr result,
                    std::function<void(bcos::Error::Ptr)> callback) mutable {
                    txpool->asyncNotifyBlockResult(
                        blockNumber, std::move(result), std::move(callback));
                });
        }
        else
        {
            // Single-node consensus mode ([consensus] enable_single_node_consensus): the legacy
            // txpool is bypassed and never initialized, so a commit that fires the transaction
            // notifier must not dereference it (asyncNotifyBlockResult on an uninitialized pool
            // segfaults in MemoryStorage::batchRemoveSealedTxs). sendRawTransaction returns the
            // tx hash immediately in this mode, so no one is waiting on a notify anyway.
            baselineScheduler->registerTransactionNotifier(
                [](bcos::protocol::BlockNumber /*blockNumber*/,
                    bcos::protocol::TransactionSubmitResultsPtr /*result*/,
                    std::function<void(bcos::Error::Ptr)> callback) { callback({}); });
        }

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