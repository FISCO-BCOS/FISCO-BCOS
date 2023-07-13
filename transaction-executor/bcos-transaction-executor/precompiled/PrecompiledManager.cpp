#include "PrecompiledManager.h"
#include "bcos-utilities/Overloaded.h"

evmc_result bcos::transaction_executor::Precompiled::call(evmc_message const& message) const
{
    auto result = std::visit(
        bcos::overloaded{
            [&](executor::PrecompiledContract const& contract) {
                evmc_result result;

                auto [success, output] = contract.execute({message.input_data, message.input_size});
                auto gas = contract.cost({message.input_data, message.input_size});

                result.status_code =
                    (evmc_status_code)(int32_t)(success ?
                                                    protocol::TransactionStatus::None :
                                                    protocol::TransactionStatus::RevertInstruction);
                result.gas_left = message.gas - gas.template convert_to<int64_t>();
                result.gas_refund = 0;
                result.output_data = output.data();
                result.output_size = output.size();
                result.release = nullptr;

                return result;
            },
            [](std::shared_ptr<precompiled::Precompiled> const&) { return evmc_result{}; }},
        *this);

    return result;
}

bcos::transaction_executor::PrecompiledManager::PrecompiledManager()
{
    m_evmPrecompiled.emplace_back(1, executor::PrecompiledContract(3000, 0,
                                         executor::PrecompiledRegistrar::executor("ecrecover")));
    m_evmPrecompiled.emplace_back(2,
        executor::PrecompiledContract(60, 12, executor::PrecompiledRegistrar::executor("sha256")));
    m_evmPrecompiled.emplace_back(3, executor::PrecompiledContract(600, 120,
                                         executor::PrecompiledRegistrar::executor("ripemd160")));
    m_evmPrecompiled.emplace_back(4,
        executor::PrecompiledContract(15, 3, executor::PrecompiledRegistrar::executor("identity")));
    m_evmPrecompiled.emplace_back(
        5, executor::PrecompiledContract(executor::PrecompiledRegistrar::pricer("modexp"),
               executor::PrecompiledRegistrar::executor("modexp")));
    m_evmPrecompiled.emplace_back(
        6, executor::PrecompiledContract(
               150, 0, executor::PrecompiledRegistrar::executor("alt_bn128_G1_add")));
    m_evmPrecompiled.emplace_back(
        7, executor::PrecompiledContract(
               6000, 0, executor::PrecompiledRegistrar::executor("alt_bn128_G1_mul")));
    m_evmPrecompiled.emplace_back(
        8, executor::PrecompiledContract(
               executor::PrecompiledRegistrar::pricer("alt_bn128_pairing_product"),
               executor::PrecompiledRegistrar::executor("alt_bn128_pairing_product")));
    m_evmPrecompiled.emplace_back(9,
        executor::PrecompiledContract(executor::PrecompiledRegistrar::pricer("blake2_compression"),
            executor::PrecompiledRegistrar::executor("blake2_compression")));

    std::sort(m_evmPrecompiled.begin(), m_evmPrecompiled.end(),
        [](const auto& lhs, const auto& rhs) { return std::get<0>(lhs) < std::get<0>(rhs); });
}

bcos::transaction_executor::Precompiled const*
bcos::transaction_executor::PrecompiledManager::getPrecompiled(unsigned long contractAddress) const
{
    auto it = std::lower_bound(m_evmPrecompiled.begin(), m_evmPrecompiled.end(), contractAddress,
        [](const auto& lhs, const auto& rhs) { return std::get<0>(lhs) < rhs; });
    if (it != m_evmPrecompiled.end() && std::get<0>(*it) == contractAddress)
    {
        return std::addressof(std::get<1>(*it));
    }

    return nullptr;
}
