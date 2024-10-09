#pragma once

#include <boost/mp11.hpp>
#include <cassert>
#include <cstddef>
#include <memory_resource>

namespace bcos::task
{

template <typename... Args>
std::pmr::memory_resource* getMemoryResourceFromArgs(Args&&... args)
{
    using args_type = boost::mp11::mp_list<std::decay_t<Args>...>;
    constexpr static auto index = boost::mp11::mp_find<args_type, std::allocator_arg_t>::value;
    if constexpr (sizeof...(Args) == index)
    {
        return std::pmr::get_default_resource();
    }
    else
    {
        return std::get<index + 1U>(std::tie(args...)).resource();
    }
}

// 如果把allocator指针放在内存区的头部，使用bcos::u256时会出现异常，原因暂未弄清，因此放在内存区的尾部
// If the allocator pointer is placed at the beginning of the memory area, an exception will occur
// when using bcos::u256, and the reason has not been determined yet. Therefore, it is placed at the
// end of the memory area.
struct MemoryResourceBase
{
    static std::pmr::memory_resource*& getAllocator(void* ptr, size_t size);

    static void* operator new(size_t size, const auto&... args /*unused*/)
    {
        auto* memoryResource = getMemoryResourceFromArgs(args...);
        auto* resources = memoryResource->allocate(
            size + sizeof(std::pmr::memory_resource*), alignof(std::pmr::memory_resource*));
        assert(resources);

        getAllocator(resources, size) = memoryResource;
        return resources;
    }

    static void* operator new(size_t size);

    static void operator delete(void* ptr, size_t size) noexcept;
};
}  // namespace bcos::task