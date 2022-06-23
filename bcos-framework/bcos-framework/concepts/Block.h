#pragma once
#include "Basic.h"
#include "Serialize.h"
#include <concepts>
#include <ranges>
#include <type_traits>

namespace bcos::concepts::block
{

template <class BlockNumberType>
concept BlockNumber = std::integral<BlockNumberType>;

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
    BlockNumber<decltype(blockHeaderData.blockNumber)>;
    std::unsigned_integral<decltype(blockHeaderData.gasUsed)>;
    std::unsigned_integral<decltype(blockHeaderData.timestamp)>;
    std::integral<decltype(blockHeaderData.sealer)>;
    std::ranges::range<decltype(blockHeaderData.sealerList)>;
    blockHeaderData.extraData;
    std::ranges::range<decltype(blockHeaderData.consensusWeights)>;
};

template <class BlockHeaderType>
concept BlockHeader = requires(BlockHeaderType block)
{
    serialize::Serializable<BlockHeaderType>;
    BlockHeaderType{};
    BlockHeaderData<decltype(block.data)>;
    block.dataHash;
    requires std::ranges::range<decltype(block.signatureList)> &&
        Signature<std::ranges::range_value_t<decltype(block.signatureList)>>;
};

template <class BlockType>
concept Block = requires(BlockType block)
{
    serialize::Serializable<BlockType>;

    BlockType{};
    std::integral<decltype(block.version)>;
    std::integral<decltype(block.type)>;
    BlockHeader<decltype(block.blockHeader)>;
    requires std::ranges::range<decltype(block.transactions)>;          // TODO: add transaction concept
    requires std::ranges::range<decltype(block.receipts)>;              // TODO: add receipt concept
    requires std::ranges::range<decltype(block.transactionsMetaData)>;  // TODO: add metadata concept
    requires std::ranges::range<decltype(block.receiptsHash)> &&
        ByteBuffer<std::ranges::range_value_t<decltype(block.receiptsHash)>>;
    requires std::ranges::range<decltype(block.nonceList)> &&
        ByteBuffer<std::ranges::range_value_t<decltype(block.nonceList)>>;
};
}  // namespace bcos::concepts::block