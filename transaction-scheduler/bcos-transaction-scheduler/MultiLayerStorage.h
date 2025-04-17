#pragma once
#include "bcos-framework/storage2/Storage.h"
#include "bcos-task/TBBWait.h"
#include "bcos-task/Trait.h"
#include "bcos-utilities/Exceptions.h"
#include "bcos-utilities/ITTAPI.h"
#include "bcos-utilities/RecursiveLambda.h"
#include <oneapi/tbb/parallel_invoke.h>
#include <boost/throw_exception.hpp>
#include <functional>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/map.hpp>
#include <range/v3/view/zip.hpp>
#include <type_traits>
#include <variant>

namespace bcos::scheduler_v1
{

DERIVE_BCOS_EXCEPTION(DuplicateMutableStorageError);
DERIVE_BCOS_EXCEPTION(DuplicateMutableViewError);
DERIVE_BCOS_EXCEPTION(NonExistsKeyIteratorError);
DERIVE_BCOS_EXCEPTION(NotExistsMutableStorageError);
DERIVE_BCOS_EXCEPTION(NotExistsImmutableStorageError);
DERIVE_BCOS_EXCEPTION(UnsupportedMethod);

template <class KeyType, class ValueType>
task::Task<bool> fillMissingValues(
    auto& storage, ::ranges::input_range auto& keys, ::ranges::input_range auto& values)
{
    using StoreKeyType =
        std::conditional_t<std::is_lvalue_reference_v<::ranges::range_value_t<decltype(keys)>>,
            std::reference_wrapper<KeyType>, KeyType>;

    std::vector<std::pair<StoreKeyType, std::reference_wrapper<std::optional<ValueType>>>>
        missingKeyValues;
    for (auto&& [key, value] : ::ranges::views::zip(std::forward<decltype(keys)>(keys), values))
    {
        if (!value)
        {
            missingKeyValues.emplace_back(std::forward<decltype(key)>(key), std::ref(value));
        }
    }
    auto gotValues = co_await storage2::readSome(storage, ::ranges::views::keys(missingKeyValues));

    size_t count = 0;
    for (auto&& [from, to] :
        ::ranges::views::zip(gotValues, ::ranges::views::values(missingKeyValues)))
    {
        if (from)
        {
            to.get() = std::move(from);
            ++count;
        }
    }

    co_return count == ::ranges::size(gotValues);
}

template <class MutableStorageType, class CachedStorage, class BackendStorageType>
    requires((std::is_void_v<CachedStorage> || (!std::is_void_v<CachedStorage>)))
class View
{
public:
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

    friend MutableStorage& mutableStorage(View& storage)
    {
        if (!storage.m_mutableStorage)
        {
            BOOST_THROW_EXCEPTION(NotExistsMutableStorageError{});
        }
        return *storage.m_mutableStorage;
    }

    template <::ranges::input_range Keys>
    friend auto tag_invoke(storage2::tag_t<storage2::readSome> /*unused*/, View& view, Keys keys)
        -> task::Task<task::AwaitableReturnType<
            std::invoke_result_t<storage2::ReadSome, MutableStorage&, decltype(keys)>>>
        requires ::ranges::sized_range<Keys> &&
                 ::ranges::sized_range<task::AwaitableReturnType<
                     std::invoke_result_t<storage2::ReadSome, MutableStorage&, Keys>>>
    {
        auto keySize = static_cast<size_t>(keys.size());
        task::AwaitableReturnType<
            std::invoke_result_t<storage2::ReadSome, MutableStorage&, decltype(keys)>>
            values(keySize);
        if (view.m_mutableStorage &&
            co_await fillMissingValues<typename View::Key, typename View::Value>(
                *view.m_mutableStorage, keys, values))
        {
            co_return values;
        }
        else
        {
            values.resize(keySize);
        }

        for (auto& immutableStorage : view.m_immutableStorages)
        {
            if (co_await fillMissingValues<typename View::Key, typename View::Value>(
                    *immutableStorage, keys, values))
            {
                co_return values;
            }
        }

        if constexpr (withCacheStorage)
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

    friend auto tag_invoke(storage2::tag_t<storage2::readSome> /*unused*/, View& view,
        ::ranges::input_range auto keys, storage2::DIRECT_TYPE /*unused*/)
        -> task::Task<task::AwaitableReturnType<
            std::invoke_result_t<storage2::ReadSome, MutableStorage&, decltype(keys)>>>
    {
        if (view.m_mutableStorage)
        {
            co_return co_await storage2::readSome(*view.m_mutableStorage, std::move(keys));
        }

        for (auto& immutableStorage : view.m_immutableStorages)
        {
            co_return co_await storage2::readSome(*immutableStorage, std::move(keys));
        }

        if constexpr (View::withCacheStorage)
        {
            co_return co_await storage2::readSome(view.m_cacheStorage.get(), std::move(keys));
        }

        co_return co_await storage2::readSome(view.m_backendStorage.get(), std::move(keys));
    }

    friend auto tag_invoke(storage2::tag_t<storage2::readOne> /*unused*/, View& view, auto key)
        -> task::Task<task::AwaitableReturnType<
            std::invoke_result_t<storage2::ReadOne, MutableStorage&, decltype(key)>>>
    {
        if (view.m_mutableStorage)
        {
            auto value = view.m_mutableStorage->readOne(key);
            if (auto* ptr = std::get_if<std::optional<typename View::Value>>(std::addressof(value)))
            {
                co_return *ptr;
            }
        }

        for (auto& immutableStorage : view.m_immutableStorages)
        {
            auto value = immutableStorage->readOne(key);
            if (auto* ptr = std::get_if<std::optional<typename View::Value>>(std::addressof(value)))
            {
                co_return *ptr;
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

    friend auto tag_invoke(storage2::tag_t<storage2::readOne> /*unused*/, View& view,
        const auto& key, storage2::DIRECT_TYPE /*unused*/)
        -> task::Task<task::AwaitableReturnType<
            std::invoke_result_t<storage2::ReadOne, MutableStorage&, decltype(key)>>>
    {
        if (view.m_mutableStorage)
        {
            co_return co_await storage2::readOne(*view.m_mutableStorage, key);
        }

        for (auto& immutableStorage : view.m_immutableStorages)
        {
            co_return co_await storage2::readOne(*immutableStorage, key);
        }

        if constexpr (View::withCacheStorage)
        {
            co_return co_await storage2::readOne(view.m_cacheStorage.get(), key);
        }

        co_return co_await storage2::readOne(view.m_backendStorage.get(), key);
    }

    friend task::Task<void> tag_invoke(storage2::tag_t<storage2::writeSome> /*unused*/, View& view,
        ::ranges::input_range auto keyValues)
    {
        co_await storage2::writeSome(mutableStorage(view), std::move(keyValues));
    }

    friend auto tag_invoke(storage2::tag_t<storage2::writeOne> /*unused*/, View& view, auto key,
        auto value) -> task::Task<void>
    {
        co_await storage2::writeOne(mutableStorage(view), std::move(key), std::move(value));
    }

    friend task::Task<void> tag_invoke(
        storage2::tag_t<storage2::merge> /*unused*/, View& toView, auto& fromStorage)
    {
        co_await storage2::merge(mutableStorage(toView), fromStorage);
    }

    friend task::Task<void> tag_invoke(storage2::tag_t<storage2::removeSome> /*unused*/, View& view,
        ::ranges::input_range auto keys, auto&&... args)
    {
        co_await storage2::removeSome(
            mutableStorage(view), std::move(keys), std::forward<decltype(args)>(args)...);
    }

    class Iterator
    {
    private:
        using StorageIterator =
            std::variant<task::AwaitableReturnType<std::invoke_result_t<storage2::Range,
                             std::add_lvalue_reference_t<MutableStorage>>>,
                task::AwaitableReturnType<std::invoke_result_t<storage2::Range,
                    std::add_lvalue_reference_t<BackendStorage>>>>;
        using RangeValue = std::optional<std::tuple<Key, Value>>;
        std::vector<std::tuple<StorageIterator, RangeValue>> m_iterators;

        task::Task<void> forwardIterators(::ranges::range auto&& iterators)
        {
            for (auto& it : iterators)
            {
                auto& [variantIterator, item] = it;
                item = co_await std::visit(
                    bcos::recursiveLambda(
                        [&](auto const& self, auto& input) -> task::Task<RangeValue> {
                            RangeValue item;
                            if (auto rangeValue = co_await input.next(); rangeValue)
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
            auto iterators = m_iterators | ::ranges::views::filter([](auto const& rangeValue) {
                return std::get<1>(rangeValue).has_value();
            });
            if (::ranges::empty(iterators))
            {
                co_return {};
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
            co_await forwardIterators(::ranges::views::indirect(minIterators));
            co_return result;
        };
    };

    void newMutable(auto&&... args)
    {
        if (m_mutableStorage)
        {
            BOOST_THROW_EXCEPTION(DuplicateMutableStorageError{});
        }

        m_mutableStorage = std::make_shared<MutableStorage>(std::forward<decltype(args)>(args)...);
    }

    friend BackendStorage& backendStorage(View& view) { return view.m_backendStorage; }

    friend task::Task<Iterator> tag_invoke(
        bcos::storage2::tag_t<storage2::range> /*unused*/, View& view, auto&&... args)
    {
        Iterator iterator;
        co_await iterator.init(view, std::forward<decltype(args)>(args)...);
        co_return iterator;
    }
};

template <class MutableStorageType, class CachedStorage, class BackendStorage>
    requires MutableStorageType::withLogicalDeletion
class MultiLayerStorage
{
public:
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

    friend ViewType fork(MultiLayerStorage& storage)
    {
        std::unique_lock lock(storage.m_listMutex);
        if constexpr (withCacheStorage)
        {
            ViewType view(storage.m_backendStorage, storage.m_cacheStorage);
            view.m_immutableStorages = storage.m_storages;
            return view;
        }
        else
        {
            ViewType view(storage.m_backendStorage);
            view.m_immutableStorages = storage.m_storages;
            return view;
        }
    }

    friend void pushView(MultiLayerStorage& storage, ViewType view)
    {
        if (!view.m_mutableStorage)
        {
            return;
        }
        std::unique_lock lock(storage.m_listMutex);
        storage.m_storages.push_front(std::move(view.m_mutableStorage));
    }

    task::Task<std::shared_ptr<MutableStorage>> mergeBackStorage()
    {
        std::unique_lock mergeLock(m_mergeMutex);
        std::unique_lock listLock(m_listMutex);
        if (m_storages.empty())
        {
            BOOST_THROW_EXCEPTION(NotExistsImmutableStorageError{});
        }
        auto backStoragePtr = m_storages.back();
        auto& backStorage = *backStoragePtr;
        listLock.unlock();

        if constexpr (withCacheStorage)
        {
            tbb::parallel_invoke(
                [&]() {
                    ittapi::Report report(ittapi::ITT_DOMAINS::instance().STORAGE2,
                        ittapi::ITT_DOMAINS::instance().MERGE_BACKEND);
                    task::tbb::syncWait(storage2::merge(m_backendStorage.get(), backStorage));
                },
                [&]() {
                    ittapi::Report report(ittapi::ITT_DOMAINS::instance().STORAGE2,
                        ittapi::ITT_DOMAINS::instance().MERGE_CACHE);
                    task::tbb::syncWait(storage2::merge(m_cacheStorage.get(), backStorage));
                });
        }
        else
        {
            ittapi::Report report(ittapi::ITT_DOMAINS::instance().STORAGE2,
                ittapi::ITT_DOMAINS::instance().MERGE_BACKEND);
            co_await storage2::merge(m_backendStorage.get(), backStorage);
        }

        listLock.lock();
        m_storages.pop_back();

        co_return backStoragePtr;
    }

    BackendStorage& backendStorage() { return m_backendStorage; }
};

}  // namespace bcos::scheduler_v1
