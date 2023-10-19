#pragma once
#include "../Common.h"
#include "ExecutiveWrapper.h"
#include "StorageWrapper.h"
#include "bcos-executor/src/executive/BlockContext.h"
#include "bcos-executor/src/executive/TransactionExecutive.h"
#include "bcos-executor/src/vm/Precompiled.h"
#include "bcos-table/src/StateStorage.h"
#include "bcos-table/src/StateStorageInterface.h"
#include "bcos-utilities/Overloaded.h"
#include <type_traits>

#ifdef WITH_WASM
#include "bcos-executor/src/vm/gas_meter/GasInjector.h"
#else
class bcos::wasm::GasInjector
{
};
#endif

namespace bcos::transaction_executor
{

class Precompiled
{
private:
    std::variant<executor::PrecompiledContract, std::shared_ptr<precompiled::Precompiled>>
        m_precompiled;

public:
    Precompiled(decltype(m_precompiled) precompiled) : m_precompiled(std::move(precompiled)) {}
    EVMCResult call(auto& storage, protocol::BlockHeader const& blockHeader,
        evmc_message const& message, evmc_address const& origin, auto externalCaller) const
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
                            (evmc_status_code)(int32_t)(success ?
                                                            protocol::TransactionStatus::None :
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
                    auto storageWrapper =
                        std::make_shared<StateStorageWrapper<std::decay_t<decltype(storage)>>>(
                            storage);

                    executor::BlockContext blockContext(storageWrapper, nullptr,
                        GlobalHashImpl::g_hashImpl, blockHeader.number(), blockHeader.hash(),
                        blockHeader.timestamp(), blockHeader.version(),
                        bcos::executor::VMSchedule{}, false, false);
                    wasm::GasInjector gasInjector;

                    auto contractAddress = evmAddress2String(message.code_address);
                    auto executive =
                        std::make_shared<ExecutiveWrapper<decltype(externalCaller)>>(blockContext,
                            contractAddress, 0, 0, gasInjector, std::move(externalCaller));

                    auto params = std::make_shared<precompiled::PrecompiledExecResult>();
                    params->m_sender = evmAddress2String(message.sender);
                    params->m_codeAddress = std::move(contractAddress);
                    params->m_precompiledAddress = evmAddress2String(message.recipient);
                    params->m_origin = evmAddress2String(origin);
                    params->m_input = {message.input_data, message.input_size};
                    params->m_gasLeft = message.gas;
                    params->m_staticCall = (message.kind == EVMC_CALL);
                    params->m_create = (message.kind == EVMC_CREATE);

                    auto response = precompiled->call(executive, params);

                    auto buffer =
                        std::unique_ptr<uint8_t>(new uint8_t[params->m_execResult.size()]);
                    std::copy(
                        params->m_execResult.begin(), params->m_execResult.end(), buffer.get());
                    EVMCResult result{evmc_result{
                        .status_code = (evmc_status_code)(int32_t)protocol::TransactionStatus::None,
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
            m_precompiled);
    }
};

}  // namespace bcos::transaction_executor