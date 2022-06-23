#pragma once

namespace bcos::sync
{

template <class BlockSyncerType>
concept BlockSyncer = requires()
{
    typename BlockSyncerType;
};

};  // namespace bcos::sync