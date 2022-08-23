#pragma once
#include "../protocol/LogEntry.h"
#include <boost/type_traits.hpp>
#include <concepts>
#include <span>
#include <string_view>
#include <type_traits>

namespace bcos::executor::serial
{
template <class MessageType>
concept CommonFields = requires(MessageType message)
{
    std::unsigned_integral<decltype(message.gasAvailable)>;
    std::convertible_to<decltype(message.data), std::span<std::byte const>>;
    std::integral<decltype(message.staticCall)>;
};

template <class MessageType>
concept RequestMessage = requires(MessageType message)
{
    CommonFields<MessageType>;
    std::convertible_to<decltype(message.origin), std::string_view>;
    std::convertible_to<decltype(message.from), std::string_view>;
    std::convertible_to<decltype(message.to), std::string_view>;
    std::convertible_to<decltype(message.abi), std::string_view>;
};

template <class MessageType>
concept ResponseMessage = requires(MessageType message)
{
    CommonFields<MessageType>;
    std::integral<decltype(message.status)>;
    std::convertible_to<decltype(message.message), std::string_view>;
    std::convertible_to<decltype(message.newEVMContractAddress), std::string_view>;
    requires RANGES::range<decltype(message.logEntries)>&& std::convertible_to<
        RANGES::range_value_t<decltype(message.logEntries)>, bcos::protocol::LogEntry>;
};

template <class ExecutorType>
concept Executor = requires(ExecutorType executor,
    typename boost::function_traits<decltype(&ExecutorType::execute)>::arg1_type executeArg1)
{
    {
        executeArg1
    }
    ->RequestMessage;
    {
        executor.execute(executeArg1)
    }
    ->ResponseMessage;
};

// class SerialTransactionExecutor
// {
// public:
//     bcos::protocol::ExecutionMessage::UniquePtr executeTransaction(
//         bcos::protocol::ExecutionMessage::UniquePtr input) = 0;
// };
}  // namespace bcos::executor::serial