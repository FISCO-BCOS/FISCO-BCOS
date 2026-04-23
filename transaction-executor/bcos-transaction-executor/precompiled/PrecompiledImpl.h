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

inline constexpr struct
{
    // FIB-79: removed noexcept to prevent std::terminate on exceptions
    EVMCResult operator()(Precompiled const& precompiled, auto& storage,
        protocol::BlockHeader const& blockHeader, evmc_message const& message,
        evmc_address const& origin, ExternalCaller auto&& externalCaller,
        auto const& precompiledManager, int64_t contextID, int64_t seq, bool authCheck) const
    {
        try
        {
            return std::visit(
                bcos::overloaded{// evm built-in precompiled contracts
                    [&](executor::PrecompiledContract const& precompiledContract) {
                        // FIB-76: compute gas cost BEFORE execution and validate
                        const auto gas =
                            precompiledContract.cost({message.input_data, message.input_size});
                        int64_t gasCost = 0;
                        if (gas > std::numeric_limits<int64_t>::max() || gas < 0)
                        {
                            return makeErrorEVMCResult(*executor::GlobalHashImpl::g_hashImpl,
                                protocol::TransactionStatus::OutOfGas, EVMC_OUT_OF_GAS, 0,
                                "Precompiled contract gas cost overflow");
                        }
                        gasCost = gas.template convert_to<int64_t>();
                        if (gasCost > message.gas)
                        {
                            return makeErrorEVMCResult(*executor::GlobalHashImpl::g_hashImpl,
                                protocol::TransactionStatus::OutOfGas, EVMC_OUT_OF_GAS, 0,
                                "Precompiled contract out of gas");
                        }

                        auto [success, output] =
                            precompiledContract.execute({message.input_data, message.input_size});

                        auto buffer = std::make_unique_for_overwrite<uint8_t[]>(output.size());
                        ::ranges::copy(output, buffer.get());
                        EVMCResult result{evmc_result{
                                              .status_code = success ? EVMC_SUCCESS : EVMC_REVERT,
                                              .gas_left = message.gas - gasCost,
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
                    // bcos precompiled contracts
                    [&](std::shared_ptr<precompiled::Precompiled> const& precompiledContract) {
                        using namespace std::string_literals;
                        auto contractAddress = address2HexString(message.code_address);
                        auto executive = buildLegacyExecutive(storage, blockHeader, contractAddress,
                            std::forward<decltype(externalCaller)>(externalCaller),
                            precompiledManager, contextID, seq, authCheck);

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
                            auto response = precompiledContract->call(executive, params);

                            auto buffer = std::make_unique<uint8_t[]>(params->m_execResult.size());
                            std::uninitialized_copy(params->m_execResult.begin(),
                                params->m_execResult.end(), buffer.get());
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
                            PRECOMPILE_LOG(WARNING) << "Revert transaction: PrecompiledFailed"
                                                    << LOG_KV("address", params->m_codeAddress)
                                                    << LOG_KV("message", e.what());
                            // FIB-80: use remaining gas instead of original gas limit
                            return makeErrorEVMCResult(*executor::GlobalHashImpl::g_hashImpl,
                                protocol::TransactionStatus::PrecompiledError, EVMC_REVERT,
                                params->m_gasLeft, e.what());
                        }
                        catch (std::exception& e)
                        {
                            PRECOMPILE_LOG(WARNING) << "Precompiled execute error: "
                                                    << boost::diagnostic_information(e);
                            // FIB-80: use remaining gas instead of original gas limit
                            return makeErrorEVMCResult(*executor::GlobalHashImpl::g_hashImpl,
                                protocol::TransactionStatus::PrecompiledError, EVMC_REVERT,
                                params->m_gasLeft, "InternalPrecompiledFailed"s);
                        }
                    }},
                precompiled.m_precompiled);
        }
        // FIB-79: outer catch to prevent std::terminate from uncaught exceptions
        catch (std::exception& e)
        {
            PRECOMPILE_LOG(ERROR) << "Unexpected precompiled exception: " << e.what();
            return makeErrorEVMCResult(*executor::GlobalHashImpl::g_hashImpl,
                protocol::TransactionStatus::PrecompiledError, EVMC_INTERNAL_ERROR, 0,
                "InternalPrecompiledError");
        }
    }
} callPrecompiled{};

}  // namespace bcos::executor_v1
