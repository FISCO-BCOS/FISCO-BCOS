#pragma once
#include <concepts>
#include <ranges>

namespace bcos::sync
{

template <class MessageType>
concept RequestBlockHeadersMessage = requires(MessageType message)
{
    MessageType{};
    std::unsigned_integral<decltype(message.currentBlockHeight)>;
    std::unsigned_integral<decltype(message.requestBlockCount)>;
};

template <class MessageType>
concept ResponseBlockHeadersMessage = requires(MessageType message)
{
    MessageType{};
    std::unsigned_integral<decltype(message.blockHeight)>;
    requires std::ranges::range<decltype(message.blockHeaders)>;
};

};  // namespace bcos::sync