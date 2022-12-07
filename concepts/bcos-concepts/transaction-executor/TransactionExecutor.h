#pragma once

#include "../protocol/Block.h"
#include <bcos-task/Task.h>

namespace bcos::concepts::transaction_executor
{

// All auto interfaces is awaitable
template <class Impl>
class TransactionExecutorBase
{
public:
    void nextBlockHeader(block::Block auto const& blockHeader)
    {
        return impl_nextBlockHeader(blockHeader);
    }

    // Return awaitable receipt::TransactionReceipt
    auto call(transaction::Transaction auto const& transaction) { return impl_call(transaction); }

    // Return awaitable receipts
    auto executeTransactions(RANGES::range auto const& transactions) requires
        transaction::Transaction<RANGES::range_value_t<decltype(transactions)>>
    {
        return impl().impl_executeTransactions(transactions);
    }

    // Return awaitable bytes
    auto getABI(bytebuffer::ByteBuffer auto const& contract)
    {
        return impl().impl_getABI(contract);
    }

    // Return awaitable bytes
    auto getCode(bytebuffer::ByteBuffer auto const& contract)
    {
        return impl().impl_getCode(contract);
    }

private:
    friend Impl;
    auto& impl() { return static_cast<Impl&>(*this); }
};
}  // namespace bcos::concepts::transaction_executor