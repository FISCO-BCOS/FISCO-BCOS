#include "MemoryResourceBase.h"
#include <span>

std::pmr::memory_resource*& bcos::task::pmr::MemoryResourceBase::getAllocator(
    void* ptr, size_t size)
{
    std::span view(static_cast<std::pmr::memory_resource**>(ptr),
        std::max(size, sizeof(std::pmr::memory_resource*)) / sizeof(std::pmr::memory_resource*) +
            1);
    return view.back();
}

void* bcos::task::pmr::MemoryResourceBase::operator new(size_t size)
{
    return operator new(size, std::allocator_arg, std::pmr::polymorphic_allocator<>{});
}

void bcos::task::pmr::MemoryResourceBase::operator delete(void* ptr, size_t size) noexcept
{
    if (auto* allocator = getAllocator(ptr, size))
    {
        allocator->deallocate(
            ptr, size + sizeof(std::pmr::memory_resource*), alignof(std::pmr::memory_resource*));
    }
}
