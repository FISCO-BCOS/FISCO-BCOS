#pragma once

#include <boost/mp11.hpp>
#include <cassert>
#include <cstddef>
#include <memory_resource>
#include <span>

#pragma GCC diagnostic ignored "-Wmismatched-new-delete"

namespace bcos::task::pmr
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

struct MemoryResourceBase
{
    static void* operator new(size_t size, const auto&... args /*unused*/)
    {
        auto* memoryResource = getMemoryResourceFromArgs(args...);
        auto* resources = memoryResource->allocate(
            size + sizeof(std::pmr::memory_resource*), alignof(std::pmr::memory_resource*));
        assert(resources);

        std::span view(static_cast<std::pmr::memory_resource**>(resources), 2);
        view[0] = memoryResource;
        return static_cast<void*>(std::addressof(view[1]));
    }

    static void* operator new(size_t size);
    static void operator delete(void* ptr, size_t size) noexcept;
};
}  // namespace bcos::task::pmr