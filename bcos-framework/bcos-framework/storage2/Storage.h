#pragma once
#include "bcos-task/Task.h"
#include "bcos-task/Trait.h"
#include "bcos-utilities/Ranges.h"
#include <optional>

// tag_invoke storage interface
namespace bcos::storage2
{

struct STORAGE_BEGIN_TYPE
{
};
inline constexpr STORAGE_BEGIN_TYPE STORAGE_BEGIN{};

struct READ_FRONT_TYPE
{
};
inline constexpr READ_FRONT_TYPE READ_FRONT{};

template <class Invoke>
using ReturnType = typename task::AwaitableReturnType<Invoke>;

struct ReadSome
{
    auto operator()(auto& storage, RANGES::input_range auto&& keys, auto&&... args) const
        -> task::Task<ReturnType<decltype(tag_invoke(*this, storage,
            std::forward<decltype(keys)>(keys), std::forward<decltype(args)>(args)...))>>
        requires RANGES::range<ReturnType<decltype(tag_invoke(*this, storage,
            std::forward<decltype(keys)>(keys), std::forward<decltype(args)>(args)...))>>
    {
        co_return co_await tag_invoke(*this, storage, std::forward<decltype(keys)>(keys),
            std::forward<decltype(args)>(args)...);
    }
};
inline constexpr ReadSome readSome{};

struct WriteSome
{
    auto operator()(auto& storage, RANGES::input_range auto&& keys,
        RANGES::input_range auto&& values) const -> task::Task<void>
        requires std::same_as<void,
            ReturnType<decltype(tag_invoke(*this, storage, std::forward<decltype(keys)>(keys),
                std::forward<decltype(values)>(values)))>>
    {
        co_await tag_invoke(*this, storage, std::forward<decltype(keys)>(keys),
            std::forward<decltype(values)>(values));
    }
};
inline constexpr WriteSome writeSome{};

struct RemoveSome
{
    auto operator()(auto& storage, RANGES::input_range auto const& keys) const
        -> task::Task<ReturnType<decltype(tag_invoke(*this, storage, keys))>>
        requires std::same_as<void, ReturnType<decltype(tag_invoke(*this, storage, keys))>>
    {
        co_await tag_invoke(*this, storage, keys);
    }
};
inline constexpr RemoveSome removeSome{};
struct ReadOne
{
    auto operator()(auto& storage, auto const& key, auto&&... args) const
        -> task::Task<ReturnType<decltype(tag_invoke(
            *this, storage, key, std::forward<decltype(args)>(args)...))>>
    {
        co_return co_await tag_invoke(*this, storage, key, std::forward<decltype(args)>(args)...);
    }
};
inline constexpr ReadOne readOne{};

struct WriteOne
{
    auto operator()(auto& storage, auto&& key, auto&& value) const -> task::Task<void>
        requires std::same_as<void,
            ReturnType<decltype(tag_invoke(*this, storage, std::forward<decltype(key)>(key),
                std::forward<decltype(value)>(value)))>>
    {
        co_await tag_invoke(
            *this, storage, std::forward<decltype(key)>(key), std::forward<decltype(value)>(value));
    }
};
inline constexpr WriteOne writeOne{};

struct RemoveOne
{
    auto operator()(auto& storage, auto const& key) const
        -> task::Task<ReturnType<decltype(tag_invoke(*this, storage, key))>>
        requires std::same_as<void, ReturnType<decltype(tag_invoke(*this, storage, key))>>
    {
        co_await tag_invoke(*this, storage, key);
    }
};
inline constexpr RemoveOne removeOne{};

struct ExistsOne
{
    auto operator()(auto& storage, auto const& key) const
        -> task::Task<ReturnType<decltype(tag_invoke(*this, storage, key))>>
        requires std::same_as<bool, ReturnType<decltype(tag_invoke(*this, storage, key))>>
    {
        co_return co_await tag_invoke(*this, storage, key);
    }
};
inline constexpr ExistsOne existsOne{};

struct Merge
{
    auto operator()(auto& fromStorage, auto& toStorage) const
        -> task::Task<ReturnType<decltype(tag_invoke(*this, fromStorage, toStorage))>>
        requires std::same_as<void, ReturnType<decltype(tag_invoke(*this, fromStorage, toStorage))>>
    {
        co_await tag_invoke(*this, fromStorage, toStorage);
    }
};
inline constexpr Merge merge{};

struct Range
{
    auto operator()(auto& storage, auto&&... args) const -> task::Task<
        ReturnType<decltype(tag_invoke(*this, storage, std::forward<decltype(args)>(args)...))>>
    {
        co_return co_await tag_invoke(*this, storage, std::forward<decltype(args)>(args)...);
    }
};
inline constexpr Range range{};

template <auto& Tag>
using tag_t = std::decay_t<decltype(Tag)>;

// Default implementations
auto tag_invoke(bcos::storage2::tag_t<readOne> /*unused*/, auto& storage, auto const& key)
    -> task::Task<std::remove_cvref_t<decltype(std::declval<
        task::AwaitableReturnType<decltype(readSome(storage, RANGES::views::single(key)))>>()[0])>>
{
    auto values = co_await readSome(storage,
        RANGES::views::single(std::cref(key)) |
            RANGES::views::transform([](auto&& input) -> auto const& { return input.get(); }));
    co_return std::move(values[0]);
}

auto tag_invoke(bcos::storage2::tag_t<writeOne> /*unused*/, auto& storage, auto&& key, auto&& value)
    -> task::Task<void>
{
    co_await writeSome(storage, RANGES::views::single(std::forward<decltype(key)>(key)),
        RANGES::views::single(std::forward<decltype(value)>(value)));
}

auto tag_invoke(bcos::storage2::tag_t<removeOne> /*unused*/, auto& storage, auto const& key)
    -> task::Task<void>
{
    co_await removeSome(storage, RANGES::views::single(std::cref(key)) | RANGES::views::transform([
    ](auto&& input) -> auto const& { return input.get(); }));
}

auto tag_invoke(bcos::storage2::tag_t<existsOne> /*unused*/, auto& storage, auto const& key)
    -> task::Task<bool>
{
    auto result = co_await readOne(storage, key);
    co_return result.has_value();
}

task::Task<void> tag_invoke(
    bcos::storage2::tag_t<merge> /*unused*/, auto const& fromStorage, auto& toStorage)
{
    auto range = co_await storage2::range(fromStorage);
    for (auto [key, value] : range)
    {
        if (value)
        {
            co_await storage2::writeOne(toStorage, *key, *value);
        }
    }
}

}  // namespace bcos::storage2