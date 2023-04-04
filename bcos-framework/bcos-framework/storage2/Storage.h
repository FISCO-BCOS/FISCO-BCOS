#pragma once

#include <bcos-concepts/Basic.h>
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-task/Coroutine.h>
#include <bcos-task/Task.h>
#include <bcos-task/Trait.h>
#include <bcos-utilities/Ranges.h>
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

struct STORAGE_BEGIN_TYPE
{
};
inline constexpr STORAGE_BEGIN_TYPE STORAGE_BEGIN{};

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

template <class Iterator>
concept RangeableIterator = requires(Iterator&& iterator) {
                                requires storage2::ReadIterator<Iterator>;
                                {
                                    iterator.range()
                                    } -> RANGES::range;
                            };

inline auto singleView(auto&& value)
{
    using ValueType = decltype(value);

    if constexpr (std::is_rvalue_reference_v<ValueType>)
    {
        return RANGES::views::single(std::forward<decltype(value)>(value));
    }
    else if constexpr (std::is_const_v<ValueType>)
    {
        return RANGES::views::single(std::cref(value)) |
               RANGES::views::transform([](auto&& input) -> auto const& { return input.get(); });
    }
    else
    {
        return RANGES::views::single(std::ref(value)) |
               RANGES::views::transform([](auto&& input) -> auto& { return input.get(); });
    }
}

inline task::Task<bool> existsOne(ReadableStorage auto& storage, auto const& key)
{
    auto it = co_await storage.read(storage2::singleView(key));
    co_await it.next();
    co_return co_await it.hasValue();
}

inline auto readOne(ReadableStorage auto& storage, auto const& key)
    -> task::Task<std::optional<std::remove_cvref_t<typename task::AwaitableReturnType<
        decltype(storage.read(storage2::singleView(key)))>::Value>>>
{
    using ValueType = std::remove_cvref_t<typename task::AwaitableReturnType<decltype(storage.read(
        storage2::singleView(key)))>::Value>;
    static_assert(std::is_copy_assignable_v<ValueType>);

    auto it = co_await storage.read(storage2::singleView(key));
    co_await it.next();
    std::optional<ValueType> result;
    if (co_await it.hasValue())
    {
        result.emplace(co_await it.value());
    }

    co_return result;
}

inline task::Task<void> writeOne(WriteableStorage auto& storage, auto const& key, auto&& value)
{
    co_await storage.write(
        storage2::singleView(key), storage2::singleView(std::forward<decltype(value)>(value)));
    co_return;
}

inline task::Task<void> removeOne(ErasableStorage auto& storage, auto const& key)
{
    co_await storage.remove(storage2::singleView(key));
    co_return;
}

namespace detail
{
template <class FromStorage, class ToStorage>
concept HasMemberMergeMethod =
    requires(FromStorage& fromStorage, ToStorage& toStorage) {
        requires SeekableStorage<FromStorage>;
        requires task::IsAwaitable<decltype(toStorage.merge(fromStorage))>;
    };
struct merge
{
    template <class ToStorage>
        requires WriteableStorage<ToStorage> && ErasableStorage<ToStorage>
    task::Task<void> operator()(SeekableStorage auto& fromStorage, ToStorage& toStorage) const
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
};
}  // namespace detail
constexpr inline detail::merge merge{};

}  // namespace bcos::storage2