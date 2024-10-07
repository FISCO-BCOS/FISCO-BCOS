#pragma once

#include <cassert>
#include <cstddef>
#include <memory_resource>

namespace bcos::task
{
struct MemoryResourceBase
{
    static void* operator new(size_t size, std::allocator_arg_t /*unused*/, const auto& allocator,
        const auto&... /*unused*/)
    {
        auto* memoryResource = allocator.resource();
        auto* resources = static_cast<std::pmr::memory_resource**>(memoryResource->allocate(
            size + sizeof(std::pmr::memory_resource*), alignof(std::pmr::memory_resource*)));
        assert(resources);

        *resources = allocator.resource();
        return resources + 1;
    }

    static void* operator new(size_t size)
    {
        return operator new(size, std::allocator_arg, std::pmr::polymorphic_allocator<>{});
    }

    static void operator delete(void* ptr, size_t size) noexcept
    {
        auto* resources = static_cast<std::pmr::memory_resource**>(ptr) - 1;
        assert(resources[0]);

        (*resources)
            ->deallocate(resources, size + sizeof(std::pmr::memory_resource*),
                alignof(std::pmr::memory_resource*));
    }
};
}  // namespace bcos::task