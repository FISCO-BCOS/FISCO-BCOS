#pragma once
#include "../concepts/Basic.h"
#include "../concepts/Block.h"
#include <concepts>
#include <ranges>

namespace bcos::sync
{

template <class MessageType>
concept RequestBlockHeaders = requires(MessageType message)
{
    bcos::concepts::Serializable<MessageType>;
    MessageType{};
    std::unsigned_integral<decltype(message.currentBlockHeight)>;
    std::unsigned_integral<decltype(message.requestBlockCount)>;
};

template <class MessageType>
concept ResponseBlockHeaders = requires(MessageType message)
{
    bcos::concepts::Serializable<MessageType>;
    MessageType{};
    std::unsigned_integral<decltype(message.blockHeight)>;
    requires std::ranges::range<decltype(message.blockHeaders)> &&
        bcos::concepts::BlockHeader<std::ranges::range_value_t<decltype(message.blockHeaders)>>;
};

};  // namespace bcos::sync