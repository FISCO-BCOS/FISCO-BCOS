#pragma once
#include "bcos-task/Task.h"
#include "bcos-task/Trait.h"
#include <optional>
#include <range/v3/range.hpp>
#include <range/v3/view/indirect.hpp>
#include <range/v3/view/single.hpp>
#include <range/v3/view/transform.hpp>
#include <type_traits>

// tag_invoke storage interface
namespace bcos::storage2
{

inline constexpr struct DIRECT_TYPE
{
} DIRECT{};

inline constexpr struct RANGE_SEEK_TYPE
{
} RANGE_SEEK{};

template <class Tag, class Storage, class... Args>
concept HasTag = requires(Tag tag, Storage storage, Args&&... args) {
    requires task::IsAwaitable<decltype(tag_invoke(tag, storage, std::forward<Args>(args)...))>;
};

inline constexpr struct ReadSome
{
    decltype(auto) operator()(
        auto&& storage, ::ranges::input_range auto&& keys, auto&&... args) const
    {
        decltype(auto) awaitable = tag_invoke(*this, std::forward<decltype(storage)>(storage),
            std::forward<decltype(keys)>(keys), std::forward<decltype(args)>(args)...);

        using AwaitableType = decltype(awaitable);
        static_assert(task::IsAwaitable<AwaitableType> &&
                      ::ranges::range<task::AwaitableReturnType<AwaitableType>>);
        return awaitable;
    }
} readSome{};

inline constexpr struct WriteSome
{
    decltype(auto) operator()(auto&& storage, ::ranges::input_range auto&& keys,
        ::ranges::input_range auto&& values, auto&&... args) const
    {
        decltype(auto) awaitable = tag_invoke(*this, std::forward<decltype(storage)>(storage),
            std::forward<decltype(keys)>(keys), std::forward<decltype(values)>(values),
            std::forward<decltype(args)>(args)...);

        static_assert(task::IsAwaitable<decltype(awaitable)>);
        return awaitable;
    }
} writeSome{};

inline constexpr struct RemoveSome
{
    decltype(auto) operator()(
        auto&& storage, ::ranges::input_range auto&& keys, auto&&... args) const
    {
        decltype(auto) awaitable = tag_invoke(*this, std::forward<decltype(storage)>(storage),
            std::forward<decltype(keys)>(keys), std::forward<decltype(args)>(args)...);

        static_assert(task::IsAwaitable<decltype(awaitable)>);
        return awaitable;
    }
} removeSome{};

template <class IteratorType>
concept Iterator =
    requires(IteratorType iterator) { requires task::IsAwaitable<decltype(iterator.next())>; };
inline constexpr struct Range
{
    decltype(auto) operator()(auto&& storage, auto&&... args) const
    {
        decltype(auto) awaitable = tag_invoke(
            *this, std::forward<decltype(storage)>(storage), std::forward<decltype(args)>(args)...);

        using AwaitableType = decltype(awaitable);
        static_assert(
            task::IsAwaitable<AwaitableType> && Iterator<task::AwaitableReturnType<AwaitableType>>);
        return awaitable;
    }
} range{};

namespace detail
{
auto toSingleView(auto&& item)
{
    if constexpr (std::is_lvalue_reference_v<decltype(item)>)
    {
        return ::ranges::views::single(std::addressof(item)) | ::ranges::views::indirect;
    }
    else
    {
        return ::ranges::views::single(std::forward<decltype(item)>(item));
    }
}
}  // namespace detail

inline constexpr struct ReadOne
{
    auto operator()(auto&& storage, auto&& key, auto&&... args) const
        -> task::Task<std::optional<typename std::decay_t<decltype(storage)>::Value>>
    {
        if constexpr (HasTag<ReadOne, decltype(storage), decltype(key), decltype(args)...>)
        {
            co_return co_await tag_invoke(*this, std::forward<decltype(storage)>(storage),
                std::forward<decltype(key)>(key), std::forward<decltype(args)>(args)...);
        }
        else
        {
            auto values = co_await storage2::readSome(std::forward<decltype(storage)>(storage),
                detail::toSingleView(std::forward<decltype(key)>(key)),
                std::forward<decltype(args)>(args)...);
            co_return std::move(values[0]);
        }
    }
} readOne{};

inline constexpr struct WriteOne
{
    decltype(auto) operator()(auto&& storage, auto&& key, auto&& value, auto&&... args) const
    {
        if constexpr (HasTag<WriteOne, decltype(storage), decltype(key), decltype(value),
                          decltype(args)...>)
        {
            return tag_invoke(*this, std::forward<decltype(storage)>(storage),
                std::forward<decltype(key)>(key), std::forward<decltype(value)>(value),
                std::forward<decltype(args)>(args)...);
        }
        else
        {
            return writeSome(std::forward<decltype(storage)>(storage),
                detail::toSingleView(std::forward<decltype(key)>(key)),
                detail::toSingleView(std::forward<decltype(value)>(value)),
                std::forward<decltype(args)>(args)...);
        }
    }
} writeOne{};

inline constexpr struct RemoveOne
{
    decltype(auto) operator()(auto&& storage, auto&& key, auto&&... args) const
    {
        if constexpr (HasTag<RemoveOne, decltype(storage), decltype(key), decltype(args)...>)
        {
            return tag_invoke(*this, std::forward<decltype(storage)>(storage),
                std::forward<decltype(key)>(key), std::forward<decltype(args)>(args)...);
        }
        else
        {
            return removeSome(std::forward<decltype(storage)>(storage),
                detail::toSingleView(std::forward<decltype(key)>(key)),
                std::forward<decltype(args)>(args)...);
        }
    }
} removeOne{};

inline constexpr struct ExistsOne
{
    auto operator()(auto&& storage, auto&& key, auto&&... args) const -> task::Task<bool>
    {
        if constexpr (HasTag<ExistsOne, decltype(storage), decltype(key), decltype(args)...>)
        {
            co_return co_await tag_invoke(*this, std::forward<decltype(storage)>(storage),
                std::forward<decltype(key)>(key), std::forward<decltype(args)>(args)...);
        }
        else
        {
            auto result = co_await readOne(
                storage, std::forward<decltype(key)>(key), std::forward<decltype(args)>(args)...);
            co_return result.has_value();
        }
    }
} existsOne{};

inline constexpr struct Merge
{
    decltype(auto) operator()(auto& toStorage, auto&& fromStorage, auto&&... args) const
    {
        auto awaitable =
            tag_invoke(*this, toStorage, std::forward<decltype(fromStorage)>(fromStorage),
                std::forward<decltype(args)>(args)...);

        static_assert(task::IsAwaitable<decltype(awaitable)>);
        return awaitable;
    }
} merge{};

template <auto& Tag>
using tag_t = std::decay_t<decltype(Tag)>;
}  // namespace bcos::storage2
