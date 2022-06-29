#pragma once

#include <concepts>

namespace bcos::concepts::sync
{

template <class Impl>
class SyncBlockServerBase
{
public:
    auto onRequestBlock(auto const& request) { return impl().impl_onRequestBlock(request); }

private:
    friend Impl;
    SyncBlockServerBase() = default;
    auto& impl() { return static_cast<Impl&>(*this); }
};

template <class Impl>
concept SyncBlockServer = std::derived_from<Impl, SyncBlockServerBase<Impl>>;

}  // namespace bcos::concepts::sync