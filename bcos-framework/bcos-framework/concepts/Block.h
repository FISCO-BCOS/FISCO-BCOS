#pragma once
#include <concepts>
#include <ranges>
#include <type_traits>

namespace bcos::concepts
{
template <class ParentInfoType>
concept ParentInfo = requires(ParentInfoType parentInfo)
{
    std::integral<decltype(parentInfo.blockNumber)>;
    parentInfo.blockHash;
};

template <class SignatureType>
concept Signature = requires(SignatureType signature)
{
    std::integral<decltype(signature.sealerIndex)>;
    signature.signature;
};

template <class BlockHeaderDataType>
concept BlockHeaderData = requires(BlockHeaderDataType blockHeaderData)
{
    std::integral<decltype(blockHeaderData.version)>;
    requires std::ranges::range<decltype(blockHeaderData.parentInfo)> &&
        ParentInfo<std::ranges::range_value_t<decltype(blockHeaderData.parentInfo)>>;
    blockHeaderData.txRoot;
    blockHeaderData.receiptRoot;
    blockHeaderData.stateRoot;
    std::unsigned_integral<decltype(blockHeaderData.blockNumber)>;
    std::unsigned_integral<decltype(blockHeaderData.gasUsed)>;
    std::integral<decltype(blockHeaderData.timestamp)>;
    std::integral<decltype(blockHeaderData.sealer)>;
    std::ranges::range<decltype(blockHeaderData.sealerList)>;
    blockHeaderData.extraData;
    std::ranges::range<decltype(blockHeaderData.consensusWeights)>;
};

template <class BlockHeaderType>
concept BlockHeader = requires(BlockHeaderType block)
{
    BlockHeaderData<decltype(block.data)>;
    block.dataHash;
    requires std::ranges::range<decltype(block.signatureList)> &&
        Signature<std::ranges::range_value_t<decltype(block.signatureList)>>;
};
}  // namespace bcos::concepts