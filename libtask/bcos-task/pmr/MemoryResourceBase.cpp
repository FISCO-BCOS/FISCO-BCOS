#include "MemoryResourceBase.h"

void* bcos::task::pmr::MemoryResourceBase::operator new(size_t size)
{
    return operator new(size, std::allocator_arg, std::pmr::polymorphic_allocator<>{});
}

void bcos::task::pmr::MemoryResourceBase::operator delete(void* ptr, size_t size) noexcept
{
    auto* pptr = static_cast<std::pmr::memory_resource**>(ptr) - 1;
    assert(*pptr);
    if (auto* allocator = *pptr)
    {
        allocator->deallocate(static_cast<void*>(pptr), size + sizeof(std::pmr::memory_resource*),
            alignof(std::pmr::memory_resource*));
    }
}