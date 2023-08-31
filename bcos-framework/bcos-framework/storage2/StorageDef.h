#pragma once
#include "bcos-task/Task.h"
#include "bcos-task/Trait.h"
#include "bcos-utilities/Ranges.h"
#include <boost/container/small_vector.hpp>
#include <optional>

namespace bcos::storage2
{

template <class Invoke>
using ReturnType = typename task::AwaitableReturnType<Invoke>;

struct ReadSome
{
    auto operator()(auto& storage, RANGES::input_range auto const& keys) const
        -> task::Task<ReturnType<decltype(tag_invoke(*this, storage, keys))>>
        requires RANGES::range<ReturnType<decltype(tag_invoke(*this, storage, keys))>>
    {
        co_return co_await tag_invoke(*this, storage, keys);
    }
};

struct ReadOne
{
    auto operator()(auto& storage, auto const& key) const
        -> task::Task<ReturnType<decltype(tag_invoke(*this, storage, key))>>
    {
        co_return co_await tag_invoke(*this, storage, key);
    }
};

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

inline constexpr ReadSome readSome{};
inline constexpr ReadOne readOne{};  // With default implementation
inline constexpr WriteSome writeSome{};
inline constexpr WriteOne writeOne{};  // With default implementation

template <auto& Tag>
using tag_t = std::decay_t<decltype(Tag)>;

// Default implementations
auto tag_invoke(bcos::storage2::tag_t<readOne> /*unused*/, auto& storage, auto const& key)
    -> task::Task<std::remove_cvref_t<decltype(std::declval<
        task::AwaitableReturnType<decltype(readSome(storage, RANGES::views::single(key)))>>()[0])>>
{
    auto values = co_await readSome(storage, RANGES::views::single(key));
    co_return std::move(values[0]);
}

auto tag_invoke(bcos::storage2::tag_t<writeOne> /*unused*/, auto& storage, auto&& key, auto&& value)
    -> task::Task<void>
{
    co_await writeSome(storage, RANGES::views::single(std::forward<decltype(key)>(key)),
        RANGES::views::single(std::forward<decltype(value)>(value)));
}

}  // namespace bcos::storage2