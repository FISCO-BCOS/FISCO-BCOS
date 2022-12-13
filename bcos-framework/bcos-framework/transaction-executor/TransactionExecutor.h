#pragma once

#include "../protocol/Transaction.h"
#include "../storage2/Storage2.h"
#include <bcos-task/Task.h>

namespace bcos::transaction_executor
{

// All auto interfaces is awaitable
template <class Impl>
class TransactionExecutorBase
{
public:
    // Return awaitable receipts
    auto execute(const protocol::Transaction& transaction, storage2::Storage auto& storage)
    {
        return impl().impl_execute(transaction, storage);
    }

private:
    friend Impl;
    TransactionExecutorBase() = default;
    auto& impl() { return static_cast<Impl&>(*this); }
};
}  // namespace bcos::transaction_executor