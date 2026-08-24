#include "MemPoolInitializer.h"

bcos::initializer::MemPoolInitializer::Ptr bcos::initializer::MemPoolInitializer::build()
{
    return std::make_shared<MemPoolInitializer>();
}