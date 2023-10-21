#pragma once
#include "../Common.h"
#include "bcos-codec/wrapper/CodecWrapper.h"
#include "bcos-executor/src/executive/TransactionExecutive.h"
#include <memory>

namespace bcos::transaction_executor
{

template <class ExternalCaller>
    requires std::is_invocable_r_v<EVMCResult, ExternalCaller, const evmc_message&>
class ExecutiveWrapper : public executor::TransactionExecutive
{
private:
    ExternalCaller m_externalCaller;

public:
    ExecutiveWrapper(const executor::BlockContext& blockContext, std::string contractAddress,
        int64_t contextID, int64_t seq, const wasm::GasInjector& gasInjector,
        decltype(m_externalCaller) externalCaller)
      : executor::TransactionExecutive(
            blockContext, std::move(contractAddress), contextID, seq, gasInjector),
        m_externalCaller(std::move(externalCaller))
    {}

    executor::CallParameters::UniquePtr externalCall(
        executor::CallParameters::UniquePtr input) override
    {
        evmc_message evmcMessage{.kind = EVMC_CALL,
            .flags = 0,
            .depth = 0,
            .gas = input->gas,
            .recipient = toEvmC(input->receiveAddress),
            .destination_ptr = nullptr,
            .destination_len = 0,
            .sender = unhexAddress(input->senderAddress),
            .sender_ptr = nullptr,
            .sender_len = 0,
            .input_data = input->data.data(),
            .input_size = input->data.size(),
            .value = toEvmC(0x0_cppui256),
            .create2_salt = toEvmC(0x0_cppui256),
            .code_address = unhexAddress(input->codeAddress)};

        std::optional<std::tuple<std::string, bcos::bytes>> internalCallParams;
        if (input->internalCall)
        {
            internalCallParams.emplace();
            auto& [precompiledContract, precompiledInput] = *internalCallParams;
            CodecWrapper codec(m_blockContext.hashHandler(), m_blockContext.isWasm());
            codec.decode(ref(input->data), precompiledContract, precompiledInput);
            evmcMessage.code_address = unhexAddress(precompiledContract);
            evmcMessage.input_data = precompiledInput.data();
            evmcMessage.input_size = precompiledInput.size();
        }

        auto result = m_externalCaller(evmcMessage);

        auto callResult =
            std::make_unique<executor::CallParameters>(executor::CallParameters::FINISHED);
        callResult->evmStatus = result.status_code;
        callResult->status = result.status_code;
        callResult->gas = result.gas_left;
        callResult->data.assign(result.output_data, result.output_data + result.output_size);

        return callResult;
    }
};
}  // namespace bcos::transaction_executor