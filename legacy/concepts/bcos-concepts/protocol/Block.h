#pragma once
#include "Receipt.h"
#include "Transaction.h"
#include <range/v3/range/concepts.hpp>
#include <range/v3/range/traits.hpp>
#include <concepts>

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
    requires ::ranges::range<decltype(blockHeaderData.parentInfo)> &&
                 ParentInfo<::ranges::range_value_t<decltype(blockHeaderData.parentInfo)>>;
    blockHeaderData.txsRoot;
    blockHeaderData.receiptRoot;
    blockHeaderData.stateRoot;
    requires BlockNumber<decltype(blockHeaderData.blockNumber)>;
    requires std::integral<decltype(blockHeaderData.timestamp)>;
    requires std::integral<decltype(blockHeaderData.sealer)>;
    requires ::ranges::range<decltype(blockHeaderData.sealerList)>;
    blockHeaderData.extraData;
    requires ::ranges::range<decltype(blockHeaderData.consensusWeights)>;
};

template <class BlockHeaderType>
concept BlockHeader = requires(BlockHeaderType block) {
    BlockHeaderType{};
    requires BlockHeaderData<decltype(block.data)>;
    block.dataHash;
    requires ::ranges::range<decltype(block.signatureList)> &&
                 Signature<::ranges::range_value_t<decltype(block.signatureList)>>;
};

template <class BlockType>
concept Block = requires(BlockType block) {
    BlockType{};
    requires std::integral<decltype(block.version)>;
    requires std::integral<decltype(block.type)>;
    requires BlockHeader<decltype(block.blockHeader)>;
    requires ::ranges::range<decltype(block.transactions)> &&
                 bcos::concepts::transaction::Transaction<
                     ::ranges::range_value_t<decltype(block.transactions)>>;
    requires ::ranges::range<decltype(block.receipts)> &&
                 bcos::concepts::receipt::TransactionReceipt<
                     ::ranges::range_value_t<decltype(block.receipts)>>;
    requires ::ranges::range<decltype(block.transactionsMetaData)>;  // TODO: add metadata concept
    requires ::ranges::range<decltype(block.receiptsHash)> &&
                 ByteBuffer<::ranges::range_value_t<decltype(block.receiptsHash)>>;
    requires ::ranges::range<decltype(block.nonceList)> &&
                 ByteBuffer<::ranges::range_value_t<decltype(block.nonceList)>>;
};
}  // namespace bcos::concepts::block