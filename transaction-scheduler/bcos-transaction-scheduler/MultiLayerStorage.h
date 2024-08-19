#pragma once
#include "bcos-framework/storage2/Storage.h"
#include "bcos-task/TBBWait.h"
#include "bcos-task/Trait.h"
#include "bcos-utilities/Error.h"
#include "bcos-utilities/RecursiveLambda.h"
#include <oneapi/tbb/parallel_invoke.h>
#include <boost/throw_exception.hpp>
#include <functional>
#include <type_traits>
#include <variant>

namespace bcos::transaction_scheduler
{

// clang-format off
struct DuplicateMutableStorageError : public bcos::Error {};
struct DuplicateMutableViewError: public bcos::Error {};
struct NonExistsKeyIteratorError: public bcos::Error {};
struct NotExistsMutableStorageError : public bcos::Error {};
struct NotExistsImmutableStorageError : public bcos::Error {};
struct UnsupportedMethod : public bcos::Error {};
// clang-format on

template <class KeyType, class ValueType>
task::Task<bool> fillMissingValues(
    auto& storage, RANGES::input_range auto&& keys, RANGES::input_range auto& values)
{
    using StoreKeyType =
        std::conditional_t<std::is_lvalue_reference_v<RANGES::range_value_t<decltype(keys)>>,
            std::reference_wrapper<KeyType>, KeyType>;

    std::vector<std::pair<StoreKeyType, std::reference_wrapper<std::optional<ValueType>>>>
        missingKeyValues;
    for (auto&& [key, value] : RANGES::views::zip(std::forward<decltype(keys)>(keys), values))
    {
        if (!value)
        {
            missingKeyValues.emplace_back(std::forward<decltype(key)>(key), std::ref(value));
        }
    }
    auto gotValues = co_await storage2::readSome(storage, missingKeyValues | RANGES::views::keys);

    size_t count = 0;
    for (auto&& [from, to] :
        RANGES::views::zip(gotValues, missingKeyValues | RANGES::views::values))
    {
        if (from)
        {
            to.get() = std::move(from);
            ++count;
        }
    }

    co_return count == RANGES::size(gotValues);
}

template <class MutableStorageType, class CachedStorage, class BackendStorageType>
    requires((std::is_void_v<CachedStorage> || (!std::is_void_v<CachedStorage>)))
class View
{
public:
    constexpr static bool isView = true;

    constexpr static bool withCacheStorage = !std::is_void_v<CachedStorage>;
    using Key = std::remove_cvref_t<typename MutableStorageType::Key>;
    using Value = std::remove_cvref_t<typename MutableStorageType::Value>;
    using MutableStorage = MutableStorageType;
    using BackendStorage = BackendStorageType;

    std::shared_ptr<MutableStorageType> m_mutableStorage;
    std::deque<std::shared_ptr<MutableStorageType>> m_immutableStorages;
    std::reference_wrapper<std::remove_reference_t<BackendStorage>> m_backendStorage;
    [[no_unique_address]] std::conditional_t<withCacheStorage,
        std::reference_wrapper<std::remove_reference_t<CachedStorage>>, std::monostate>
        m_cacheStorage;

    View(BackendStorage& backendStorage)
        requires(!withCacheStorage)
      : m_backendStorage(backendStorage)
    {}
    View(BackendStorage& backendStorage,
        std::conditional_t<withCacheStorage, std::add_lvalue_reference_t<CachedStorage>,
            std::monostate>
            cacheStorage)
        requires(withCacheStorage)
      : m_backendStorage(backendStorage), m_cacheStorage(cacheStorage)
    {}

    View(const View&) = delete;
    View& operator=(const View&) = delete;
    View(View&&) noexcept = default;
    View& operator=(View&&) noexcept = default;
    ~View() noexcept = default;
};

template <class Storage>
concept IsView = std::remove_cvref_t<Storage>::isView;

template <IsView View>
typename View::MutableStorage& mutableStorage(View& storage)
{
    if (!storage.m_mutableStorage)
    {
        BOOST_THROW_EXCEPTION(NotExistsMutableStorageError{});
    }
    return *storage.m_mutableStorage;
}

template <IsView View>
auto tag_invoke(
    storage2::tag_t<storage2::readSome> /*unused*/, View& view, RANGES::input_range auto&& keys)
    -> task::Task<task::AwaitableReturnType<
        std::invoke_result_t<storage2::ReadSome, typename View::MutableStorage&, decltype(keys)>>>
    requires RANGES::sized_range<decltype(keys)> &&
             RANGES::sized_range<task::AwaitableReturnType<std::invoke_result_t<storage2::ReadSome,
                 typename View::MutableStorage&, decltype(keys)>>>
{
    task::AwaitableReturnType<decltype(storage2::readSome(*view.m_mutableStorage, keys))> values(
        RANGES::size(keys));
    if (view.m_mutableStorage &&
        co_await fillMissingValues<typename View::Key, typename View::Value>(
            *view.m_mutableStorage, keys, values))
    {
        co_return values;
    }
    else
    {
        values.resize(RANGES::size(keys));
    }

    for (auto& immutableStorage : view.m_immutableStorages)
    {
        if (co_await fillMissingValues<typename View::Key, typename View::Value>(
                *immutableStorage, keys, values))
        {
            co_return values;
        }
    }

    if constexpr (View::withCacheStorage)
    {
        if (co_await fillMissingValues<typename View::Key, typename View::Value>(
                view.m_cacheStorage.get(), keys, values))
        {
            co_return values;
        }
    }

    co_await fillMissingValues<typename View::Key, typename View::Value>(
        view.m_backendStorage.get(), keys, values);
    co_return values;
}

template <IsView View>
auto tag_invoke(storage2::tag_t<storage2::readSome> /*unused*/, View& view,
    RANGES::input_range auto&& keys, storage2::DIRECT_TYPE /*unused*/)
    -> task::Task<task::AwaitableReturnType<
        std::invoke_result_t<storage2::ReadSome, typename View::MutableStorage&, decltype(keys)>>>
{
    if (view.m_mutableStorage)
    {
        co_return co_await storage2::readSome(
            *view.m_mutableStorage, std::forward<decltype(keys)>(keys));
    }

    for (auto& immutableStorage : view.m_immutableStorages)
    {
        co_return co_await storage2::readSome(
            *immutableStorage, std::forward<decltype(keys)>(keys));
    }

    if constexpr (View::withCacheStorage)
    {
        co_return co_await storage2::readSome(
            view.m_cacheStorage.get(), std::forward<decltype(keys)>(keys));
    }

    co_return co_await storage2::readSome(
        view.m_backendStorage.get(), std::forward<decltype(keys)>(keys));
}

template <IsView View>
auto tag_invoke(storage2::tag_t<storage2::readOne> /*unused*/, View& view, auto&& key)
    -> task::Task<task::AwaitableReturnType<
        std::invoke_result_t<storage2::ReadOne, typename View::MutableStorage&, decltype(key)>>>
{
    if (view.m_mutableStorage)
    {
        if (auto value = co_await storage2::readOne(*view.m_mutableStorage, key))
        {
            co_return value;
        }
    }

    for (auto& immutableStorage : view.m_immutableStorages)
    {
        if (auto value = co_await storage2::readOne(*immutableStorage, key))
        {
            co_return value;
        }
    }

    if constexpr (View::withCacheStorage)
    {
        if (auto value = co_await storage2::readOne(view.m_cacheStorage.get(), key))
        {
            co_return value;
        }
    }

    co_return co_await storage2::readOne(view.m_backendStorage.get(), key);
}

template <IsView View>
auto tag_invoke(storage2::tag_t<storage2::readOne> /*unused*/, View& view, auto&& key,
    storage2::DIRECT_TYPE /*unused*/)
    -> task::Task<task::AwaitableReturnType<
        std::invoke_result_t<storage2::ReadOne, typename View::MutableStorage&, decltype(key)>>>
{
    if (view.m_mutableStorage)
    {
        co_return co_await storage2::readOne(
            *view.m_mutableStorage, std::forward<decltype(key)>(key));
    }

    for (auto& immutableStorage : view.m_immutableStorages)
    {
        co_return co_await storage2::readOne(*immutableStorage, std::forward<decltype(key)>(key));
    }

    if constexpr (View::withCacheStorage)
    {
        co_return co_await storage2::readOne(
            view.m_cacheStorage.get(), std::forward<decltype(key)>(key));
    }

    co_return co_await storage2::readOne(
        view.m_backendStorage.get(), std::forward<decltype(key)>(key));
}

template <IsView View>
task::Task<void> tag_invoke(storage2::tag_t<storage2::writeSome> /*unused*/, View& view,
    RANGES::input_range auto&& keys, RANGES::input_range auto&& values)
{
    co_await storage2::writeSome(mutableStorage(view), std::forward<decltype(keys)>(keys),
        std::forward<decltype(values)>(values));
}

template <IsView View>
auto tag_invoke(storage2::tag_t<storage2::writeOne> /*unused*/, View& view, auto&& key,
    auto&& value) -> task::Task<void>
{
    co_await storage2::writeOne(mutableStorage(view), std::forward<decltype(key)>(key),
        std::forward<decltype(value)>(value));
}

template <IsView View>
task::Task<void> tag_invoke(
    storage2::tag_t<storage2::merge> /*unused*/, View& toView, auto&& fromStorage)
{
    co_await storage2::merge(
        mutableStorage(toView), std::forward<decltype(fromStorage)>(fromStorage));
}

template <IsView View>
task::Task<void> tag_invoke(storage2::tag_t<storage2::removeSome> /*unused*/, View& view,
    RANGES::input_range auto&& keys, auto&&... args)
{
    co_await storage2::removeSome(mutableStorage(view), std::forward<decltype(keys)>(keys),
        std::forward<decltype(args)>(args)...);
}

template <IsView View>
class Iterator
{
private:
    using StorageIterator =
        std::variant<task::AwaitableReturnType<std::invoke_result_t<storage2::Range,
                         std::add_lvalue_reference_t<typename View::MutableStorage>>>,
            task::AwaitableReturnType<std::invoke_result_t<storage2::Range,
                std::add_lvalue_reference_t<typename View::BackendStorage>>>>;
    using RangeValue = std::optional<
        std::tuple<typename View::MutableStorage::Key, typename View::MutableStorage::Value>>;
    std::vector<std::tuple<StorageIterator, RangeValue>> m_iterators;

    task::Task<void> forwardIterators(auto&& iterators)
    {
        for (auto& it : iterators)
        {
            auto& [variantIterator, item] = it;
            item = co_await std::visit(
                bcos::recursiveLambda([&](auto const& self, auto& input) -> task::Task<RangeValue> {
                    RangeValue item;
                    auto rangeValue = co_await input.next();
                    if (rangeValue)
                    {
                        auto&& [key, value] = *rangeValue;
                        if constexpr (std::is_pointer_v<std::decay_t<decltype(value)>>)
                        {
                            if (!value)
                            {
                                co_return co_await self(self, input);
                            }
                            item.emplace(key, *value);
                        }
                        else
                        {
                            item = std::move(rangeValue);
                        }
                    }
                    co_return item;
                }),
                variantIterator);
        }
    }

public:
    task::Task<void> init(View& view, auto&&... args)
    {
        if (view.m_mutableStorage)
        {
            m_iterators.emplace_back(co_await storage2::range(*view.m_mutableStorage,
                                         std::forward<decltype(args)>(args)...),
                RangeValue{});
        }
        for (auto& storage : view.m_immutableStorages)
        {
            m_iterators.emplace_back(
                co_await storage2::range(*storage, std::forward<decltype(args)>(args)...),
                RangeValue{});
        }
        m_iterators.emplace_back(co_await storage2::range(view.m_backendStorage.get(),
                                     std::forward<decltype(args)>(args)...),
            RangeValue{});
        co_await forwardIterators(m_iterators);
    }

    task::Task<RangeValue> next()
    {
        // 基于合并排序，找到所有迭代器的最小值，推进迭代器并返回值
        // Based on merge sort, find the minimum value of all iterators, advance the
        // iterator and return its value
        auto iterators = m_iterators | RANGES::views::filter([](auto const& rangeValue) {
            return std::get<1>(rangeValue).has_value();
        });
        if (RANGES::empty(iterators))
        {
            co_return std::nullopt;
        }

        std::vector<std::tuple<StorageIterator, RangeValue>*> minIterators;
        for (auto& it : iterators)
        {
            if (minIterators.empty())
            {
                minIterators.emplace_back(std::addressof(it));
            }
            else
            {
                auto& [variantIterator, value] = it;
                auto& key = std::get<0>(*value);
                auto& existsKey = std::get<0>(*std::get<1>(*minIterators[0]));

                if (key < existsKey)
                {
                    minIterators.clear();
                    minIterators.emplace_back(std::addressof(it));
                }
                else if (key == existsKey)
                {
                    minIterators.emplace_back(std::addressof(it));
                }
            }
        }

        RangeValue result = std::get<1>(*minIterators[0]);
        co_await forwardIterators(
            minIterators |
            RANGES::views::transform([](auto* iterator) -> auto& { return *iterator; }));
        co_return result;
    };
};

template <IsView View>
task::Task<Iterator<View>> tag_invoke(
    bcos::storage2::tag_t<storage2::range> /*unused*/, View& view, auto&&... args)
{
    Iterator<View> iterator;
    co_await iterator.init(view, std::forward<decltype(args)>(args)...);
    co_return iterator;
}

template <IsView View, class... Args>
void newMutable(View& storage, Args&&... args)
{
    if (storage.m_mutableStorage)
    {
        BOOST_THROW_EXCEPTION(DuplicateMutableStorageError{});
    }

    storage.m_mutableStorage =
        std::make_shared<typename View::MutableStorage>(std::forward<decltype(args)>(args)...);
}

template <IsView View>
typename View::BackendStorage& backendStorage(View& storage)
{
    return storage.m_backendStorage;
}

template <class MutableStorageType, class CachedStorage, class BackendStorage>
    requires((std::is_void_v<CachedStorage> || (!std::is_void_v<CachedStorage>)))
class MultiLayerStorage
{
public:
    constexpr static bool isMultiLayerStorage = true;
    constexpr static bool withCacheStorage = !std::is_void_v<CachedStorage>;
    using KeyType = std::remove_cvref_t<typename MutableStorageType::Key>;
    using ValueType = std::remove_cvref_t<typename MutableStorageType::Value>;
    using ViewType = View<MutableStorageType, CachedStorage, BackendStorage>;

    std::deque<std::shared_ptr<MutableStorageType>> m_storages;
    std::mutex m_listMutex;
    std::mutex m_mergeMutex;

    std::reference_wrapper<std::remove_reference_t<BackendStorage>> m_backendStorage;
    [[no_unique_address]] std::conditional_t<withCacheStorage,
        std::reference_wrapper<std::remove_reference_t<CachedStorage>>, std::monostate>
        m_cacheStorage;

    using MutableStorage = MutableStorageType;

    using Key = KeyType;
    using Value = ValueType;

    explicit MultiLayerStorage(BackendStorage& backendStorage) noexcept
        requires(!withCacheStorage)
      : m_backendStorage(backendStorage)
    {
        static_assert(std::same_as<typename MutableStorageType::Key, typename BackendStorage::Key>);
        static_assert(
            std::same_as<typename MutableStorageType::Value, typename BackendStorage::Value>);
    }

    MultiLayerStorage(BackendStorage& backendStorage,
        std::conditional_t<withCacheStorage, std::add_lvalue_reference_t<CachedStorage>,
            std::monostate>
            cacheStorage) noexcept
        requires(withCacheStorage)
      : m_backendStorage(backendStorage), m_cacheStorage(cacheStorage)
    {
        static_assert(std::same_as<typename MutableStorageType::Key, typename CachedStorage::Key>);
        static_assert(
            std::same_as<typename MutableStorageType::Value, typename CachedStorage::Value>);
    }
    MultiLayerStorage(const MultiLayerStorage&) = delete;
    MultiLayerStorage(MultiLayerStorage&&) noexcept = default;
    MultiLayerStorage& operator=(const MultiLayerStorage&) = delete;
    MultiLayerStorage& operator=(MultiLayerStorage&&) noexcept = default;
    ~MultiLayerStorage() noexcept = default;
};

template <class Storage>
concept IsMultiLayerStorage = std::remove_cvref_t<Storage>::isMultiLayerStorage;

template <IsMultiLayerStorage MultiLayerStorage>
typename MultiLayerStorage::ViewType fork(MultiLayerStorage& storage)
{
    std::unique_lock lock(storage.m_listMutex);
    if constexpr (MultiLayerStorage::withCacheStorage)
    {
        typename MultiLayerStorage::ViewType view(storage.m_backendStorage, storage.m_cacheStorage);
        view.m_immutableStorages = storage.m_storages;
        return view;
    }
    else
    {
        typename MultiLayerStorage::ViewType view(storage.m_backendStorage);
        view.m_immutableStorages = storage.m_storages;
        return view;
    }
}

template <IsMultiLayerStorage MultiLayerStorage>
void pushView(MultiLayerStorage& storage, typename MultiLayerStorage::ViewType&& view)
{
    if (!view.m_mutableStorage)
    {
        return;
    }
    std::unique_lock lock(storage.m_listMutex);
    storage.m_storages.push_front(std::move(view.m_mutableStorage));
}

template <IsMultiLayerStorage MultiLayerStorage>
task::Task<std::shared_ptr<typename MultiLayerStorage::MutableStorage>> mergeBackStorage(
    MultiLayerStorage& storage)
{
    std::unique_lock mergeLock(storage.m_mergeMutex);
    std::unique_lock listLock(storage.m_listMutex);
    if (storage.m_storages.empty())
    {
        BOOST_THROW_EXCEPTION(NotExistsImmutableStorageError{});
    }
    auto backStoragePtr = storage.m_storages.back();
    auto const& backStorage = *backStoragePtr;
    listLock.unlock();

    if constexpr (MultiLayerStorage::withCacheStorage)
    {
        tbb::parallel_invoke(
            [&]() {
                task::tbb::syncWait(storage2::merge(storage.m_backendStorage.get(), backStorage));
            },
            [&]() {
                task::tbb::syncWait(storage2::merge(storage.m_cacheStorage.get(), backStorage));
            });
    }
    else
    {
        co_await storage2::merge(storage.m_backendStorage.get(), backStorage);
    }

    listLock.lock();
    storage.m_storages.pop_back();

    co_return backStoragePtr;
}

template <IsMultiLayerStorage MultiLayerStorage>
std::shared_ptr<typename MultiLayerStorage::MutableStorage> frontStorage(MultiLayerStorage& storage)
{
    std::unique_lock immutablesLock(storage.m_listMutex);
    if (storage.m_storages.empty())
    {
        BOOST_THROW_EXCEPTION(NotExistsImmutableStorageError{});
    }

    return storage.m_storages.front();
}

template <IsMultiLayerStorage MultiLayerStorage>
std::shared_ptr<typename MultiLayerStorage::MutableStorage> backStorage(MultiLayerStorage& storage)
{
    std::unique_lock immutablesLock(storage.m_listMutex);
    if (storage.m_storages.empty())
    {
        BOOST_THROW_EXCEPTION(NotExistsImmutableStorageError{});
    }

    return storage.m_storages.back();
}

template <IsMultiLayerStorage MultiLayerStorage>
typename MultiLayerStorage::BackendStorage& backendStorage(MultiLayerStorage& storage)
{
    return storage.m_backendStorage;
}

}  // namespace bcos::transaction_scheduler
