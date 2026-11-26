#pragma once
#include "Receipt.h"
#include "Transaction.h"
#include <concepts>
#include <ranges>

namespace bcos::concepts::block
{

template <class BlockNumberType>
concept BlockNumber = std::integral<BlockNumberType>;

template <class ParentInfoType>
concept ParentInfo = requires(ParentInfoType parentInfo) {
    requires std::integral<decltype(parentInfo.blockNumber)>;
    parentInfo.blockHash;
};

template <class SignatureType>
concept Signature = requires(SignatureType signature) {
    requires std::integral<decltype(signature.sealerIndex)>;
    signature.signature;
};

template <class BlockHeaderDataType>
concept BlockHeaderData = requires(BlockHeaderDataType blockHeaderData) {
    requires std::integral<decltype(blockHeaderData.version)>;
    requires std::ranges::range<decltype(blockHeaderData.parentInfo)> &&
                 ParentInfo<std::ranges::range_value_t<decltype(blockHeaderData.parentInfo)>>;
    blockHeaderData.txsRoot;
    blockHeaderData.receiptRoot;
    blockHeaderData.stateRoot;
    requires BlockNumber<decltype(blockHeaderData.blockNumber)>;
    requires std::integral<decltype(blockHeaderData.timestamp)>;
    requires std::integral<decltype(blockHeaderData.sealer)>;
    requires std::ranges::range<decltype(blockHeaderData.sealerList)>;
    blockHeaderData.extraData;
    requires std::ranges::range<decltype(blockHeaderData.consensusWeights)>;
};

template <class BlockHeaderType>
concept BlockHeader = requires(BlockHeaderType block) {
    BlockHeaderType{};
    requires BlockHeaderData<decltype(block.data)>;
    block.dataHash;
    requires std::ranges::range<decltype(block.signatureList)> &&
                 Signature<std::ranges::range_value_t<decltype(block.signatureList)>>;
};

template <class BlockType>
concept Block = requires(BlockType block) {
    BlockType{};
    requires std::integral<decltype(block.version)>;
    requires std::integral<decltype(block.type)>;
    requires BlockHeader<decltype(block.blockHeader)>;
    requires std::ranges::range<decltype(block.transactions)> &&
                 bcos::concepts::transaction::Transaction<
                     std::ranges::range_value_t<decltype(block.transactions)>>;
    requires std::ranges::range<decltype(block.receipts)> &&
                 bcos::concepts::receipt::TransactionReceipt<
                     std::ranges::range_value_t<decltype(block.receipts)>>;
    requires std::ranges::range<decltype(block.transactionsMetaData)>;  // TODO: add metadata
                                                                        // concept
    requires std::ranges::range<decltype(block.receiptsHash)> &&
                 ByteBuffer<std::ranges::range_value_t<decltype(block.receiptsHash)>>;
    requires std::ranges::range<decltype(block.nonceList)> &&
                 ByteBuffer<std::ranges::range_value_t<decltype(block.nonceList)>>;
};
}  // namespace bcos::concepts::block