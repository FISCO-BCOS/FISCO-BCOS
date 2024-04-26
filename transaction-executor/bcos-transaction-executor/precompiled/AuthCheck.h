#pragma once

#include "ExecutiveWrapper.h"
#include "bcos-executor/src/CallParameters.h"
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

inline std::tuple<bool, std::unique_ptr<executor::CallParameters>> checkAuth(auto& storage,
    protocol::BlockHeader const& blockHeader, evmc_message const& message,
    evmc_address const& origin, ExternalCaller auto&& externalCaller, auto& precompiledManager,
    int64_t contextID, int64_t seq, bool authCheck)
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
    params->staticCall = (message.kind == EVMC_CALL);
    params->create = (message.kind == EVMC_CREATE);

    auto result = executive->checkAuth(params);
    return {result, std::move(params)};
}

}  // namespace bcos::transaction_executor