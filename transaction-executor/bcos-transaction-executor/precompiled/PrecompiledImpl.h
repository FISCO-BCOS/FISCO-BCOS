#pragma once
#include "../EVMCResult.h"
#include "ExecutiveWrapper.h"
#include "bcos-executor/src/Common.h"
#include "bcos-executor/src/executive/BlockContext.h"
#include "bcos-executor/src/executive/TransactionExecutive.h"
#include "bcos-executor/src/vm/Precompiled.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-protocol/TransactionStatus.h"
#include "bcos-utilities/Overloaded.h"
#include <evmc/evmc.h>
#include <boost/exception/diagnostic_information.hpp>
#include <exception>
#include <limits>
#include <memory>
#include <range/v3/algorithm/copy.hpp>
#include <variant>

namespace bcos::executor_v1
{

#define PRECOMPILE_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("PRECOMPILE")

struct Precompiled
{
    std::variant<executor::PrecompiledContract, std::shared_ptr<precompiled::Precompiled>>
        m_precompiled;
    std::optional<ledger::Features::Flag> m_flag;
    size_t m_size{1};

    explicit Precompiled(decltype(m_precompiled) precompiled);
    Precompiled(decltype(m_precompiled) precompiled, ledger::Features::Flag flag);
    Precompiled(decltype(m_precompiled) precompiled, size_t size);
};

size_t size(Precompiled const& precompiled);
std::optional<ledger::Features::Flag> featureFlag(Precompiled const& precompiled);

// Build an EVMCResult for a built-in precompiled call, taking ownership of the output buffer.
inline EVMCResult buildBuiltinPrecompiledResult(bool success, auto const& output, int64_t gasLeft)
{
    auto buffer = std::make_unique_for_overwrite<uint8_t[]>(output.size());
    ::ranges::copy(output, buffer.get());
    return EVMCResult{
        evmc_result{
            .status_code = success ? EVMC_SUCCESS : EVMC_REVERT,
            .gas_left = gasLeft,
            .gas_refund = 0,
            .output_data = buffer.release(),
            .output_size = output.size(),
            .release = [](const struct evmc_result* result) { delete[] result->output_data; },
            .create_address = {},
            .padding = {},
        },
        success ? protocol::TransactionStatus::None :
                  protocol::TransactionStatus::RevertInstruction};
}

// Execute an EVM built-in precompiled contract (sha256, ecrecover, etc.).
inline EVMCResult callBuiltinPrecompiled(executor::PrecompiledContract const& precompiledContract,
    evmc_message const& message, ledger::Features const& features)
{
    if (features.get(ledger::Features::Flag::bugfix_v1_precompiled_error_gas))
    {
        // FIB-76: validate cost BEFORE execute to guard against overflow / insufficient gas.
        const auto gas = precompiledContract.cost({message.input_data, message.input_size});
        if (gas > std::numeric_limits<int64_t>::max() || gas < 0)
        {
            return makeErrorEVMCResult(*executor::GlobalHashImpl::g_hashImpl,
                protocol::TransactionStatus::OutOfGas, EVMC_OUT_OF_GAS, 0,
                "Precompiled contract gas cost overflow",
                features.get(ledger::Features::Flag::bugfix_clamp_gas_left_on_error));
        }
        const auto gasCost = gas.template convert_to<int64_t>();
        if (gasCost > message.gas)
        {
            return makeErrorEVMCResult(*executor::GlobalHashImpl::g_hashImpl,
                protocol::TransactionStatus::OutOfGas, EVMC_OUT_OF_GAS, 0,
                "Precompiled contract out of gas",
                features.get(ledger::Features::Flag::bugfix_clamp_gas_left_on_error));
        }
        auto [success, output] =
            precompiledContract.execute({message.input_data, message.input_size});
        return buildBuiltinPrecompiledResult(success, output, message.gas - gasCost);
    }

    // Legacy path: execute first, then compute cost (no validation).
    auto [success, output] = precompiledContract.execute({message.input_data, message.input_size});
    const auto gas = precompiledContract.cost({message.input_data, message.input_size});
    return buildBuiltinPrecompiledResult(
        success, output, message.gas - gas.template convert_to<int64_t>());
}

// Execute a bcos precompiled contract (BFS, table ops, auth, etc.).
inline EVMCResult callBcosPrecompiled(
    std::shared_ptr<precompiled::Precompiled> const& precompiledContract, auto& storage,
    protocol::BlockHeader const& blockHeader, evmc_message const& message,
    evmc_address const& origin, ExternalCaller auto&& externalCaller,
    auto const& precompiledManager, int64_t contextID, int64_t seq, bool authCheck,
    ledger::Features const& features)
{
    using namespace std::string_literals;
    auto contractAddress = address2HexString(message.code_address);
    auto executive = buildLegacyExecutive(storage, blockHeader, contractAddress,
        std::forward<decltype(externalCaller)>(externalCaller), precompiledManager, contextID, seq,
        authCheck);

    auto params = std::make_shared<precompiled::PrecompiledExecResult>();
    params->m_sender = address2HexString(message.sender);
    params->m_codeAddress = std::move(contractAddress);
    params->m_precompiledAddress = address2HexString(message.recipient);
    params->m_origin = address2HexString(origin);
    params->m_input = {message.input_data, message.input_size};
    params->m_gasLeft = message.gas;
    params->m_staticCall = (message.flags & EVMC_STATIC) != 0;
    params->m_create = (message.kind == EVMC_CREATE);

    // FIB-80: use remaining gas on revert (EVM-spec). Clamp to [0, message.gas] to defend
    // against buggy precompiled implementations.
    auto errorGas = [&] {
        return features.get(ledger::Features::Flag::bugfix_v1_precompiled_error_gas) ?
                   std::clamp(params->m_gasLeft, static_cast<int64_t>(0), message.gas) :
                   message.gas;
    };

    try
    {
        auto response = precompiledContract->call(executive, params);

        auto buffer = std::make_unique<uint8_t[]>(params->m_execResult.size());
        std::uninitialized_copy(
            params->m_execResult.begin(), params->m_execResult.end(), buffer.get());
        return EVMCResult{
            evmc_result{
                .status_code = EVMC_SUCCESS,
                .gas_left = response->m_gasLeft,
                .gas_refund = 0,
                .output_data = buffer.release(),
                .output_size = params->m_execResult.size(),
                .release = [](const struct evmc_result* result) { delete[] result->output_data; },
                .create_address = {},
                .padding = {},
            },
            protocol::TransactionStatus::None};
    }
    catch (protocol::PrecompiledError const& e)
    {
        PRECOMPILE_LOG(WARNING) << "Revert transaction: PrecompiledFailed"
                                << LOG_KV("address", params->m_codeAddress)
                                << LOG_KV("message", e.what());
        return makeErrorEVMCResult(*executor::GlobalHashImpl::g_hashImpl,
            protocol::TransactionStatus::PrecompiledError, EVMC_REVERT, errorGas(), e.what(),
            features.get(ledger::Features::Flag::bugfix_clamp_gas_left_on_error));
    }
    catch (std::exception& e)
    {
        PRECOMPILE_LOG(WARNING) << "Precompiled execute error: "
                                << boost::diagnostic_information(e);
        return makeErrorEVMCResult(*executor::GlobalHashImpl::g_hashImpl,
            protocol::TransactionStatus::PrecompiledError, EVMC_REVERT, errorGas(),
            "InternalPrecompiledFailed"s,
            features.get(ledger::Features::Flag::bugfix_clamp_gas_left_on_error));
    }
}

inline constexpr struct
{
    // FIB-79: removed noexcept so the outer catch can return a receipt instead of std::terminate.
    // `features` carries all hard-fork bugfix flags so future additions don't require new params.
    EVMCResult operator()(Precompiled const& precompiled, auto& storage,
        protocol::BlockHeader const& blockHeader, evmc_message const& message,
        evmc_address const& origin, ExternalCaller auto&& externalCaller,
        auto const& precompiledManager, int64_t contextID, int64_t seq, bool authCheck,
        ledger::Features const& features) const
    {
        const bool bugfixPrecompiled =
            features.get(ledger::Features::Flag::bugfix_v1_precompiled_error_gas);

        try
        {
            return std::visit(
                bcos::overloaded{[&](executor::PrecompiledContract const& contract) {
                                     return callBuiltinPrecompiled(contract, message, features);
                                 },
                    [&](std::shared_ptr<precompiled::Precompiled> const& contract) {
                        return callBcosPrecompiled(contract, storage, blockHeader, message, origin,
                            std::forward<decltype(externalCaller)>(externalCaller),
                            precompiledManager, contextID, seq, authCheck, features);
                    }},
                precompiled.m_precompiled);
        }
        catch (std::exception& e)
        {
            PRECOMPILE_LOG(ERROR) << "Unexpected precompiled exception: " << e.what();
            // FIB-79: preserve old std::terminate behavior when flag off; return receipt when on.
            if (!bugfixPrecompiled)
            {
                throw;
            }
            return makeErrorEVMCResult(*executor::GlobalHashImpl::g_hashImpl,
                protocol::TransactionStatus::PrecompiledError, EVMC_INTERNAL_ERROR, 0,
                "InternalPrecompiledError",
                features.get(ledger::Features::Flag::bugfix_clamp_gas_left_on_error));
        }
    }
} callPrecompiled{};

}  // namespace bcos::executor_v1
