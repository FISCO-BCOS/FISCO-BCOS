#pragma once

#include "SchedulerBaseImpl.h"

namespace bcos::transaction_scheduler
{
template <bcos::transaction_executor::StateStorage BackendStorage,
    protocol::IsTransactionReceiptFactory ReceiptFactory,
    bcos::transaction_executor::TransactionExecutor<BackendStorage, ReceiptFactory> Executor,
    bcos::transaction_executor::StateStorage MutableStorage = storage2::memory_storage::
        MemoryStorage<transaction_executor::StateKey, transaction_executor::StateValue,
            storage2::memory_storage::Attribute(
                storage2::memory_storage::ORDERED | storage2::memory_storage::LOGICAL_DELETION)>>
class SchedulerSerialImpl
  : public SchedulerBaseImpl<BackendStorage, ReceiptFactory, Executor, MutableStorage>
{
public:
    using SchedulerBaseImpl<BackendStorage, ReceiptFactory, Executor,
        MutableStorage>::SchedulerBaseImpl;
    using SchedulerBaseImpl<BackendStorage, ReceiptFactory, Executor,
        MutableStorage>::multiLayerStorage;
    using SchedulerBaseImpl<BackendStorage, ReceiptFactory, Executor,
        MutableStorage>::receiptFactory;

    task::Task<std::vector<protocol::ReceiptFactoryReturnType<ReceiptFactory>>> executeTransactions(
        protocol::IsBlockHeader auto const& blockHeader,
        RANGES::input_range auto const& transactions)
    {
        std::vector<protocol::ReceiptFactoryReturnType<ReceiptFactory>> receipts;
        if constexpr (RANGES::sized_range<decltype(transactions)>)
        {
            receipts.reserve(RANGES::size(transactions));
        }

        int contextID = 0;
        Executor executor(multiLayerStorage(), receiptFactory());
        for (auto const& transaction : transactions)
        {
            receipts.emplace_back(co_await executor.execute(blockHeader, transaction, contextID++));
        }
        multiLayerStorage().pushMutableToImmutableFront();

        co_return receipts;
    }
};
}  // namespace bcos::transaction_scheduler