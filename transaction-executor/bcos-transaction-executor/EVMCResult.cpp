#pragma once

#include "EVMCResult.h"
#include "bcos-codec/abi/ContractABICodec.h"
#include "bcos-protocol/TransactionStatus.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/Exceptions.h"
#include <evmc/evmc.h>
#include <boost/throw_exception.hpp>
#include <cstdint>
#include <memory>

struct UnknownEVMCStatus : public bcos::Exception
{
};

static void cleanEVMCResult(evmc_result& from)
{
    from.release = nullptr;
    from.output_data = nullptr;
    from.output_size = 0;
}

bcos::executor_v1::EVMCResult::EVMCResult(evmc_result&& from)
  : evmc_result(from), status{evmcStatusToTransactionStatus(from.status_code)}
{
    cleanEVMCResult(from);
}

bcos::executor_v1::EVMCResult::EVMCResult(evmc_result&& from, protocol::TransactionStatus _status)
  : evmc_result(from), status(_status)
{
    cleanEVMCResult(from);
}

bcos::executor_v1::EVMCResult::EVMCResult(EVMCResult&& from) noexcept
  : evmc_result(from), status{from.status}
{
    cleanEVMCResult(from);
}

bcos::executor_v1::EVMCResult& bcos::executor_v1::EVMCResult::operator=(EVMCResult&& from) noexcept
{
    evmc_result::operator=(from);
    cleanEVMCResult(from);
    return *this;
}

bcos::executor_v1::EVMCResult::~EVMCResult() noexcept
{
    if (release != nullptr)
    {
        release(static_cast<evmc_result*>(this));
        release = nullptr;
        output_data = nullptr;
        output_size = 0;
    }
}

bcos::bytes bcos::executor_v1::writeErrInfoToOutput(
    const crypto::Hash& hashImpl, std::string const& errInfo)
{
    bcos::codec::abi::ContractABICodec abi(hashImpl);
    return abi.abiIn("Error(string)", errInfo);
}

bcos::protocol::TransactionStatus bcos::executor_v1::evmcStatusToTransactionStatus(
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
    case EVMC_INVALID_INSTRUCTION:
    case EVMC_UNDEFINED_INSTRUCTION:
        return protocol::TransactionStatus::BadInstruction;
    default:
        BOOST_THROW_EXCEPTION(UnknownEVMCStatus{});
    }
}

std::tuple<bcos::protocol::TransactionStatus, bcos::bytes>
bcos::executor_v1::evmcStatusToErrorMessage(
    const bcos::crypto::Hash& hashImpl, evmc_status_code status)
{
    using namespace std::string_literals;
    bcos::bytes output;

    switch (status)
    {
    case EVMC_SUCCESS:
        return {bcos::protocol::TransactionStatus::None, {}};
    case EVMC_REVERT:
        return {bcos::protocol::TransactionStatus::RevertInstruction, {}};
    case EVMC_OUT_OF_GAS:
        return {bcos::protocol::TransactionStatus::OutOfGas,
            bcos::executor_v1::writeErrInfoToOutput(hashImpl, "Execution out of gas."s)};
    case EVMC_INSUFFICIENT_BALANCE:
        return {bcos::protocol::TransactionStatus::NotEnoughCash, {}};
    case EVMC_STACK_OVERFLOW:
        return {bcos::protocol::TransactionStatus::OutOfStack,
            bcos::executor_v1::writeErrInfoToOutput(hashImpl, "Execution stack overflow."s)};
    case EVMC_STACK_UNDERFLOW:
        return {bcos::protocol::TransactionStatus::StackUnderflow,
            bcos::executor_v1::writeErrInfoToOutput(
                hashImpl, "Execution needs more items on EVM stack."s)};
    case EVMC_INVALID_INSTRUCTION:
    case EVMC_UNDEFINED_INSTRUCTION:
        return {bcos::protocol::TransactionStatus::BadInstruction,
            bcos::executor_v1::writeErrInfoToOutput(
                hashImpl, "Execution invalid/undefined opcode."s)};
    case EVMC_BAD_JUMP_DESTINATION:
        return {bcos::protocol::TransactionStatus::BadJumpDestination,
            bcos::executor_v1::writeErrInfoToOutput(
                hashImpl, "Execution has violated the jump destination restrictions."s)};
    case EVMC_INVALID_MEMORY_ACCESS:
        return {bcos::protocol::TransactionStatus::StackUnderflow,
            bcos::executor_v1::writeErrInfoToOutput(
                hashImpl, "Execution tried to read outside memory bounds."s)};
    case EVMC_STATIC_MODE_VIOLATION:
        return {bcos::protocol::TransactionStatus::Unknown,
            bcos::executor_v1::writeErrInfoToOutput(hashImpl,
                "Execution tried to execute an operation which is restricted in static mode."s)};
    case EVMC_INTERNAL_ERROR:
    default:
        return {bcos::protocol::TransactionStatus::Unknown, {}};
    };
}

bcos::executor_v1::EVMCResult bcos::executor_v1::makeErrorEVMCResult(crypto::Hash const& hashImpl,
    protocol::TransactionStatus status, evmc_status_code evmStatus, int64_t gas,
    const std::string& errorInfo)
{
    bytes errorBytes;
    if (!errorInfo.empty())
    {
        errorBytes = writeErrInfoToOutput(hashImpl, errorInfo);
    }
    else
    {
        auto [_, errorMessage] = evmcStatusToErrorMessage(hashImpl, evmStatus);
        errorBytes.swap(errorMessage);
    }

    std::unique_ptr<uint8_t[]> output;
    size_t outputSize = 0;
    if (!errorBytes.empty())
    {
        output = std::make_unique_for_overwrite<uint8_t[]>(errorBytes.size());
        outputSize = errorBytes.size();
        ::ranges::copy(errorBytes, output.get());
    }

    return EVMCResult{
        evmc_result{.status_code = evmStatus,
            .gas_left = gas,
            .gas_refund = 0,
            .output_data = output.release(),
            .output_size = outputSize,
            .release = +[](const struct evmc_result* result) { delete[] result->output_data; },
            .create_address = {},
            .padding = {}},
        status};
}

std::ostream& operator<<(std::ostream& output, const evmc_message& message)
{
    output << "evmc_message{";
    output << "kind: " << message.kind << ", ";
    output << "flags: " << message.flags << ", ";
    output << "depth: " << message.depth << ", ";
    output << "gas: " << message.gas << ", ";
    output << "recipient: " << bcos::bytesConstRef(message.recipient.bytes) << ", ";
    output << "sender: " << bcos::bytesConstRef(message.sender.bytes) << ", ";
    output << "input_data: " << bcos::bytesConstRef(message.input_data, message.input_size) << ", ";
    output << "value: " << bcos::bytesConstRef(message.value.bytes) << ", ";
    output << "create2_salt: " << bcos::bytesConstRef(message.create2_salt.bytes) << ", ";
    output << "code_address: " << bcos::bytesConstRef(message.code_address.bytes) << "}";
    return output;
}

std::ostream& operator<<(std::ostream& output, const evmc_result& result)
{
    output << "evmc_result{";
    output << "status_code: " << result.status_code << ", ";
    output << "gas_left: " << result.gas_left << ", ";
    output << "gas_refund: " << result.gas_refund << ", ";
    output << "output_data: " << bcos::bytesConstRef(result.output_data, result.output_size)
           << ", ";
    output << "release: " << result.release << ", ";
    output << "create_address: " << bcos::bytesConstRef(result.create_address.bytes) << "}";
    return output;
}

std::ostream& operator<<(std::ostream& output, const evmc_address& address)
{
    output << bcos::bytesConstRef(address.bytes);
    return output;
}