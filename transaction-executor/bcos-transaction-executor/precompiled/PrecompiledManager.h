#pragma once

#include "PrecompiledImpl.h"
#include "bcos-framework/ledger/Features.h"

namespace bcos::executor_v1
{

class PrecompiledManager
{
public:
    explicit PrecompiledManager(crypto::Hash::Ptr hashImpl);
    Precompiled const* getPrecompiled(unsigned long contractAddress) const;
    Precompiled const* getPrecompiled(const evmc_address& address) const;

    // FIB-84: feature-aware lookup - returns nullptr if precompiled's flag is disabled
    Precompiled const* getPrecompiled(
        unsigned long contractAddress, const ledger::Features& features) const;
    Precompiled const* getPrecompiled(
        const evmc_address& address, const ledger::Features& features) const;

private:
    crypto::Hash::Ptr m_hashImpl;
    std::vector<std::tuple<unsigned long, Precompiled>> m_address2Precompiled{};
};

}  // namespace bcos::executor_v1