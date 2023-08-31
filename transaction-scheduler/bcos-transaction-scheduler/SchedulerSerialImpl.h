#pragma once

#include "SchedulerBaseImpl.h"
#include "bcos-framework/protocol/TransactionReceipt.h"

namespace bcos::transaction_scheduler
{

#define SERIAL_SCHEDULER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("SERIAL_SCHEDULER")

template <class MultiLayerStorage, template <typename, typename> class Executor,
    class PrecompiledManager>
class SchedulerSerialImpl
  : public SchedulerBaseImpl<MultiLayerStorage, Executor, PrecompiledManager>
{
public:
    using SchedulerBaseImpl<MultiLayerStorage, Executor, PrecompiledManager>::SchedulerBaseImpl;
    using SchedulerBaseImpl<MultiLayerStorage, Executor, PrecompiledManager>::multiLayerStorage;
    using SchedulerBaseImpl<MultiLayerStorage, Executor, PrecompiledManager>::receiptFactory;
    using SchedulerBaseImpl<MultiLayerStorage, Executor, PrecompiledManager>::precompiledManager;

    task::Task<std::vector<protocol::TransactionReceipt::Ptr>> execute(
        protocol::BlockHeader const& blockHeader, RANGES::input_range auto const& transactions)
    {
        auto view = multiLayerStorage().fork(true);
        std::vector<protocol::TransactionReceipt::Ptr> receipts;
        if constexpr (RANGES::sized_range<decltype(transactions)>)
        {
            receipts.reserve(RANGES::size(transactions));
        }

        int contextID = 0;
        Executor<decltype(view), PrecompiledManager> executor(
            view, receiptFactory(), precompiledManager());
        for (auto const& transaction : transactions)
        {
            receipts.emplace_back(co_await executor.execute(blockHeader, transaction, contextID++));
        }

        co_return receipts;
    }
};
}  // namespace bcos::transaction_scheduler