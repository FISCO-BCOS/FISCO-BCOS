#pragma once

#include "ExecutiveWrapper.h"
#include "bcos-executor/src/CallParameters.h"
#include "bcos-transaction-executor/EVMCResult.h"
#include "bcos-transaction-executor/precompiled/PrecompiledImpl.h"
#include <evmc/evmc.h>
#include <boost/throw_exception.hpp>
#include <memory>
#include <utility>

namespace bcos::transaction_executor
{

inline void createAuthTable(auto& storage, protocol::BlockHeader const& blockHeader,
    evmc_message const& message, evmc_address const& origin, std::string_view tableName,
    ExternalCaller auto&& externalCaller, auto& precompiledManager, int64_t contextID, int64_t seq)
{
    auto contractAddress = address2HexString(message.code_address);
    auto originAddress = address2HexString(origin);
    auto senderAddress = address2HexString(message.sender);
    auto executive = buildLegacyExecutive(storage, blockHeader, contractAddress,
        std::forward<decltype(externalCaller)>(externalCaller), precompiledManager, contextID, seq,
        true);

    executive->creatAuthTable(tableName, originAddress, senderAddress, blockHeader.version());
}

inline std::optional<EVMCResult> checkAuth(auto& storage, protocol::BlockHeader const& blockHeader,
    evmc_message const& message, evmc_address const& origin, ExternalCaller auto&& externalCaller,
    auto& precompiledManager, int64_t contextID, int64_t seq, crypto::Hash const& hashImpl)
{
    auto contractAddress = address2HexString(message.code_address);
    auto executive = buildLegacyExecutive(storage, blockHeader, contractAddress,
        std::forward<decltype(externalCaller)>(externalCaller), precompiledManager, contextID, seq,
        true);

    auto params = std::make_unique<executor::CallParameters>(executor::CallParameters::MESSAGE);
    params->senderAddress = address2HexString(message.sender);
    params->codeAddress = std::move(contractAddress);
    params->receiveAddress = address2HexString(message.recipient);
    params->origin = address2HexString(origin);
    params->data.assign(message.input_data, message.input_data + message.input_size);
    params->gas = message.gas;
    params->staticCall = (message.flags & EVMC_STATIC) != 0;
    params->create = (message.kind == EVMC_CREATE);
    auto result = executive->checkAuth(params);

    if (!result)
    {
        auto buffer = std::unique_ptr<uint8_t>(new uint8_t[params->data.size()]);
        std::uninitialized_copy(params->data.begin(), params->data.end(), buffer.get());
        return std::make_optional(
            EVMCResult{evmc_result{.status_code = static_cast<evmc_status_code>(params->status),
                .gas_left = params->gas,
                .gas_refund = 0,
                .output_data = buffer.release(),
                .output_size = params->data.size(),
                .release = [](const struct evmc_result* result) { delete[] result->output_data; },
                .create_address = {},
                .padding = {}}});
    }
    return {};
}

}  // namespace bcos::transaction_executor