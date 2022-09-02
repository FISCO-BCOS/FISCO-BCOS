#pragma once

#include "../Basic.h"
#include "../Receipt.h"
#include "../Transaction.h"

namespace bcos::concepts::scheduler
{
template <class Impl>
class SchedulerBase
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

template <class Impl>
concept Scheduler = std::derived_from<Impl, SchedulerBase<Impl>> ||
    std::derived_from<typename Impl::element_type, SchedulerBase<typename Impl::element_type>>;
}  // namespace bcos::concepts::scheduler