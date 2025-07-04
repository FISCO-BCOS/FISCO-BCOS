#pragma once

#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-task/Trait.h"

namespace bcos::executor_v1
{

template <class TransactionExecutorType, class Storage>
concept TransactionExecutor = requires(TransactionExecutorType& executor, Storage& storage,
    const protocol::BlockHeader& blockHeader, const protocol::Transaction& transaction,
    int contextID, const ledger::LedgerConfig& ledgerConfig, bool call) {
    {
        executor.executeTransaction(
            storage, blockHeader, transaction, contextID, ledgerConfig, call)
    } -> task::IsAwaitableReturnValue<bcos::protocol::TransactionReceipt::Ptr>;

    typename TransactionExecutorType::template ExecuteContext<Storage>;
    requires std::move_constructible<
        typename TransactionExecutorType::template ExecuteContext<Storage>>;

    {
        executor.createExecuteContext(
            storage, blockHeader, transaction, contextID, ledgerConfig, call)
    } -> task::IsAwaitableReturnValue<
        typename TransactionExecutorType::template ExecuteContext<Storage>>;
};

}  // namespace bcos::executor_v1