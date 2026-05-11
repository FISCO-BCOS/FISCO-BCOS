#include "BaselineSchedulerInitializer.h"
#include "libinitializer/BaselineStorageInitializer.h"
#include "bcos-transaction-executor/TransactionExecutorImpl.h"
#include "bcos-transaction-scheduler/BaselineScheduler.h"
#include "bcos-transaction-scheduler/SchedulerParallelImpl.h"
#include "bcos-transaction-scheduler/SchedulerSerialImpl.h"
#include "libinitializer/Common.h"
#include <boost/throw_exception.hpp>

std::tuple<std::function<std::shared_ptr<bcos::scheduler::SchedulerInterface>()>,
    std::function<void(std::function<void(bcos::protocol::BlockNumber)>)>>
bcos::scheduler_v1::BaselineSchedulerInitializer::build(
    std::shared_ptr<initializer::BaselineStorageInitializer> storageInitializer,
    std::shared_ptr<protocol::BlockFactory> blockFactory,
    std::shared_ptr<txpool::TxPoolInterface> txpool,
    std::shared_ptr<protocol::TransactionSubmitResultFactory> transactionSubmitResultFactory,
    std::shared_ptr<ledger::LedgerInterface> ledger,
    tool::NodeConfig::BaselineSchedulerConfig const& config)
{
    struct Data
    {
        initializer::BaselineStorageInitializer::Ptr m_storageInitializer;
        executor_v1::PrecompiledManager m_precompiledManager;
        executor_v1::TransactionExecutorImpl m_transactionExecutor;

        Data(initializer::BaselineStorageInitializer::Ptr storageInitializer,
            protocol::BlockFactory& blockFactory)
          : m_storageInitializer(std::move(storageInitializer)),
            m_precompiledManager(blockFactory.cryptoSuite()->hashImpl()),
            m_transactionExecutor(*blockFactory.receiptFactory(),
                blockFactory.cryptoSuite()->hashImpl(), m_precompiledManager)
        {}
    };
    auto data = std::make_shared<Data>(std::move(storageInitializer), *blockFactory);

    auto buildBaselineHolder = [&](auto scheduler) {
        auto baselineScheduler =
            std::make_shared<BaselineScheduler<initializer::BaselineSchedulerMultiLayerStorage,
                decltype(data->m_transactionExecutor), decltype(*scheduler),
                ledger::LedgerInterface>>(data->m_storageInitializer->storage(), *scheduler,
                data->m_transactionExecutor, *blockFactory, *ledger, *txpool,
                *transactionSubmitResultFactory, *blockFactory->cryptoSuite()->hashImpl());
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
    };

    INITIALIZER_LOG(INFO) << "Initialize baseline scheduler, parallel: " << config.parallel
                          << ", grainSize: " << config.grainSize
                          << ", maxThread: " << config.maxThread;

    if (config.parallel)
    {
        auto scheduler =
            std::make_shared<SchedulerParallelImpl<initializer::BaselineSchedulerMutableStorage>>();
        scheduler->m_grainSize = config.grainSize;
        scheduler->m_maxConcurrency = config.maxThread;
        return buildBaselineHolder(std::move(scheduler));
    }
    return buildBaselineHolder(std::make_shared<SchedulerSerialImpl>());
}
