#pragma once

#include "bcos-executor/src/vm/Precompiled.h"
#include "bcos-utilities/FixedBytes.h"

namespace bcos::transaction_executor
{

class Precompiled
  : public std::variant<executor::PrecompiledContract, std::shared_ptr<precompiled::Precompiled>>
{
public:
    using std::variant<executor::PrecompiledContract,
        std::shared_ptr<precompiled::Precompiled>>::variant;

    evmc_result call(evmc_message const& message) const;
};

class PrecompiledManager
{
public:
    PrecompiledManager();
    Precompiled const* getPrecompiled(unsigned long contractAddress) const;

private:
    std::vector<std::tuple<unsigned long, Precompiled>> m_evmPrecompiled;
};

}  // namespace bcos::transaction_executor