#pragma once

#include "../protocol/Transaction.h"
#include <bcos-task/Task.h>

namespace bcos::concepts::transaction_executor
{

// All auto interfaces is awaitable
template <class Impl>
class TransactionExecutorBase
{
public:
    // Return awaitable receipts
    auto execute(const protocol::Transaction& transaction)
    {
        return impl().impl_execute(transaction);
    }

private:
    friend Impl;
    auto& impl() { return static_cast<Impl&>(*this); }
};
}  // namespace bcos::concepts::transaction_executor