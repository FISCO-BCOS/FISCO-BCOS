#pragma once

#include "SchedulerBaseImpl.h"

namespace bcos::transaction_scheduler
{

#define SERIAL_SCHEDULER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("SERIAL_SCHEDULER")

template <class MultiLayerStorage, protocol::IsTransactionReceiptFactory ReceiptFactory,
    template <typename, typename> class Executor>
class SchedulerSerialImpl : public SchedulerBaseImpl<MultiLayerStorage, ReceiptFactory, Executor>
{
public:
    using SchedulerBaseImpl<MultiLayerStorage, ReceiptFactory, Executor>::SchedulerBaseImpl;
    using SchedulerBaseImpl<MultiLayerStorage, ReceiptFactory, Executor>::multiLayerStorage;
    using SchedulerBaseImpl<MultiLayerStorage, ReceiptFactory, Executor>::receiptFactory;
    using SchedulerBaseImpl<MultiLayerStorage, ReceiptFactory, Executor>::tableNamePool;

    task::Task<std::vector<protocol::ReceiptFactoryReturnType<ReceiptFactory>>> execute(
        protocol::IsBlockHeader auto const& blockHeader,
        RANGES::input_range auto const& transactions)
    {
        auto view = multiLayerStorage().fork(true);
        std::vector<protocol::ReceiptFactoryReturnType<ReceiptFactory>> receipts;
        if constexpr (RANGES::sized_range<decltype(transactions)>)
        {
            receipts.reserve(RANGES::size(transactions));
        }

        int contextID = 0;
        Executor<decltype(view), ReceiptFactory> executor(view, receiptFactory(), tableNamePool());
        for (auto const& transaction : transactions)
        {
            receipts.emplace_back(co_await executor.execute(blockHeader, transaction, contextID++));
        }

        co_return receipts;
    }
};
}  // namespace bcos::transaction_scheduler