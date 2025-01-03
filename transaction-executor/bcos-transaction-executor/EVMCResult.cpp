#pragma once

#include "EVMCResult.h"
#include "bcos-codec/abi/ContractABICodec.h"
#include "bcos-utilities/Exceptions.h"
#include <evmc/evmc.h>
#include <boost/throw_exception.hpp>

struct UnknownEVMCStatus : public bcos::Exception
{
};

static void cleanEVMCResult(evmc_result& from)
{
    from.release = nullptr;
    from.output_data = nullptr;
    from.output_size = 0;
}

bcos::transaction_executor::EVMCResult::EVMCResult(evmc_result&& from)
  : evmc_result(from), status{evmcStatusToTransactionStatus(from.status_code)}
{
    cleanEVMCResult(from);
}

bcos::transaction_executor::EVMCResult::EVMCResult(
    evmc_result&& from, protocol::TransactionStatus _status)
  : evmc_result(from), status(_status)
{
    cleanEVMCResult(from);
}

bcos::transaction_executor::EVMCResult::EVMCResult(EVMCResult&& from) noexcept
  : evmc_result(from), status{from.status}
{
    cleanEVMCResult(from);
}

bcos::transaction_executor::EVMCResult& bcos::transaction_executor::EVMCResult::operator=(
    EVMCResult&& from) noexcept
{
    evmc_result::operator=(from);
    cleanEVMCResult(from);
    return *this;
}

bcos::transaction_executor::EVMCResult::~EVMCResult() noexcept
{
    if (release != nullptr)
    {
        release(static_cast<evmc_result*>(this));
        release = nullptr;
        output_data = nullptr;
        output_size = 0;
    }
}

bcos::bytes bcos::transaction_executor::writeErrInfoToOutput(
    const crypto::Hash& hashImpl, std::string const& errInfo)
{
    bcos::codec::abi::ContractABICodec abi(hashImpl);
    return abi.abiIn("Error(string)", errInfo);
}

bcos::protocol::TransactionStatus bcos::transaction_executor::evmcStatusToTransactionStatus(
    evmc_status_code status)
{
    switch (status)
    {
    case EVMC_SUCCESS:
        return protocol::TransactionStatus::None;
    case EVMC_REVERT:
        return protocol::TransactionStatus::RevertInstruction;
    case EVMC_OUT_OF_GAS:
        return protocol::TransactionStatus::OutOfGas;
    case EVMC_INSUFFICIENT_BALANCE:
        return protocol::TransactionStatus::NotEnoughCash;
    case EVMC_STACK_OVERFLOW:
        return protocol::TransactionStatus::OutOfStack;
    case EVMC_STACK_UNDERFLOW:
        return protocol::TransactionStatus::StackUnderflow;
    default:
        BOOST_THROW_EXCEPTION(UnknownEVMCStatus{});
    }
}

bcos::transaction_executor::EVMCResult bcos::transaction_executor::makeErrorEVMCResult(
    crypto::Hash const& hashImpl, protocol::TransactionStatus status, evmc_status_code evmStatus,
    int64_t gas, const std::string& errorInfo)
{
    constexpr static auto release =
        +[](const struct evmc_result* result) { delete[] result->output_data; };

    std::unique_ptr<uint8_t> output;
    size_t outputSize = 0;

    if (!errorInfo.empty())
    {
        auto codecOutput = writeErrInfoToOutput(hashImpl, errorInfo);
        output = std::unique_ptr<uint8_t>(new uint8_t[codecOutput.size()]);
        outputSize = codecOutput.size();
        std::uninitialized_copy(codecOutput.begin(), codecOutput.end(), output.get());
    }

    return EVMCResult{evmc_result{.status_code = evmStatus,
                          .gas_left = gas,
                          .gas_refund = 0,
                          .output_data = output.release(),
                          .output_size = outputSize,
                          .release = release,
                          .create_address = {},
                          .padding = {}},
        status};
}