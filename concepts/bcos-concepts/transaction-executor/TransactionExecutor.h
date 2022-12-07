#pragma once

#include "../protocol/Block.h"
#include <bcos-task/Task.h>

namespace bcos::concepts::transaction_executor
{
template <class Impl>
class TransactionExecutorBase
{
public:
    task::Task<void> executeTransactions(block::Block auto const& block)
    {
        return impl().impl_executeTransactions(block);
    }

private:
    friend Impl;
    auto& impl() { return static_cast<Impl&>(*this); }
};
}  // namespace bcos::concepts::transaction_executor