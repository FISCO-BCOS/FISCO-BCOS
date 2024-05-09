#include "PrecompiledImpl.h"

bcos::transaction_executor::ErrorMessage bcos::transaction_executor::buildEncodeErrorMessage(
    std::string_view message, crypto::Hash const& hashImpl)
{
    bcos::codec::abi::ContractABICodec abi(hashImpl);
    auto codecOutput = abi.abiIn("Error(string)", message);
    ErrorMessage errorMessage{
        .buffer = new uint8_t[codecOutput.size()], .size = codecOutput.size()};
    std::uninitialized_copy(codecOutput.begin(), codecOutput.end(), errorMessage.buffer);
    return errorMessage;
}

bcos::transaction_executor::ErrorMessage bcos::transaction_executor::buildErrorMessage(
    std::string_view message)
{
    ErrorMessage errorMessage{.buffer = new uint8_t[message.size()], .size = message.size()};
    std::uninitialized_copy(message.begin(), message.end(), errorMessage.buffer);
    return errorMessage;
}
