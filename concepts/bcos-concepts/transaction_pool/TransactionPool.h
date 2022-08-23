#pragma once

#include "../Basic.h"
#include "../Receipt.h"
#include "../Transaction.h"

namespace bcos::concepts::transacton_pool
{
template <class Impl>
class TransactionPoolBase
{
public:
    void submitTransaction(bcos::concepts::transaction::Transaction auto transaction,
        bcos::concepts::receipt::TransactionReceipt auto& receipt)
    {
        impl().impl_submitTransaction(std::move(transaction), receipt);
    }

private:
    friend Impl;
    auto& impl() { return static_cast<Impl&>(*this); }
};

template <class Impl>
concept TransactionPool = std::derived_from<Impl, TransactionPoolBase<Impl>> ||
    std::derived_from<typename Impl::element_type,
        TransactionPoolBase<typename Impl::element_type>>;
}  // namespace bcos::concepts::transacton_pool