#pragma once

#include "../Basic.h"
#include "../protocol/Receipt.h"
#include "../protocol/Transaction.h"

namespace bcos::concepts::transacton_pool
{
template <class Impl>
class TransactionPoolBase
{
public:
    auto submitTransaction(bcos::concepts::transaction::Transaction auto transaction,
        bcos::concepts::receipt::TransactionReceipt auto& receipt)
    {
        return impl().impl_submitTransaction(transaction, receipt);
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