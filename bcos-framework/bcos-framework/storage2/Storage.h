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
    requires std::convertible_to<task::AwaitableReturnType<decltype(iterator.next())>, bool>;
    requires std::same_as<typename task::AwaitableReturnType<decltype(iterator.key())>,
        typename IteratorType::Key>;
    requires std::same_as<typename task::AwaitableReturnType<decltype(iterator.value())>,
        typename IteratorType::Value>;
};

template <class IteratorType>
concept ReadIterator = requires(IteratorType iterator)
{
    requires SeekIterator<IteratorType>;
    requires std::convertible_to<task::AwaitableReturnType<decltype(iterator.hasValue())>, bool>;
};

template <class StorageType, class KeyType, class ValueType>
concept Storage = requires(StorageType&& impl, KeyType&& key)
{
    requires ReadIterator<task::AwaitableReturnType<decltype(impl.read(RANGES::any_view<KeyType>()))>>;
    std::is_void_v<task::AwaitableReturnType<decltype(impl.write(
        RANGES::any_view<KeyType>(), RANGES::any_view<ValueType>()))>>;
    requires SeekIterator<task::AwaitableReturnType<decltype(impl.seek(key))>>;
    std::is_void_v<task::AwaitableReturnType<decltype(impl.remove(RANGES::any_view<KeyType>()))>>;
};

auto single(auto&& value)
{
    return RANGES::single_view(std::ref(value)) |
           RANGES::views::transform([](auto&& input) -> auto& { return input.get(); });
}


}  // namespace bcos::storage2