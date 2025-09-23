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
#include <boost/throw_exception.hpp>
#include <exception>
#include <memory>
#include <variant>

namespace bcos::transaction_executor
{

#define PRECOMPILE_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("PRECOMPILE")

struct Precompiled
{
    std::variant<executor::PrecompiledContract, std::shared_ptr<precompiled::Precompiled>>
        m_precompiled;
    std::optional<ledger::Features::Flag> m_flag;
    size_t m_size{1};

    explicit Precompiled(auto precompiled) : m_precompiled(std::move(precompiled)) {}
    Precompiled(auto precompiled, ledger::Features::Flag flag)
      : m_precompiled(std::move(precompiled)), m_flag(flag)
    {}
    Precompiled(auto precompiled, size_t size) : m_precompiled(precompiled), m_size(size) {}
};

inline constexpr struct
{
    size_t operator()(Precompiled const& precompiled) const noexcept { return precompiled.m_size; }
} size{};

inline constexpr struct
{
    std::optional<ledger::Features::Flag> operator()(Precompiled const& precompiled) const noexcept
    {
        return precompiled.m_flag;
    }
} featureFlag{};

inline constexpr struct
{
    EVMCResult operator()(Precompiled const& precompiled, auto& storage,
        protocol::BlockHeader const& blockHeader, evmc_message const& message,
        evmc_address const& origin, ExternalCaller auto&& externalCaller,
        auto const& precompiledManager, int64_t contextID, int64_t seq,
        bool authCheck) const noexcept
    {
        return std::visit(
            bcos::overloaded{
                [&](executor::PrecompiledContract const& precompiled) {
                    auto [success, output] =
                        precompiled.execute({message.input_data, message.input_size});
                    auto gas = precompiled.cost({message.input_data, message.input_size});

                    auto buffer = std::make_unique_for_overwrite<uint8_t[]>(output.size());
                    std::copy(output.begin(), output.end(), buffer.get());
                    EVMCResult result{
                        evmc_result{
                            .status_code = success ? EVMC_SUCCESS : EVMC_REVERT,
                            .gas_left = message.gas - gas.template convert_to<int64_t>(),
                            .gas_refund = 0,
                            .output_data = buffer.release(),
                            .output_size = output.size(),
                            .release =
                                [](const struct evmc_result* result) {
                                    delete[] result->output_data;
                                },
                            .create_address = {},
                            .padding = {},
                        },
                        success ? protocol::TransactionStatus::None :
                                  protocol::TransactionStatus::RevertInstruction};

                    return result;
                },
                [&](std::shared_ptr<precompiled::Precompiled> const& precompiled) {
                    using namespace std::string_literals;
                    auto contractAddress = address2HexString(message.code_address);
                    auto executive = buildLegacyExecutive(storage, blockHeader, contractAddress,
                        std::forward<decltype(externalCaller)>(externalCaller), precompiledManager,
                        contextID, seq, authCheck);

                    auto params = std::make_shared<precompiled::PrecompiledExecResult>();
                    params->m_sender = address2HexString(message.sender);
                    params->m_codeAddress = std::move(contractAddress);
                    params->m_precompiledAddress = address2HexString(message.recipient);
                    params->m_origin = address2HexString(origin);
                    params->m_input = {message.input_data, message.input_size};
                    params->m_gasLeft = message.gas;
                    params->m_staticCall = (message.flags & EVMC_STATIC) != 0;
                    params->m_create = (message.kind == EVMC_CREATE);

                    try
                    {
                        auto response = precompiled->call(executive, params);

                        auto buffer = std::make_unique<uint8_t[]>(params->m_execResult.size());
                        std::uninitialized_copy(
                            params->m_execResult.begin(), params->m_execResult.end(), buffer.get());
                        EVMCResult result{evmc_result{
                                              .status_code = EVMC_SUCCESS,
                                              .gas_left = response->m_gasLeft,
                                              .gas_refund = 0,
                                              .output_data = buffer.release(),
                                              .output_size = params->m_execResult.size(),
                                              .release =
                                                  [](const struct evmc_result* result) {
                                                      delete[] result->output_data;
                                                  },
                                              .create_address = {},
                                              .padding = {},
                                          },
                            protocol::TransactionStatus::None};

                        return result;
                    }
                    catch (protocol::PrecompiledError const& e)
                    {
                        PRECOMPILE_LOG(WARNING)
                            << "Revert transaction: PrecompiledFailed"
                            << LOG_KV("address", contractAddress) << LOG_KV("message", e.what());
                        return makeErrorEVMCResult(*executor::GlobalHashImpl::g_hashImpl,
                            protocol::TransactionStatus::PrecompiledError, EVMC_REVERT, message.gas,
                            e.what());
                    }
                    catch (std::exception& e)
                    {
                        PRECOMPILE_LOG(WARNING)
                            << "Precompiled execute error: " << boost::diagnostic_information(e);
                        return makeErrorEVMCResult(*executor::GlobalHashImpl::g_hashImpl,
                            protocol::TransactionStatus::PrecompiledError, EVMC_REVERT, message.gas,
                            "InternalPrecompiledFailed"s);
                    }
                }},
            precompiled.m_precompiled);
    }
} callPrecompiled{};

}  // namespace bcos::transaction_executor
