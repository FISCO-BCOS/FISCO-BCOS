#pragma once

#include "SchedulerBaseImpl.h"
#include "bcos-framework/protocol/TransactionReceipt.h"

namespace bcos::transaction_scheduler
{

#define SERIAL_SCHEDULER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("SERIAL_SCHEDULER")

class SchedulerSerialImpl
{
public:
    friend task::Task<std::vector<protocol::TransactionReceipt::Ptr>> tag_invoke(
        tag_t<execute> /*unused*/, SchedulerSerialImpl& /*unused*/, auto& storage, auto& executor,
        protocol::BlockHeader const& blockHeader, RANGES::input_range auto const& transactions)
    {
        auto& view = storage;
        std::vector<protocol::TransactionReceipt::Ptr> receipts;
        if constexpr (RANGES::sized_range<decltype(transactions)>)
        {
            receipts.reserve(RANGES::size(transactions));
        }

        int contextID = 0;
        for (auto const& transaction : transactions)
        {
            receipts.emplace_back(co_await transaction_executor::execute(
                executor, view, blockHeader, transaction, contextID++));
        }

        co_return receipts;
    }
};
}  // namespace bcos::transaction_scheduler