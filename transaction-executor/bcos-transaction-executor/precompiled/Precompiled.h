#pragma once
#include "../Common.h"
#include "bcos-executor/src/vm/Precompiled.h"

namespace bcos::transaction_executor
{

class Precompiled
{
private:
    std::variant<executor::PrecompiledContract, std::shared_ptr<precompiled::Precompiled>>
        m_precompiled;

public:
    Precompiled(decltype(m_precompiled) precompiled) : m_precompiled(std::move(precompiled)) {}
    EVMCResult call(evmc_message const& message) const;
};

}  // namespace bcos::transaction_executor