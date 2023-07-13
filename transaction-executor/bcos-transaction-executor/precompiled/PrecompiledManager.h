#pragma once

#include "bcos-executor/src/vm/Precompiled.h"
#include "bcos-utilities/FixedBytes.h"

namespace bcos::transaction_executor
{

class PrecompiledManager
{
public:
    using Precompiled = std::variant<executor::PrecompiledContract>;
    PrecompiledManager();
    Precompiled const* getPrecompiled(unsigned long contractAddress) const;

private:
    std::vector<std::tuple<unsigned long, Precompiled>> m_evmPrecompiled;
};
const inline PrecompiledManager g_precompiledManager{};

}  // namespace bcos::transaction_executor