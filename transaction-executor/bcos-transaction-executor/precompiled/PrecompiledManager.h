#pragma once

#include "Precompiled.h"
#include "bcos-utilities/FixedBytes.h"
#include <utility>

namespace bcos::transaction_executor
{

class PrecompiledManager
{
public:
    PrecompiledManager();
    Precompiled const* getPrecompiled(unsigned long contractAddress) const;

private:
    std::vector<std::tuple<unsigned long, Precompiled>> m_address2Precompiled;
};

}  // namespace bcos::transaction_executor