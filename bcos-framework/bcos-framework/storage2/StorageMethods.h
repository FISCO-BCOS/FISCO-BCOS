#pragma once

#include "Storage.h"
#include <bcos-concepts/Basic.h>
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-task/Coroutine.h>
#include <bcos-task/Task.h>
#include <bcos-task/Trait.h>
#include <bcos-utilities/Ranges.h>
#include <boost/container/small_vector.hpp>
#include <boost/throw_exception.hpp>
#include <functional>
#include <optional>
#include <type_traits>

namespace bcos::storage2
{

template <class IteratorType>
concept ReadIterator =
    requires(IteratorType&& iterator) {
        requires std::convertible_to<task::AwaitableReturnType<decltype(iterator.next())>, bool>;
        requires std::same_as<typename task::AwaitableReturnType<decltype(iterator.key())>,
            typename IteratorType::Key>;
        requires std::same_as<typename task::AwaitableReturnType<decltype(iterator.value())>,
            typename IteratorType::Value>;
        requires std::convertible_to<task::AwaitableReturnType<decltype(iterator.hasValue())>,
            bool>;
    };

template <class StorageType>
concept ReadableStorage = requires(StorageType&& impl) {
                              typename StorageType::Key;
                              requires ReadIterator<task::AwaitableReturnType<decltype(impl.read(
                                  std::declval<std::vector<typename StorageType::Key>>()))>>;
                          };

template <class StorageType>
concept WriteableStorage =
    requires(StorageType&& impl) {
        typename StorageType::Key;
        typename StorageType::Value;
        requires task::IsAwaitable<decltype(impl.write(
            std::vector<typename StorageType::Key>(), std::vector<typename StorageType::Value>()))>;
    };

template <class StorageType>
concept SeekableStorage =
    requires(StorageType&& impl) {
        typename StorageType::Key;
        requires ReadIterator<task::AwaitableReturnType<decltype(impl.seek(
            std::declval<typename StorageType::Key>()))>>;
        requires ReadIterator<
            task::AwaitableReturnType<decltype(impl.seek(std::declval<STORAGE_BEGIN_TYPE>()))>>;
    };

template <class StorageType>
concept ErasableStorage = requires(StorageType&& impl) {
                              typename StorageType::Key;
                              requires task::IsAwaitable<decltype(impl.remove(
                                  std::declval<std::vector<typename StorageType::Key>>()))>;
                          };

template <storage2::ReadableStorage Storage>
struct ReadIteratorTrait
{
    using type = typename task::AwaitableReturnType<decltype(std::declval<Storage>().read(
        std::declval<std::vector<typename Storage::Key>>()))>;
};
template <storage2::ReadableStorage Storage>
using ReadIteratorType = typename ReadIteratorTrait<Storage>::type;

template <storage2::SeekableStorage Storage>
struct SeekIteratorTrait
{
    using type = typename task::AwaitableReturnType<decltype(std::declval<Storage>().seek(
        std::declval<typename Storage::Key>()))>;
};
template <storage2::SeekableStorage Storage>
using SeekIteratorType = typename SeekIteratorTrait<Storage>::type;

template <class FromStorage, class ToStorage>
concept HasMemberMergeMethod =
    requires(FromStorage& fromStorage, ToStorage& toStorage) {
        requires SeekableStorage<FromStorage>;
        requires task::IsAwaitable<decltype(toStorage.merge(fromStorage))>;
    };

template <class ToStorage>
    requires WriteableStorage<ToStorage> && ErasableStorage<ToStorage>
task::Task<void> tag_invoke(bcos::storage2::tag_t<merge> /*unused*/,
    SeekableStorage auto& fromStorage, ToStorage& toStorage)
{
    if constexpr (HasMemberMergeMethod<std::remove_cvref_t<decltype(fromStorage)>,
                      std::remove_cvref_t<decltype(toStorage)>>)
    {
        co_await toStorage.merge(fromStorage);
    }
    else
    {
        auto it = co_await fromStorage.seek(storage2::STORAGE_BEGIN);
        while (co_await it.next())
        {
            auto&& key = co_await it.key();
            if (co_await it.hasValue())
            {
                co_await storage2::writeOne(toStorage, key, co_await it.value());
            }
            else
            {
                co_await storage2::removeOne(toStorage, key);
            }
        }
    }
}

auto tag_invoke(bcos::storage2::tag_t<removeSome> /*unused*/, ErasableStorage auto& storage,
    RANGES::input_range auto const& keys) -> task::Task<void>
{
    co_await storage.remove(keys);
    co_return;
}

auto tag_invoke(bcos::storage2::tag_t<readSome> /*unused*/, ReadableStorage auto& storage,
    RANGES::input_range auto&& keys)
    -> task::Task<boost::container::small_vector<
        std::optional<std::remove_cvref_t<typename task::AwaitableReturnType<decltype(storage.read(
            std::forward<decltype(keys)>(keys)))>::Value>>,
        1>>
{
    using ValueType = std::remove_cvref_t<typename task::AwaitableReturnType<decltype(storage.read(
        std::forward<decltype(keys)>(keys)))>::Value>;
    static_assert(std::is_copy_assignable_v<ValueType>);

    boost::container::small_vector<std::optional<ValueType>, 1> values;
    if constexpr (RANGES::sized_range<decltype(keys)>)
    {
        values.reserve(RANGES::size(keys));
    }
    auto it = co_await storage.read(std::forward<decltype(keys)>(keys));
    while (co_await it.next())
    {
        if (co_await it.hasValue())
        {
            values.emplace_back(co_await it.value());
        }
        else
        {
            values.emplace_back();
        }
    }

    co_return values;
}

task::Task<void> tag_invoke(bcos::storage2::tag_t<writeSome> /*unused*/,
    WriteableStorage auto& storage, RANGES::input_range auto&& keys,
    RANGES::input_range auto&& values)
{
    co_await storage.write(
        std::forward<decltype(keys)>(keys), std::forward<decltype(values)>(values));
    co_return;
}

}  // namespace bcos::storage2