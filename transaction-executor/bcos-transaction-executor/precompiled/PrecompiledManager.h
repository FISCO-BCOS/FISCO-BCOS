#pragma once

#include "PrecompiledImpl.h"

namespace bcos::transaction_executor
{

class PrecompiledManager
{
public:
    PrecompiledManager(crypto::Hash::Ptr hashImpl);
    Precompiled const* getPrecompiled(unsigned long contractAddress) const;
    Precompiled const* getPrecompiled(const evmc_address& address) const;

private:
    crypto::Hash::Ptr m_hashImpl;
    std::vector<std::tuple<unsigned long, Precompiled>> m_address2Precompiled;
};

}  // namespace bcos::transaction_executor