#pragma once
#include "bcos-task/Task.h"
#include "bcos-task/Trait.h"
#include <range/v3/range.hpp>
#include <range/v3/view/indirect.hpp>
#include <range/v3/view/single.hpp>
#include <range/v3/view/transform.hpp>
#include <range/v3/view/zip.hpp>
#include <tuple>
#include <type_traits>
#include <variant>
#include <vector>

// tag_invoke storage interface
namespace bcos::storage2
{

inline constexpr struct DIRECT_TYPE
{
} DIRECT;

inline constexpr struct RANGE_SEEK_TYPE
{
} RANGE_SEEK;

struct NOT_EXISTS_TYPE
{
};
constexpr inline struct DELETED_TYPE
{
} deleteItem;

template <class Value>
using StorageValueType = std::variant<NOT_EXISTS_TYPE, DELETED_TYPE, Value>;

template <class Invoke>
using ReturnType = typename task::AwaitableReturnType<Invoke>;

inline constexpr struct ReadSome
{
    auto operator()(auto& storage, ::ranges::input_range auto keys, auto&&... args) const
        -> task::Task<task::AwaitableReturnType<decltype(storage.readSome(
            std::move(keys), std::forward<decltype(args)>(args)...))>>
        requires requires {
            storage.readSome(std::move(keys), std::forward<decltype(args)>(args)...);
        }
    {
        co_return co_await storage.readSome(std::move(keys), std::forward<decltype(args)>(args)...);
    }

    auto operator()(auto& storage, ::ranges::input_range auto keys, auto&&... args) const
        -> task::Task<task::AwaitableReturnType<decltype(tag_invoke(
            *this, storage, std::move(keys), std::forward<decltype(args)>(args)...))>>
        requires(
            !requires { storage.readSome(std::move(keys), std::forward<decltype(args)>(args)...); })
    {
        co_return co_await tag_invoke(
            *this, storage, std::move(keys), std::forward<decltype(args)>(args)...);
    }
} readSome;

inline constexpr struct WriteSome
{
    auto operator()(auto& storage, ::ranges::input_range auto keyValues, auto&&... args) const
        -> task::Task<task::AwaitableReturnType<decltype(storage.writeSome(
            std::move(keyValues), std::forward<decltype(args)>(args)...))>>
        requires(std::tuple_size_v<::ranges::range_value_t<decltype(keyValues)>> >= 2) && requires {
            storage.writeSome(std::move(keyValues), std::forward<decltype(args)>(args)...);
        }
    {
        using Return = decltype(storage.writeSome(
            std::move(keyValues), std::forward<decltype(args)>(args)...));
        if constexpr (std::is_void_v<task::AwaitableReturnType<Return>>)
        {
            co_await storage.writeSome(std::move(keyValues), std::forward<decltype(args)>(args)...);
            co_return;
        }
        else
        {
            co_return co_await storage.writeSome(
                std::move(keyValues), std::forward<decltype(args)>(args)...);
        }
    }

    auto operator()(auto& storage, ::ranges::input_range auto keyValues, auto&&... args) const
        -> task::Task<task::AwaitableReturnType<decltype(tag_invoke(
            *this, storage, std::move(keyValues), std::forward<decltype(args)>(args)...))>>
        requires(std::tuple_size_v<::ranges::range_value_t<decltype(keyValues)>> >= 2) &&
                (!requires {
                    storage.writeSome(std::move(keyValues), std::forward<decltype(args)>(args)...);
                })
    {
        using Return = decltype(tag_invoke(
            *this, storage, std::move(keyValues), std::forward<decltype(args)>(args)...));
        if constexpr (std::is_void_v<task::AwaitableReturnType<Return>>)
        {
            co_await tag_invoke(
                *this, storage, std::move(keyValues), std::forward<decltype(args)>(args)...);
            co_return;
        }
        else
        {
            co_return co_await tag_invoke(
                *this, storage, std::move(keyValues), std::forward<decltype(args)>(args)...);
        }
    }
} writeSome;

inline constexpr struct RemoveSome
{
    auto operator()(auto& storage, ::ranges::input_range auto keys, auto&&... args) const
        -> task::Task<task::AwaitableReturnType<decltype(storage.removeSome(
            std::move(keys), std::forward<decltype(args)>(args)...))>>
        requires requires {
            storage.removeSome(std::move(keys), std::forward<decltype(args)>(args)...);
        }
    {
        using Return =
            decltype(storage.removeSome(std::move(keys), std::forward<decltype(args)>(args)...));
        if constexpr (std::is_void_v<task::AwaitableReturnType<Return>>)
        {
            co_await storage.removeSome(std::move(keys), std::forward<decltype(args)>(args)...);
            co_return;
        }
        else
        {
            co_return co_await storage.removeSome(
                std::move(keys), std::forward<decltype(args)>(args)...);
        }
    }

    auto operator()(auto& storage, ::ranges::input_range auto keys, auto&&... args) const
        -> task::Task<task::AwaitableReturnType<decltype(tag_invoke(
            *this, storage, std::move(keys), std::forward<decltype(args)>(args)...))>>
        requires(!requires {
            storage.removeSome(std::move(keys), std::forward<decltype(args)>(args)...);
        })
    {
        using Return = decltype(tag_invoke(
            *this, storage, std::move(keys), std::forward<decltype(args)>(args)...));
        if constexpr (std::is_void_v<task::AwaitableReturnType<Return>>)
        {
            co_await tag_invoke(
                *this, storage, std::move(keys), std::forward<decltype(args)>(args)...);
            co_return;
        }
        else
        {
            co_return co_await tag_invoke(
                *this, storage, std::move(keys), std::forward<decltype(args)>(args)...);
        }
    }
} removeSome;

template <class IteratorType>
concept Iterator =
    requires(IteratorType iterator) { requires task::IsAwaitable<decltype(iterator.next())>; };
inline constexpr struct Range
{
    auto operator()(auto& storage, auto&&... args) const
        -> task::Task<ReturnType<decltype(storage.range(std::forward<decltype(args)>(args)...))>>
        requires Iterator<
            ReturnType<decltype(storage.range(std::forward<decltype(args)>(args)...))>>
    {
        co_return co_await storage.range(std::forward<decltype(args)>(args)...);
    }
} range;

inline constexpr struct ReadOne
{
    auto operator()(auto& storage, auto key, auto&&... args) const
        -> task::Task<task::AwaitableReturnType<decltype(storage.readOne(
            std::move(key), std::forward<decltype(args)>(args)...))>>
        requires requires {
            storage.readOne(std::move(key), std::forward<decltype(args)>(args)...);
        }
    {
        co_return co_await storage.readOne(std::move(key), std::forward<decltype(args)>(args)...);
    }

    auto operator()(auto& storage, auto key, auto&&... args) const
        -> task::Task<task::AwaitableReturnType<decltype(tag_invoke(
            *this, storage, std::move(key), std::forward<decltype(args)>(args)...))>>
        requires(
            !requires { storage.readOne(std::move(key), std::forward<decltype(args)>(args)...); })
    {
        co_return co_await tag_invoke(
            *this, storage, std::move(key), std::forward<decltype(args)>(args)...);
    }
} readOne;

inline constexpr struct WriteOne
{
    auto operator()(auto& storage, auto key, auto value, auto&&... args) const
        -> task::Task<task::AwaitableReturnType<decltype(storage.writeOne(
            std::move(key), std::move(value), std::forward<decltype(args)>(args)...))>>
        requires requires {
            storage.writeOne(
                std::move(key), std::move(value), std::forward<decltype(args)>(args)...);
        }
    {
        using Return = decltype(storage.writeOne(
            std::move(key), std::move(value), std::forward<decltype(args)>(args)...));
        if constexpr (std::is_void_v<task::AwaitableReturnType<Return>>)
        {
            co_await storage.writeOne(
                std::move(key), std::move(value), std::forward<decltype(args)>(args)...);
            co_return;
        }
        else
        {
            co_return co_await storage.writeOne(
                std::move(key), std::move(value), std::forward<decltype(args)>(args)...);
        }
    }

    auto operator()(auto& storage, auto key, auto value, auto&&... args) const
        -> task::Task<task::AwaitableReturnType<decltype(tag_invoke(*this, storage, std::move(key),
            std::move(value), std::forward<decltype(args)>(args)...))>>
        requires(!requires {
            storage.writeOne(
                std::move(key), std::move(value), std::forward<decltype(args)>(args)...);
        })
    {
        using Return = decltype(tag_invoke(*this, storage, std::move(key), std::move(value),
            std::forward<decltype(args)>(args)...));
        if constexpr (std::is_void_v<task::AwaitableReturnType<Return>>)
        {
            co_await tag_invoke(*this, storage, std::move(key), std::move(value),
                std::forward<decltype(args)>(args)...);
            co_return;
        }
        else
        {
            co_return co_await tag_invoke(*this, storage, std::move(key), std::move(value),
                std::forward<decltype(args)>(args)...);
        }
    }
} writeOne;

inline constexpr struct WriteOneIf
{
    // Atomically reads existing value, writes only if predicate(existing) is true.
    // If no existing value, writes unconditionally.
    // Predicate: (Value const&) -> bool
    // Returns true if the write was performed.
    auto operator()(auto& storage, auto key, auto value, auto predicate, auto&&... args) const
        -> task::Task<bool>
        requires requires {
            storage.writeOneIf(std::move(key), std::move(value), std::move(predicate),
                std::forward<decltype(args)>(args)...);
        } && std::is_same_v<task::AwaitableReturnType<decltype(storage.writeOneIf(std::move(key),
                                std::move(value), std::move(predicate),
                                std::forward<decltype(args)>(args)...))>,
                 bool>
    {
        co_return co_await storage.writeOneIf(std::move(key), std::move(value),
            std::move(predicate), std::forward<decltype(args)>(args)...);
    }
} writeOneIf;

inline constexpr struct RemoveOne
{
    auto operator()(auto& storage, auto key, auto&&... args) const
        -> task::Task<task::AwaitableReturnType<decltype(storage.removeOne(
            std::move(key), std::forward<decltype(args)>(args)...))>>
        requires requires {
            storage.removeOne(std::move(key), std::forward<decltype(args)>(args)...);
        }
    {
        using Return =
            decltype(storage.removeOne(std::move(key), std::forward<decltype(args)>(args)...));
        if constexpr (std::is_void_v<task::AwaitableReturnType<Return>>)
        {
            co_await storage.removeOne(std::move(key), std::forward<decltype(args)>(args)...);
            co_return;
        }
        else
        {
            co_return co_await storage.removeOne(
                std::move(key), std::forward<decltype(args)>(args)...);
        }
    }

    auto operator()(auto& storage, auto key, auto&&... args) const
        -> task::Task<task::AwaitableReturnType<decltype(tag_invoke(
            *this, storage, std::move(key), std::forward<decltype(args)>(args)...))>>
        requires(
            !requires { storage.removeOne(std::move(key), std::forward<decltype(args)>(args)...); })
    {
        using Return = decltype(tag_invoke(
            *this, storage, std::move(key), std::forward<decltype(args)>(args)...));
        if constexpr (std::is_void_v<task::AwaitableReturnType<Return>>)
        {
            co_await tag_invoke(
                *this, storage, std::move(key), std::forward<decltype(args)>(args)...);
            co_return;
        }
        else
        {
            co_return co_await tag_invoke(
                *this, storage, std::move(key), std::forward<decltype(args)>(args)...);
        }
    }
} removeOne;

inline constexpr struct ExistsOne
{
    auto operator()(auto& storage, auto key, auto&&... args) const -> task::Task<bool>
    {
        if constexpr (requires {
                          storage.existsOne(std::move(key), std::forward<decltype(args)>(args)...);
                      })
        {
            co_return co_await storage.existsOne(
                std::move(key), std::forward<decltype(args)>(args)...);
        }
        else
        {
            auto value =
                co_await readOne(storage, std::move(key), std::forward<decltype(args)>(args)...);
            co_return static_cast<bool>(value);
        }
    }
} existsOne;

inline constexpr struct InsertIfAbsent
{
    auto operator()(auto& storage, auto key, auto value, auto&&... args) const -> task::Task<bool>
        requires requires {
            storage.insertIfAbsent(
                std::move(key), std::move(value), std::forward<decltype(args)>(args)...);
        } &&
                 std::is_same_v<
                     task::AwaitableReturnType<decltype(storage.insertIfAbsent(
                         std::move(key), std::move(value), std::forward<decltype(args)>(args)...))>,
                     bool>
    {
        co_return co_await storage.insertIfAbsent(
            std::move(key), std::move(value), std::forward<decltype(args)>(args)...);
    }
} insertIfAbsent;

inline constexpr struct Merge
{
    auto operator()(auto& toStorage, auto& fromStorage, auto&&... args) const
        -> task::Task<task::AwaitableReturnType<decltype(toStorage.merge(
            fromStorage, std::forward<decltype(args)>(args)...))>>
    {
        using Return =
            decltype(toStorage.merge(fromStorage, std::forward<decltype(args)>(args)...));
        if constexpr (std::is_void_v<task::AwaitableReturnType<Return>>)
        {
            co_await toStorage.merge(fromStorage, std::forward<decltype(args)>(args)...);
            co_return;
        }
        else
        {
            co_return co_await toStorage.merge(fromStorage, std::forward<decltype(args)>(args)...);
        }
    }
} merge;

template <class Storage, class Key>
concept ReadableStorage = requires(Storage& storage, std::vector<Key> keys, Key key) {
    { readSome(storage, keys) } -> task::IsAwaitable;
    { readOne(storage, key) } -> task::IsAwaitable;
};
template <class Storage, class Key, class Value>
concept WritableStorage = requires(
    Storage& storage, std::vector<std::tuple<Key, Value>> keyValues, Key key, Value value) {
    { writeSome(storage, keyValues) } -> task::IsAwaitable;
    { writeOne(storage, key, value) } -> task::IsAwaitable;
};
template <class Storage, class Key, class Value>
concept ReadWriteStorage = ReadableStorage<Storage, Key> && WritableStorage<Storage, Key, Value>;

template <auto& Tag>
using tag_t = std::decay_t<decltype(Tag)>;
}  // namespace bcos::storage2
