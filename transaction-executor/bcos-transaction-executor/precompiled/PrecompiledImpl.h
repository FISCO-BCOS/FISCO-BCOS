#pragma once
#include "../EVMCResult.h"
#include "ExecutiveWrapper.h"
#include "bcos-executor/src/Common.h"
#include "bcos-executor/src/executive/BlockContext.h"
#include "bcos-executor/src/executive/TransactionExecutive.h"
#include "bcos-executor/src/vm/Precompiled.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-table/src/LegacyStorageWrapper.h"
#include "bcos-table/src/StateStorage.h"
#include "bcos-utilities/Overloaded.h"
#include <evmc/evmc.h>
#include <boost/throw_exception.hpp>
#include <memory>
#include <type_traits>
#include <typeinfo>
#include <variant>

#ifdef WITH_WASM
#include "bcos-executor/src/vm/gas_meter/GasInjector.h"
#else
class bcos::wasm::GasInjector
{
};
#endif

namespace bcos::transaction_executor
{

inline auto buildLegacyExecutive(auto& storage, protocol::BlockHeader const& blockHeader,
    std::string contractAddress, ExternalCaller auto externalCaller, auto const& precompiledManager,
    int64_t contextID, int64_t seq)
{
    auto storageWrapper =
        std::make_shared<storage::LegacyStateStorageWrapper<std::decay_t<decltype(storage)>>>(
            storage);

    auto blockContext = std::make_unique<executor::BlockContext>(storageWrapper, nullptr,
        executor::GlobalHashImpl::g_hashImpl, blockHeader.number(), blockHeader.hash(),
        blockHeader.timestamp(), blockHeader.version(), bcos::executor::VMSchedule{}, false, false);
    return std::make_shared<
        ExecutiveWrapper<decltype(externalCaller), std::decay_t<decltype(precompiledManager)>>>(
        std::move(blockContext), std::move(contractAddress), contextID, seq, wasm::GasInjector{},
        std::move(externalCaller), precompiledManager);
}

struct Precompiled
{
    std::variant<executor::PrecompiledContract, std::shared_ptr<precompiled::Precompiled>>
        m_precompiled;
    std::optional<ledger::Features::Flag> m_flag;

    explicit Precompiled(auto precompiled) : m_precompiled(std::move(precompiled)) {}
    explicit Precompiled(auto precompiled, ledger::Features::Flag flag)
      : m_precompiled(std::move(precompiled)), m_flag(flag)
    {}
};

inline std::optional<ledger::Features::Flag> requiredFlag(Precompiled const& precompiled)
{
    return precompiled.m_flag;
}

inline EVMCResult callPrecompiled(Precompiled const& precompiled, auto& storage,
    protocol::BlockHeader const& blockHeader, evmc_message const& message,
    evmc_address const& origin, ExternalCaller auto&& externalCaller,
    auto const& precompiledManager, int64_t contextID, int64_t seq)
{
    return std::visit(
        bcos::overloaded{
            [&](executor::PrecompiledContract const& precompiled) {
                auto [success, output] =
                    precompiled.execute({message.input_data, message.input_size});
                auto gas = precompiled.cost({message.input_data, message.input_size});

                auto buffer = std::unique_ptr<uint8_t>(new uint8_t[output.size()]);
                std::copy(output.begin(), output.end(), buffer.get());
                EVMCResult result{evmc_result{
                    .status_code =
                        (evmc_status_code)(int32_t)(success ? protocol::TransactionStatus::None :
                                                              protocol::TransactionStatus::
                                                                  RevertInstruction),
                    .gas_left = message.gas - gas.template convert_to<int64_t>(),
                    .gas_refund = 0,
                    .output_data = buffer.release(),
                    .output_size = output.size(),
                    .release =
                        [](const struct evmc_result* result) { delete[] result->output_data; },
                    .create_address = {},
                    .padding = {},
                }};

                return result;
            },
            [&](std::shared_ptr<precompiled::Precompiled> const& precompiled) {
                auto contractAddress = address2HexString(message.code_address);
                auto executive = buildLegacyExecutive(storage, blockHeader, contractAddress,
                    std::forward<decltype(externalCaller)>(externalCaller), precompiledManager,
                    contextID, seq);

                auto params = std::make_shared<precompiled::PrecompiledExecResult>();
                params->m_sender = address2HexString(message.sender);
                params->m_codeAddress = std::move(contractAddress);
                params->m_precompiledAddress = address2HexString(message.recipient);
                params->m_origin = address2HexString(origin);
                params->m_input = {message.input_data, message.input_size};
                params->m_gasLeft = message.gas;
                params->m_staticCall = (message.kind == EVMC_CALL);
                params->m_create = (message.kind == EVMC_CREATE);

                auto response = precompiled->call(executive, params);

                auto buffer = std::unique_ptr<uint8_t>(new uint8_t[params->m_execResult.size()]);
                std::copy(params->m_execResult.begin(), params->m_execResult.end(), buffer.get());
                EVMCResult result{evmc_result{
                    .status_code = (evmc_status_code)protocol::TransactionStatus::None,
                    .gas_left = response->m_gasLeft,
                    .gas_refund = 0,
                    .output_data = buffer.release(),
                    .output_size = params->m_execResult.size(),
                    .release =
                        [](const struct evmc_result* result) { delete[] result->output_data; },
                    .create_address = {},
                    .padding = {},
                }};

                return result;
            }},
        precompiled.m_precompiled);
}


}  // namespace bcos::transaction_executor