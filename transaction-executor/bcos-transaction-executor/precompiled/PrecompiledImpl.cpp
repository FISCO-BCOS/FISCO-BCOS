#include "PrecompiledImpl.h"

bcos::transaction_executor::Precompiled::Precompiled(decltype(m_precompiled) precompiled)
  : m_precompiled(std::move(precompiled))
{}

bcos::transaction_executor::Precompiled::Precompiled(
    decltype(m_precompiled) precompiled, ledger::Features::Flag flag)
  : m_precompiled(std::move(precompiled)), m_flag(flag)
{}

bcos::transaction_executor::Precompiled::Precompiled(
    decltype(m_precompiled) precompiled, size_t size)
  : m_precompiled(std::move(precompiled)), m_size(size)
{}

size_t bcos::transaction_executor::size(Precompiled const& precompiled)
{
    return precompiled.m_size;
}

std::optional<bcos::ledger::Features::Flag> bcos::transaction_executor::featureFlag(
    Precompiled const& precompiled)
{
    return precompiled.m_flag;
}
