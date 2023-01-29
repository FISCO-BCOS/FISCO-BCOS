#pragma once

#include "../protocol/Block.h"
#include <bcos-task/Trait.h>

namespace bcos::concepts::transaction_scheduler
{

template <class TransactionRangeType>
concept TransactionRange = RANGES::range<TransactionRangeType>;

template <class Impl>
concept TransactionScheduler = requires(Impl impl)
{
    requires 
};

// All auto interfaces is awaitable
template <class Impl>
class TransactionSchedulerBase
{
public:
    // Return awaitable block header
    auto executeTransactions(
        const protocol::BlockHeader& blockHeader, TransactionRange auto const& transactions)
    {
        return impl().impl_executeBlock(transactions);
    }

    // Return awaitable string
    auto getCode(std::string_view contractAddress) { return impl().impl_getCode(contractAddress); }

    // Return awaitable string
    auto getABI(std::string_view contractAddress) { return impl().impl_getABI(contractAddress); }

private:
    friend Impl;
    auto& impl() { return static_cast<Impl&>(*this); }
};
}  // namespace bcos::concepts::transaction_scheduler