#pragma once

#include "../Common.h"
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

    EVMCResult call(evmc_message const& message) const;
};

class PrecompiledManager
{
public:
    PrecompiledManager();
    Precompiled const* getPrecompiled(unsigned long contractAddress) const;

private:
    std::vector<std::tuple<unsigned long, Precompiled>> m_address2Precompiled;
};

}  // namespace bcos::transaction_executor