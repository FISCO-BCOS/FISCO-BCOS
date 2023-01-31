#pragma once

#include <bcos-concepts/Basic.h>
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-task/Coroutine.h>
#include <bcos-task/Task.h>
#include <bcos-task/Trait.h>
#include <bcos-utilities/Ranges.h>
#include <boost/graph/graph_concepts.hpp>
#include <boost/throw_exception.hpp>
#include <functional>
#include <optional>
#include <range/v3/view/transform.hpp>
#include <type_traits>

namespace bcos::storage2
{

template <class IteratorType>
concept ReadIterator = requires(IteratorType iterator)
{
    typename IteratorType::Key;
    typename IteratorType::Value;
    requires std::convertible_to < task::AwaitableReturnType<decltype(iterator.next())>,
    bool > ;
    requires std::same_as < typename task::AwaitableReturnType<decltype(iterator.key())>,
    typename IteratorType::Key > ;
    requires std::same_as < typename task::AwaitableReturnType<decltype(iterator.value())>,
    typename IteratorType::Value > ;
    requires std::convertible_to < task::AwaitableReturnType<decltype(iterator.hasValue())>,
    bool > ;
};

template <class StorageType, class KeyType>
concept ReadableStorage = requires(StorageType&& impl, RANGES::any_view<KeyType> keys)
{
    requires ReadIterator<task::AwaitableReturnType<decltype(impl.read(keys))>>;
};

template <class StorageType, class KeyType, class ValueType>
concept WriteableStorage = requires(StorageType&& impl, KeyType&& key)
{
    requires task::IsAwaitable<decltype(impl.write(
        RANGES::any_view<KeyType>(), RANGES::any_view<ValueType>()))>;
    requires task::IsAwaitable<decltype(impl.remove(RANGES::any_view<KeyType>()))>;
};

template <class StorageType, class KeyType>
concept SeekableStorage = requires(StorageType&& impl, KeyType&& key)
{
    requires ReadIterator<task::AwaitableReturnType<decltype(impl.seek(key))>>;
};

auto single(auto&& value)
{
    return RANGES::single_view(std::ref(value)) |
           RANGES::views::transform([](auto&& input) -> auto& { return input.get(); });
}

template <class KeyType>
task::Task<bool> existsOne(ReadableStorage<KeyType> auto& storage, KeyType const& key)
{
    auto it = co_await storage.read(storage2::single(key));
    co_await it.next();
    co_return co_await it.hasValue();
}

template <class Reference>
using ValueOrReferenceWrapper = std::conditional_t<std::is_reference_v<Reference>,
    std::reference_wrapper<std::remove_reference_t<Reference>>, Reference>;

template <class KeyType>
auto readOne(ReadableStorage<KeyType> auto& storage, KeyType const& key)
    -> task::Task<std::optional<ValueOrReferenceWrapper<
        typename task::AwaitableReturnType<decltype(storage.read(storage2::single(key)))>::Value>>>
{
    using ValueType = ValueOrReferenceWrapper<
        typename task::AwaitableReturnType<decltype(storage.read(storage2::single(key)))>::Value>;

    std::optional<ValueType> result;
    auto keys = storage2::single(key);
    auto it = co_await storage.read(keys);
    co_await it.next();
    if (co_await it.hasValue())
    {
        result.emplace(std::forward<ValueType>(co_await it.value()));
    }

    co_return result;
}

template <class KeyType, class ValueType>
task::Task<void> writeOne(
    WriteableStorage<KeyType, ValueType> auto& storage, KeyType const& key, ValueType&& value)
{
    co_await storage.write(
        storage2::single(key), storage2::single(std::forward<decltype(value)>(value)));
    co_return;
}

template <class KeyType, class ValueType>
task::Task<void> removeOne(WriteableStorage<KeyType, ValueType> auto& storage, KeyType const& key)
{
    co_await storage.remove(storage2::single(key));
    co_return;
}

}  // namespace bcos::storage2