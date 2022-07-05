#pragma once
#include <bcos-framework/gateway/GroupNodeInfo.h>

namespace bcos::concepts::sync
{

template <class Impl>
class BlockSyncerClientBase
{
public:
    void fetchAndStoreNewBlocks() { impl().impl_fetchAndStoreNewBlocks(); }

private:
    friend Impl;
    BlockSyncerClientBase() = default;
    auto& impl() { return static_cast<Impl&>(*this); }
};

template <class Impl>
concept BlockSyncerClient = std::derived_from<Impl, BlockSyncerClientBase<Impl>>;

};  // namespace bcos::concepts::sync