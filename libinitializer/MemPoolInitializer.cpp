#include "MemPoolInitializer.h"

bcos::initializer::MemPoolInitializer::Ptr bcos::initializer::MemPoolInitializer::build(
    bcos::txpool::MemPoolConfig config)
{
    return std::make_shared<MemPoolInitializer>(config);
}
