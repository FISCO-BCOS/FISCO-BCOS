#pragma once

#include "bcos-mempool/MemPoolImpl.h"
#include <memory>

namespace bcos::initializer
{
class MemPoolInitializer
{
public:
    using Ptr = std::shared_ptr<MemPoolInitializer>;

    MemPoolInitializer() = default;

    static Ptr build();

    bcos::txpool::MemPoolImpl& memPool() { return m_memPool; }
    bcos::txpool::MemPoolImpl const& memPool() const { return m_memPool; }

private:
    bcos::txpool::MemPoolImpl m_memPool;
};
}  // namespace bcos::initializer