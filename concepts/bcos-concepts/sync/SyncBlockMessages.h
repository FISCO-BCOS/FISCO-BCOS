#pragma once
#include "../Basic.h"
#include "../Block.h"
#include <concepts>
#include <ranges>

namespace bcos::concepts::sync
{

template <class MessageType>
concept RequestBlock = requires(MessageType message)
{
    bcos::concepts::serialize::Serializable<MessageType>;
    MessageType{};
    std::unsigned_integral<decltype(message.beginBlockNumber)>;
    std::unsigned_integral<decltype(message.endBlockNumber)>;
};

template <class MessageType>
concept ResponseBlock = requires(MessageType message)
{
    bcos::concepts::serialize::Serializable<MessageType>;
    MessageType{};
    std::unsigned_integral<decltype(message.blockNumber)>;
    requires std::ranges::range<decltype(message.blocks)> &&
        bcos::concepts::block::Block<std::ranges::range_value_t<decltype(message.blocks)>>;
};

}  // namespace bcos::concepts::sync