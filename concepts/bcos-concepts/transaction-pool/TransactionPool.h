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
    auto submitTransaction(auto transaction, auto& receipt)
    {
        return impl().impl_submitTransaction(transaction, receipt);
    }

    auto broadcastTransactionBuffer(concepts::transaction::Transaction auto const& transaction)
    {
        return impl().impl_broadcastTransaction(transaction);
    }

    // auto onTransaction(auto nodeID, auto const& messageID, auto const& data) {}

private:
    friend Impl;
    auto& impl() { return static_cast<Impl&>(*this); }
};

template <class Impl>
concept TransactionPool = std::derived_from<Impl, TransactionPoolBase<Impl>> ||
    std::derived_from<typename Impl::element_type,
        TransactionPoolBase<typename Impl::element_type>>;
}  // namespace bcos::concepts::transacton_pool