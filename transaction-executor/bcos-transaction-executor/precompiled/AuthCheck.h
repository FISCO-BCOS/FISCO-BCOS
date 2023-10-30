#pragma once

#include "ExecutiveWrapper.h"
#include <evmc/evmc.h>
#include <boost/throw_exception.hpp>
#include <memory>
#include <utility>

namespace bcos::transaction_executor
{

inline std::tuple<bool, std::unique_ptr<executor::CallParameters>> checkAuth(auto& storage,
    protocol::BlockHeader const& blockHeader, evmc_message const& message,
    evmc_address const& origin, ExternalCaller auto externalCaller)
{
    auto contractAddress = address2HexString(message.code_address);
    auto executive =
        buildLegacyExecutive(storage, blockHeader, contractAddress, std::move(externalCaller));

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