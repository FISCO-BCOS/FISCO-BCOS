#pragma once

#include "ExecutiveWrapper.h"
#include "bcos-executor/src/CallParameters.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-protocol/TransactionStatus.h"
#include "bcos-transaction-executor/EVMCResult.h"
#include <evmc/evmc.h>
#include <boost/throw_exception.hpp>
#include <memory>
#include <utility>

namespace bcos::executor_v1
{

inline task::Task<void> createAuthTable(auto& storage, protocol::BlockHeader const& blockHeader,
    evmc_message const& message, evmc_address const& origin, std::string_view tableName,
    ExternalCaller auto&& externalCaller, auto& precompiledManager, int64_t contextID, int64_t seq,
    const ledger::LedgerConfig& ledgerConfig)
{
    auto contractAddress = address2HexString(message.code_address);
    auto originAddress = address2HexString(origin);
    auto senderAddress = address2HexString(message.sender);
    auto executive = buildLegacyExecutive(storage, blockHeader, contractAddress,
        std::forward<decltype(externalCaller)>(externalCaller), precompiledManager, contextID, seq,
        true);

    executive->creatAuthTable(tableName, originAddress, senderAddress, blockHeader.version());
    co_return;
}

inline std::optional<EVMCResult> checkAuth(auto& storage, protocol::BlockHeader const& blockHeader,
    evmc_message const& message, evmc_address const& origin, ExternalCaller auto&& externalCaller,
    auto& precompiledManager, int64_t contextID, int64_t seq, crypto::Hash const& hashImpl,
    const ledger::Features& features)
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
    // FIB-77: include EVMC_CREATE2 in deploy authorization check
    if (features.get(ledger::Features::Flag::bugfix_auth_check_create2))
    {
        params->create = (message.kind == EVMC_CREATE || message.kind == EVMC_CREATE2);
    }
    else
    {
        params->create = (message.kind == EVMC_CREATE);
    }
    auto result = executive->checkAuth(params);

    if (!result)
    {
        // FIB-81: return ABI-encoded error message instead of full transaction input
        bcos::bytes errorOutput;
        if (features.get(ledger::Features::Flag::bugfix_auth_check_revert_status))
        {
            errorOutput = writeErrInfoToOutput(
                hashImpl, params->message.empty() ? "Authorization check failed" : params->message);
        }
        else
        {
            errorOutput.assign(params->data.begin(), params->data.end());
        }
        auto buffer = std::make_unique<uint8_t[]>(errorOutput.size());
        std::uninitialized_copy(errorOutput.begin(), errorOutput.end(), buffer.get());

        return std::make_optional(EVMCResult{
            evmc_result{.status_code = static_cast<evmc_status_code>(params->evmStatus),
                .gas_left = params->gas,
                .gas_refund = 0,
                .output_data = buffer.release(),
                .output_size = errorOutput.size(),
                .release = [](const struct evmc_result* result) { delete[] result->output_data; },
                .create_address = {},
                .padding = {}},
            static_cast<protocol::TransactionStatus>(params->status)});
    }
    return {};
}

}  // namespace bcos::executor_v1
