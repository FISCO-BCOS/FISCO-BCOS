#pragma once

#include "../Basic.h"
#include "../Receipt.h"
#include "../Transaction.h"

namespace bcos::concepts::scheduler
{
template <class Impl>
class Scheduler
{
public:
    void call(bcos::concepts::transaction::Transaction auto const& transaction,
        bcos::concepts::receipt::TransactionReceipt auto& receipt)
    {
        impl().impl_call(transaction, receipt);
    }

private:
    friend Impl;
    auto& impl() { return static_cast<Impl&>(*this); }
};
}  // namespace bcos::concepts::scheduler