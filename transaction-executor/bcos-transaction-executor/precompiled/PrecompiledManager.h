#pragma once

#include <utility>

#include "../Common.h"
#include "bcos-executor/src/vm/Precompiled.h"
#include "bcos-utilities/FixedBytes.h"

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

class PrecompiledManager
{
public:
    PrecompiledManager();
    Precompiled const* getPrecompiled(unsigned long contractAddress) const;

private:
    std::vector<std::tuple<unsigned long, Precompiled>> m_address2Precompiled;
};

}  // namespace bcos::transaction_executor