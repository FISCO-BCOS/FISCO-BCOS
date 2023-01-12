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
concept SeekIterator = requires(IteratorType iterator)
{
    typename IteratorType::Key;
    typename IteratorType::Value;
    std::convertible_to<task::AwaitableReturnType<decltype(iterator.next())>, bool>;
    std::same_as<typename task::AwaitableReturnType<decltype(iterator.key())>, typename IteratorType::Key>;
    std::same_as<typename task::AwaitableReturnType<decltype(iterator.value())>, typename IteratorType::Value>;
};

template <class IteratorType>
concept ReadIterator = requires(IteratorType iterator)
{
    SeekIterator<IteratorType>;
    std::convertible_to<task::AwaitableReturnType<decltype(iterator.hasValue())>, bool>;
};

template <class StorageType, class KeyType>
concept ReadableStorage = requires(StorageType&& impl, KeyType&& key)
{
    ReadIterator<task::AwaitableReturnType<decltype(impl.read(RANGES::any_view<KeyType>()))>>;
};

template <class StorageType, class KeyType, class ValueType>
concept WriteableStorage = requires(StorageType&& impl, KeyType&& key)
{
    task::IsAwaitable<decltype(impl.write(RANGES::any_view<KeyType>(), RANGES::any_view<ValueType>()))>;
    task::IsAwaitable<decltype(impl.remove(RANGES::any_view<KeyType>()))>;
};

template <class StorageType, class KeyType>
concept SeekableStorage = requires(StorageType&& impl, KeyType&& key)
{
    SeekIterator<task::AwaitableReturnType<decltype(impl.seek(key))>>;
};

auto single(auto&& value)
{
    return RANGES::single_view(std::ref(value)) |
           RANGES::views::transform([](auto&& input) -> auto& { return input.get(); });
}


}  // namespace bcos::storage2