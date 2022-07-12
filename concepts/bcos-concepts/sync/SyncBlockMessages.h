#pragma once
#include "../Basic.h"
#include "../Block.h"
#include <concepts>
#include <bcos-utilities/Ranges.h>

namespace bcos::concepts::sync
{

template <class MessageType>
concept RequestBlock = requires(MessageType message)
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

}  // namespace bcos::concepts::sync