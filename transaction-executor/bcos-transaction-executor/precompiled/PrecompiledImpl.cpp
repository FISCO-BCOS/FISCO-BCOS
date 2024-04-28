#include "PrecompiledImpl.h"

bcos::transaction_executor::ErrorMessage bcos::transaction_executor::buildErrorMessage(
    std::string_view message, crypto::Hash const& hashImpl)
{
    bcos::codec::abi::ContractABICodec abi(hashImpl);
    auto codecOutput = abi.abiIn("Error(string)", message);
    ErrorMessage errorMessage{.buffer = std::unique_ptr<uint8_t>(new uint8_t[codecOutput.size()]),
        .size = codecOutput.size()};
    std::uninitialized_copy(codecOutput.begin(), codecOutput.end(), errorMessage.buffer.get());
    return errorMessage;
}
