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
concept ReadIterator = requires(IteratorType&& iterator)
{
    requires std::convertible_to < task::AwaitableReturnType<decltype(iterator.next())>,
    bool > ;
    requires std::same_as < typename task::AwaitableReturnType<decltype(iterator.key())>,
    typename IteratorType::Key > ;
    requires std::same_as < typename task::AwaitableReturnType<decltype(iterator.value())>,
    typename IteratorType::Value > ;
    requires std::convertible_to < task::AwaitableReturnType<decltype(iterator.hasValue())>,
    bool > ;
};

template <class StorageType>
concept ReadableStorage = requires(StorageType&& impl)
{
    typename StorageType::Key;
    requires ReadIterator<task::AwaitableReturnType<decltype(impl.read(
        RANGES::any_view<typename StorageType::Key>()))>>;
};

template <class StorageType>
concept WriteableStorage = requires(StorageType&& impl)
{
    typename StorageType::Key;
    typename StorageType::Value;
    requires task::IsAwaitable<decltype(impl.write(RANGES::any_view<typename StorageType::Key>(),
        RANGES::any_view<typename StorageType::Value>()))>;
};

template <class StorageType>
concept SeekableStorage = requires(StorageType&& impl)
{
    typename StorageType::Key;
    requires ReadIterator<
        task::AwaitableReturnType<decltype(impl.seek(std::declval<StorageType::Key>()))>>;
};

template <class StorageType>
concept ErasableStorage = requires(StorageType&& impl)
{
    typename StorageType::Key;
    requires task::IsAwaitable<decltype(impl.remove(
        RANGES::any_view<typename StorageType::Key>()))>;
};

template <storage2::ReadableStorage Storage>
struct ReadIteratorTrait
{
    using type = typename task::AwaitableReturnType<decltype(std::declval<Storage>().read(
        RANGES::any_view<typename Storage::Key>()))>;
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

auto single(auto&& value)
{
    return RANGES::single_view(std::ref(value)) |
           RANGES::views::transform([](auto&& input) -> auto& { return input.get(); });
}

task::Task<bool> existsOne(ReadableStorage auto& storage, auto const& key)
{
    auto it = co_await storage.read(storage2::single(key));
    co_await it.next();
    co_return co_await it.hasValue();
}

template <class Reference>
using ValueOrReferenceWrapper = std::conditional_t<std::is_reference_v<Reference>,
    std::reference_wrapper<std::remove_reference_t<Reference>>, Reference>;

auto readOne(ReadableStorage auto& storage, auto const& key)
    -> task::Task<std::optional<ValueOrReferenceWrapper<
        typename task::AwaitableReturnType<decltype(storage.read(storage2::single(key)))>::Value>>>
{
    using ValueType = ValueOrReferenceWrapper<
        typename task::AwaitableReturnType<decltype(storage.read(storage2::single(key)))>::Value>;

    auto keys = storage2::single(key);
    auto it = co_await storage.read(keys);
    co_await it.next();
    std::optional<ValueType> result;
    if (co_await it.hasValue())
    {
        result.emplace(std::forward<ValueType>(co_await it.value()));
    }

    co_return result;
}

task::Task<void> writeOne(WriteableStorage auto& storage, auto const& key, auto&& value)
{
    co_await storage.write(
        storage2::single(key), storage2::single(std::forward<decltype(value)>(value)));
    co_return;
}

task::Task<void> removeOne(ErasableStorage auto& storage, auto const& key)
{
    co_await storage.remove(storage2::single(key));
    co_return;
}

}  // namespace bcos::storage2