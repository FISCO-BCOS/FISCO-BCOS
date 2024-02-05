#pragma once

#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-framework/transaction-scheduler/TransactionScheduler.h"
#include "bcos-task/Wait.h"

namespace bcos::transaction_scheduler
{

#define SERIAL_SCHEDULER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("SERIAL_SCHEDULER")

class SchedulerSerialImpl
{
private:
    friend task::Task<std::vector<protocol::TransactionReceipt::Ptr>> tag_invoke(
        tag_t<executeBlock> /*unused*/, SchedulerSerialImpl& /*unused*/, auto& storage,
        auto& executor, protocol::BlockHeader const& blockHeader,
        RANGES::input_range auto const& transactions, ledger::LedgerConfig const& ledgerConfig)
    {
        std::vector<protocol::TransactionReceipt::Ptr> receipts;
        if constexpr (RANGES::sized_range<decltype(transactions)>)
        {
            receipts.reserve(RANGES::size(transactions));
        }

        int contextID = 0;
        for (auto const& transaction : transactions)
        {
            receipts.emplace_back(co_await transaction_executor::executeTransaction(executor,
                storage, blockHeader, transaction, contextID, ledgerConfig, task::syncWait));
            ++contextID;
        }

        co_return receipts;
    }
};
}  // namespace bcos::transaction_scheduler