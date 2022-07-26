#pragma once

#include <concepts>

namespace bcos::concepts::sync
{

template <class Impl>
class BlockSyncerBase
{
public:
    void fetchAndStoreNewBlocks() { impl().impl_fetchAndStoreNewBlocks(); }

private:
    friend Impl;
    BlockSyncerBase() = default;
    auto& impl() { return static_cast<Impl&>(*this); }
};

template <class Impl>
concept BlockSyncerClient = std::derived_from<Impl, BlockSyncerBase<Impl>>;

}  // namespace bcos::concepts::sync