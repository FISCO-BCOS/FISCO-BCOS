#pragma once
#include "../Basic.h"
#include "../Block.h"
#include <bcos-utilities/Ranges.h>
#include <concepts>

namespace bcos::concepts::sync
{

template <class MessageType>
concept RequestGetBlock = requires(MessageType message)
{
    bcos::concepts::serialize::Serializable<MessageType>;
    MessageType{};
    std::unsigned_integral<decltype(message.beginBlockNumber)>;
    std::unsigned_integral<decltype(message.endBlockNumber)>;
    std::integral<decltype(message.onlyHeader)>;
};

template <class MessageType>
concept ResponseBlock = requires(MessageType message)
{
    bcos::concepts::serialize::Serializable<MessageType>;
    MessageType{};
    std::unsigned_integral<decltype(message.blockNumber)>;
    requires RANGES::range<decltype(message.blocks)> &&
        bcos::concepts::block::Block<RANGES::range_value_t<decltype(message.blocks)>>;
};

template <class MessageType>
concept RequestTransactions = requires(MessageType message)
{
    bcos::concepts::serialize::Serializable<MessageType>;
    MessageType{};
    requires RANGES::range<decltype(message.hashes)> &&
        bcos::concepts::ByteBuffer<RANGES::range_value_t<decltype(message.hashes)>>;
    std::integral<decltype(message.withProof)>;
};

template <class MessageType>
concept ResponseTransactions = requires(MessageType message)
{
    bcos::concepts::serialize::Serializable<MessageType>;
    MessageType{};
    requires RANGES::range<decltype(message.transactions)> &&
        bcos::concepts::transaction::Transaction<
            RANGES::range_value_t<decltype(message.transactions)>>;
    requires RANGES::range<decltype(message.proofs)>;
};

template <class MessageType>
concept RequestReceipts = RequestTransactions<MessageType>;

template <class MessageType>
concept ResponseReceipts = requires(MessageType message)
{
    bcos::concepts::serialize::Serializable<MessageType>;
    MessageType{};
    requires RANGES::range<decltype(message.receipts)> &&
        bcos::concepts::receipt::TransactionReceipt<
            RANGES::range_value_t<decltype(message.receipts)>>;
    requires RANGES::range<decltype(message.proofs)>;
};

}  // namespace bcos::concepts::sync