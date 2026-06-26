#pragma once
#include <bcos-concepts/ByteBuffer.h>
#include "Ledger.h"
#include "../protocol/Block.h"

namespace bcos::concepts::ledger
{

// Lightnode-specific extensions to LedgerBase.
// These methods have been moved to legacy because they are only used by the lightnode module.
template <class Impl>
class LedgerLightnodeBase : public LedgerBase<Impl>
{
public:
    template <DataFlag... Flags>
    auto getBlockByNodeList(bcos::concepts::block::BlockNumber auto blockNumber,
        bcos::concepts::block::Block auto& block, bcos::crypto::NodeIDs const& nodeList)
    {
        return static_cast<Impl&>(*this).template impl_getBlockByNodeList<Flags...>(
            blockNumber, block, nodeList);
    }

    auto getAllPeersStatus()
    {
        return static_cast<Impl&>(*this).impl_getAllPeersStatus();
    }

    template <class LedgerType, bcos::concepts::block::Block BlockType>
        requires std::derived_from<LedgerType, LedgerBase<LedgerType>> ||
                 std::derived_from<typename LedgerType::element_type,
                     LedgerBase<typename LedgerType::element_type>>
    auto sync(LedgerType& source, bool onlyHeader)
    {
        return static_cast<Impl&>(*this).template impl_sync<LedgerType, BlockType>(
            source, onlyHeader);
    }
};

template <class Impl>
concept LedgerLightnode =
    std::derived_from<Impl, LedgerLightnodeBase<Impl>> ||
    std::derived_from<typename Impl::element_type, LedgerLightnodeBase<typename Impl::element_type>>;

}  // namespace bcos::concepts::ledger
