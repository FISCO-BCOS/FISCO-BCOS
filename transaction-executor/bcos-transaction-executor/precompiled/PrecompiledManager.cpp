#include "PrecompiledManager.h"
#include <memory>

bcos::transaction_executor::PrecompiledManager::PrecompiledManager()
{
    m_address2Precompiled.emplace_back(
        1, executor::PrecompiledContract(
               3000, 0, executor::PrecompiledRegistrar::executor("ecrecover")));
    m_address2Precompiled.emplace_back(2,
        executor::PrecompiledContract(60, 12, executor::PrecompiledRegistrar::executor("sha256")));
    m_address2Precompiled.emplace_back(
        3, executor::PrecompiledContract(
               600, 120, executor::PrecompiledRegistrar::executor("ripemd160")));
    m_address2Precompiled.emplace_back(4,
        executor::PrecompiledContract(15, 3, executor::PrecompiledRegistrar::executor("identity")));
    m_address2Precompiled.emplace_back(
        5, executor::PrecompiledContract(executor::PrecompiledRegistrar::pricer("modexp"),
               executor::PrecompiledRegistrar::executor("modexp")));
    m_address2Precompiled.emplace_back(
        6, executor::PrecompiledContract(
               150, 0, executor::PrecompiledRegistrar::executor("alt_bn128_G1_add")));
    m_address2Precompiled.emplace_back(
        7, executor::PrecompiledContract(
               6000, 0, executor::PrecompiledRegistrar::executor("alt_bn128_G1_mul")));
    m_address2Precompiled.emplace_back(
        8, executor::PrecompiledContract(
               executor::PrecompiledRegistrar::pricer("alt_bn128_pairing_product"),
               executor::PrecompiledRegistrar::executor("alt_bn128_pairing_product")));
    m_address2Precompiled.emplace_back(9,
        executor::PrecompiledContract(executor::PrecompiledRegistrar::pricer("blake2_compression"),
            executor::PrecompiledRegistrar::executor("blake2_compression")));

    std::sort(m_address2Precompiled.begin(), m_address2Precompiled.end(),
        [](const auto& lhs, const auto& rhs) { return std::get<0>(lhs) < std::get<0>(rhs); });
}

bcos::transaction_executor::Precompiled const*
bcos::transaction_executor::PrecompiledManager::getPrecompiled(unsigned long contractAddress) const
{
    auto it = std::lower_bound(m_address2Precompiled.begin(), m_address2Precompiled.end(),
        contractAddress,
        [](const decltype(m_address2Precompiled)::value_type& lhs, unsigned long rhs) {
            return std::get<0>(lhs) < rhs;
        });
    if (it != m_address2Precompiled.end() && std::get<0>(*it) == contractAddress)
    {
        return std::addressof(std::get<1>(*it));
    }

    return nullptr;
}
