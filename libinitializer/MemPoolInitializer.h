#pragma once

#include "bcos-mempool/MemPoolImpl.h"
#include <memory>

namespace bcos::initializer
{
class MemPoolInitializer
{
public:
    using Ptr = std::shared_ptr<MemPoolInitializer>;

    explicit MemPoolInitializer(bcos::txpool::MemPoolConfig config = {}) : m_memPool(config) {}

    static Ptr build(bcos::txpool::MemPoolConfig config = {});

    bcos::txpool::MemPoolImpl& memPool() { return m_memPool; }
    bcos::txpool::MemPoolImpl const& memPool() const { return m_memPool; }

private:
    bcos::txpool::MemPoolImpl m_memPool;
};
}  // namespace bcos::initializer