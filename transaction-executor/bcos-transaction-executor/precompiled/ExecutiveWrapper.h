#pragma once
#include "../Common.h"
#include "bcos-codec/wrapper/CodecWrapper.h"
#include "bcos-executor/src/executive/TransactionExecutive.h"
#include <evmc/evmc.h>
#include <memory>

namespace bcos::transaction_executor
{

inline std::shared_ptr<precompiled::Precompiled> getInnerPrecompiled(auto const& precompiled)
{
    return std::get<std::shared_ptr<precompiled::Precompiled>>(precompiled);
}

template <class T>
concept ExternalCaller = std::is_invocable_r_v<EVMCResult, T, const evmc_message&>;

template <ExternalCaller Caller, class PrecompiledManager>
class ExecutiveWrapper : public executor::TransactionExecutive
{
private:
    std::unique_ptr<executor::BlockContext> m_blockContext;
    Caller m_externalCaller;
    PrecompiledManager const& m_precompiledManager;

public:
    ExecutiveWrapper(std::unique_ptr<executor::BlockContext> blockContext,
        std::string contractAddress, int64_t contextID, int64_t seq,
        const wasm::GasInjector& gasInjector, Caller externalCaller,
        PrecompiledManager const& precompiledManager)
      : executor::TransactionExecutive(
            *blockContext, std::move(contractAddress), contextID, seq, gasInjector),
        m_blockContext(std::move(blockContext)),
        m_externalCaller(std::move(externalCaller)),
        m_precompiledManager(precompiledManager)
    {}

    std::shared_ptr<precompiled::Precompiled> getPrecompiled(const std::string& _address,
        uint32_t version, bool isAuth, const ledger::Features& features) const override
    {
        auto addressBytes = fromHex(_address);
        auto address = fromBigEndian<u160>(addressBytes);
        const auto* precompiled =
            m_precompiledManager.getPrecompiled(address.convert_to<unsigned long>());
        if (precompiled == nullptr)
        {
            return nullptr;
        }

        return getInnerPrecompiled(*precompiled);
    }

    bool isPrecompiled(const std::string& _address) const override
    {
        auto addressBytes = fromHex(_address);
        auto address = fromBigEndian<u160>(addressBytes);
        return m_precompiledManager.getPrecompiled(address.convert_to<unsigned long>()) != nullptr;
    }

    executor::CallParameters::UniquePtr externalCall(
        executor::CallParameters::UniquePtr input) override
    {
        evmc_message evmcMessage{.kind = input->create ? EVMC_CREATE : EVMC_CALL,
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
            CodecWrapper codec(m_blockContext->hashHandler(), m_blockContext->isWasm());
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