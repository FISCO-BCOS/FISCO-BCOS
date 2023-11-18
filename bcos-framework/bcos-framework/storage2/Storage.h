#pragma once
#include "bcos-task/Task.h"
#include "bcos-task/Trait.h"
#include "bcos-utilities/Ranges.h"
#include <optional>

// tag_invoke storage interface
namespace bcos::storage2
{

struct READ_FRONT_TYPE
{
};
inline constexpr READ_FRONT_TYPE READ_FRONT{};

template <class Invoke>
using ReturnType = typename task::AwaitableReturnType<Invoke>;
template <class Tag, class Storage, class... Args>
concept HasTag = requires(Tag&& tag, Storage&& storage, Args&&... args) {
                     requires task::IsAwaitable<decltype(tag_invoke(
                         std::forward<decltype(tag)>(tag), std::forward<Storage>(storage), std::forward<Args>(args)...))>;
                 };

inline constexpr struct ReadSome
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
} readSome{};

struct WriteSome
{
    auto operator()(auto& storage, RANGES::input_range auto&& keys,
        RANGES::input_range auto&& values) const -> task::Task<void>
    {
        co_await tag_invoke(*this, storage, std::forward<decltype(keys)>(keys),
            std::forward<decltype(values)>(values));
    }
};
inline constexpr WriteSome writeSome{};

inline constexpr struct RemoveSome
{
    auto operator()(auto& storage, RANGES::input_range auto&& keys) const -> task::Task<
        ReturnType<decltype(tag_invoke(*this, storage, std::forward<decltype(keys)>(keys)))>>
    {
        co_await tag_invoke(*this, storage, std::forward<decltype(keys)>(keys));
    }
} removeSome{};

inline constexpr struct ReadOne
{
    auto operator()(auto& storage, auto&& key, auto&&... args) const
        -> task::Task<ReturnType<decltype(tag_invoke(*this, storage,
            std::forward<decltype(key)>(key), std::forward<decltype(args)>(args)...))>>
    {
        if constexpr (HasTag<ReadOne, std::decay_t<decltype(storage)>, decltype(key),
                          decltype(args)...>)
        {
            co_return co_await tag_invoke(*this, storage, std::forward<decltype(key)>(key),
                std::forward<decltype(args)>(args)...);
        }
        else
        {
            auto values = co_await storage2::readSome(storage,
                RANGES::views::single(std::forward<decltype(key)>(key)),
                std::forward<decltype(args)>(args)...);
            co_return std::move(values[0]);
        }
    }
} readOne{};


inline constexpr struct WriteOne
{
    auto operator()(auto& storage, auto&& key, auto&& value, auto&&... args) const
        -> task::Task<void>
    {
        if constexpr (HasTag<WriteOne, std::decay_t<decltype(storage)>, decltype(key),
                          decltype(value), decltype(args)...>)
        {
            co_await tag_invoke(*this, storage, std::forward<decltype(key)>(key),
                std::forward<decltype(value)>(value), std::forward<decltype(args)>(args)...);
        }
        else
        {
            co_await writeSome(storage, RANGES::views::single(std::forward<decltype(key)>(key)),
                RANGES::views::single(std::forward<decltype(value)>(value)),
                std::forward<decltype(args)>(args)...);
        }
    }
} writeOne{};

inline constexpr struct RemoveOne
{
    auto operator()(auto& storage, auto&& key, auto&&... args) const
        -> task::Task<ReturnType<decltype(tag_invoke(*this, storage, key, args...))>>
    {
        if constexpr (HasTag<RemoveOne, std::decay_t<decltype(storage)>, decltype(key),
                          decltype(args)...>)
        {
            co_await tag_invoke(*this, storage, std::forward<decltype(key)>(key),
                std::forward<decltype(args)>(args)...);
        }
        else
        {
            co_await removeSome(storage, RANGES::views::single(std::forward<decltype(key)>(key)),
                std::forward<decltype(args)>(args)...);
        }
    }
} removeOne{};

inline constexpr struct ExistsOne
{
    auto operator()(auto& storage, auto&& key, auto&&... args) const -> task::Task<
        ReturnType<decltype(tag_invoke(*this, storage, std::forward<decltype(key)>(key)))>>
        requires std::same_as<bool,
            ReturnType<decltype(tag_invoke(*this, storage, std::forward<decltype(key)>(key)))>>
    {
        if constexpr (HasTag<ExistsOne, std::decay_t<decltype(storage)>, decltype(key)>)
        {
            co_return co_await tag_invoke(*this, storage, std::forward<decltype(key)>(key),
                std::forward<decltype(args)>(args)...);
        }
        else
        {
            auto result = co_await readOne(storage, key, std::forward<decltype(args)>(args)...);
            co_return result.has_value();
        }
    }
} existsOne{};

inline constexpr struct Range
{
    auto operator()(auto& storage, auto&&... args) const -> task::Task<
        ReturnType<decltype(tag_invoke(*this, storage, std::forward<decltype(args)>(args)...))>>
    {
        co_return co_await tag_invoke(*this, storage, std::forward<decltype(args)>(args)...);
    }
} range{};

inline constexpr struct Merge
{
    auto operator()(auto& fromStorage, auto& toStorage, auto&&... args) const
        -> task::Task<ReturnType<decltype(tag_invoke(*this, fromStorage, toStorage))>>
    {
        if constexpr (HasTag<Merge, decltype(fromStorage), decltype(toStorage)>)
        {
            co_await tag_invoke(*this, fromStorage, toStorage);
        }
        else
        {
            auto range = co_await storage2::range(fromStorage);
            for (auto [key, value] : range)
            {
                if (value)
                {
                    co_await storage2::writeOne(toStorage, *key, *value);
                }
                else
                {
                    co_await storage2::removeOne(toStorage, *key);
                }
            }
        }
    }
} merge{};

template <auto& Tag>
using tag_t = std::decay_t<decltype(Tag)>;

}  // namespace bcos::storage2