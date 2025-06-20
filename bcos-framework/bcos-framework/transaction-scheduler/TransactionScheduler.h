#pragma once
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-task/Trait.h"

namespace bcos::scheduler_v1
{

template <class TransactionSchedulerType, class Storage, class TransactionExecutor,
    class Transactions>
concept TransactionScheduler = requires(TransactionSchedulerType& scheduler, Storage& storage,
    TransactionExecutor& executor, const protocol::BlockHeader& blockHeader,
    const Transactions& transactions, const ledger::LedgerConfig& ledgerConfig) {
    requires executor_v1::TransactionExecutor<TransactionExecutor, Storage>;
    requires ::ranges::input_range<Transactions>;

    {
        scheduler.executeBlock(storage, executor, blockHeader, transactions, ledgerConfig)
    } -> task::IsAwaitableReturnValue<std::vector<protocol::TransactionReceipt::Ptr>>;
};
}  // namespace bcos::scheduler_v1