#pragma once
#include "Receipt.h"
#include "Transaction.h"
#include <bcos-utilities/Ranges.h>
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
    requires RANGES::range<decltype(blockHeaderData.parentInfo)> &&
                 ParentInfo<RANGES::range_value_t<decltype(blockHeaderData.parentInfo)>>;
    blockHeaderData.txsRoot;
    blockHeaderData.receiptRoot;
    blockHeaderData.stateRoot;
    requires BlockNumber<decltype(blockHeaderData.blockNumber)>;
    requires std::integral<decltype(blockHeaderData.timestamp)>;
    requires std::integral<decltype(blockHeaderData.sealer)>;
    requires RANGES::range<decltype(blockHeaderData.sealerList)>;
    blockHeaderData.extraData;
    requires RANGES::range<decltype(blockHeaderData.consensusWeights)>;
};

template <class BlockHeaderType>
concept BlockHeader = requires(BlockHeaderType block) {
    BlockHeaderType{};
    requires BlockHeaderData<decltype(block.data)>;
    block.dataHash;
    requires RANGES::range<decltype(block.signatureList)> &&
                 Signature<RANGES::range_value_t<decltype(block.signatureList)>>;
};

template <class BlockType>
concept Block = requires(BlockType block) {
    BlockType{};
    requires std::integral<decltype(block.version)>;
    requires std::integral<decltype(block.type)>;
    requires BlockHeader<decltype(block.blockHeader)>;
    requires RANGES::range<decltype(block.transactions)> &&
                 bcos::concepts::transaction::Transaction<
                     RANGES::range_value_t<decltype(block.transactions)>>;
    requires RANGES::range<decltype(block.receipts)> &&
                 bcos::concepts::receipt::TransactionReceipt<
                     RANGES::range_value_t<decltype(block.receipts)>>;
    requires RANGES::range<decltype(block.transactionsMetaData)>;  // TODO: add metadata concept
    requires RANGES::range<decltype(block.receiptsHash)> &&
                 ByteBuffer<RANGES::range_value_t<decltype(block.receiptsHash)>>;
    requires RANGES::range<decltype(block.nonceList)> &&
                 ByteBuffer<RANGES::range_value_t<decltype(block.nonceList)>>;
};
}  // namespace bcos::concepts::block