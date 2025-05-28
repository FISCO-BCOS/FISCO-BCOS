#pragma once

#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-task/Trait.h"
#include <concepts>

namespace bcos::executor_v1
{

template <class TransactionExecutor, class Storage>
concept IsTransactionExecutor = requires(TransactionExecutor& executor, Storage& storage,
    const protocol::BlockHeader& blockHeader, const protocol::Transaction& transaction,
    int contextID, const ledger::LedgerConfig& ledgerConfig, bool call) {
    requires std::same_as<task::AwaitableReturnType<decltype(executor.executeTransaction(
                              storage, blockHeader, transaction, contextID, ledgerConfig, call))>,
        bcos::protocol::TransactionReceipt::Ptr>;

    typename TransactionExecutor::template ExecuteContext<Storage>;
    requires std::same_as<task::AwaitableReturnType<decltype(executor.createExecuteContext(
                              storage, blockHeader, transaction, contextID, ledgerConfig, call))>,
        std::unique_ptr<typename TransactionExecutor::template ExecuteContext<Storage>>>;
};

}  // namespace bcos::executor_v1