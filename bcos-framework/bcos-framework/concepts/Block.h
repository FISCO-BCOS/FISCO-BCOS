#pragma once
#include <concepts>
#include <ranges>
#include <type_traits>

namespace bcos::concepts
{
template <class BlockHeaderType>
concept BlockHeader = requires(BlockHeaderType blockHeader)
{
    std::integral<decltype(blockHeader.version)>;
    std::integral<decltype(blockHeader.type)>;
    blockHeader.txRoot;
    blockHeader.receiptRoot;
    blockHeader.stateRoot;
    std::unsigned_integral<decltype(blockHeader.number)>;
    std::unsigned_integral<decltype(blockHeader.gasUsed)>;
    std::integral<decltype(blockHeader.timestamp)>;
    std::integral<decltype(blockHeader.sealer)>;
    std::ranges::range<decltype(blockHeader.sealerList)>;
};
}  // namespace bcos::concepts