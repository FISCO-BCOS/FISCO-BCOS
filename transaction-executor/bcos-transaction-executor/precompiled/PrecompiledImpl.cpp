#include "PrecompiledImpl.h"

bcos::executor_v1::Precompiled::Precompiled(decltype(m_precompiled) precompiled)
  : m_precompiled(std::move(precompiled))
{}

bcos::executor_v1::Precompiled::Precompiled(
    decltype(m_precompiled) precompiled, ledger::Features::Flag flag)
  : m_precompiled(std::move(precompiled)), m_flag(flag)
{}

bcos::executor_v1::Precompiled::Precompiled(
    decltype(m_precompiled) precompiled, size_t size)
  : m_precompiled(std::move(precompiled)), m_size(size)
{}

size_t bcos::executor_v1::size(Precompiled const& precompiled)
{
    return precompiled.m_size;
}

std::optional<bcos::ledger::Features::Flag> bcos::executor_v1::featureFlag(
    Precompiled const& precompiled)
{
    return precompiled.m_flag;
}
