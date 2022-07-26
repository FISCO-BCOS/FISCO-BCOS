#pragma once

#include "SyncBlockMessages.h"
#include <concepts>

namespace bcos::concepts::sync
{

template <class Impl>
class SyncBlockBase
{
public:
    void getBlock(RequestGetBlock auto const& request, ResponseBlock auto& response)
    {
        impl().impl_getBlock(request, response);
    }

    void getTransactions(
        RequestTransactions auto const& request, ResponseTransactions auto& response)
    {
        impl().impl_getTransactions(request, response);
    }

    void getReceipts(RequestReceipts auto const& request, ResponseTransactions auto& response)
    {
        impl().impl_getReceipts(request, response);
    }

private:
    friend Impl;
    SyncBlockBase() = default;
    auto& impl() { return static_cast<Impl&>(*this); }
};

template <class Impl>
concept SyncBlockServer = std::derived_from<Impl, SyncBlockBase<Impl>>;

}  // namespace bcos::concepts::sync