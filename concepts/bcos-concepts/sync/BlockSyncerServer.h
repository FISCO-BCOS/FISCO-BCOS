#pragma once

#include "SyncBlockMessages.h"
#include <concepts>

namespace bcos::concepts::sync
{

template <class Impl>
class SyncBlockServerBase
{
public:
    void getBlock(RequestBlock auto const& request, ResponseBlock auto& response)
    {
        impl().impl_getBlock(request, response);
    }

    void getTransactionWithProof(bcos::concepts::ByteBuffer auto const& hash,
        bcos::concepts::transaction::Transaction auto& transaction)
    {
        impl().impl_getTransactionWithProof(hash, transaction);
    }

private:
    friend Impl;
    SyncBlockServerBase() = default;
    auto& impl() { return static_cast<Impl&>(*this); }
};

template <class Impl>
concept SyncBlockServer = std::derived_from<Impl, SyncBlockServerBase<Impl>>;

}  // namespace bcos::concepts::sync