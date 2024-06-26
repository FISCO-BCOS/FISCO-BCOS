#pragma once
#include "bcos-framework/storage2/Storage.h"
#include "bcos-task/Trait.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/Error.h"
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

template <class MutableStorageType, class CachedStorage, class BackendStorage>
    requires((std::is_void_v<CachedStorage> || (!std::is_void_v<CachedStorage>)))
class MultiLayerStorage
{
private:
    constexpr static bool withCacheStorage = !std::is_void_v<CachedStorage>;
    using KeyType = std::remove_cvref_t<typename MutableStorageType::Key>;
    using ValueType = std::remove_cvref_t<typename MutableStorageType::Value>;
    static_assert(std::same_as<typename MutableStorageType::Key, typename BackendStorage::Key>);
    static_assert(std::same_as<typename MutableStorageType::Value, typename BackendStorage::Value>);

    std::shared_ptr<MutableStorageType> m_mutableStorage;
    std::deque<std::shared_ptr<MutableStorageType>> m_immutableStorages;
    std::mutex m_listMutex;
    std::mutex m_mergeMutex;

    std::reference_wrapper<std::remove_reference_t<BackendStorage>> m_backendStorage;
    [[no_unique_address]] std::conditional_t<withCacheStorage,
        std::reference_wrapper<std::remove_reference_t<CachedStorage>>, std::monostate>
        m_cacheStorage;

    // 同一时间只允许一个可以修改的view
    // Only one view that can be modified is allowed at a time
    std::mutex m_mutableMutex;

public:
    using MutableStorage = MutableStorageType;

    class View
    {
        friend class MultiLayerStorage;

    private:
        std::shared_ptr<MutableStorageType> m_mutableStorage;
        std::deque<std::shared_ptr<MutableStorageType>> m_immutableStorages;
        std::reference_wrapper<std::remove_reference_t<BackendStorage>> m_backendStorage;
        [[no_unique_address]] std::conditional_t<withCacheStorage,
            std::reference_wrapper<std::remove_reference_t<CachedStorage>>, std::monostate>
            m_cacheStorage;
        std::unique_lock<std::mutex> m_mutableLock;

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

        static task::Task<bool> fillMissingValues(
            auto& storage, RANGES::input_range auto&& keys, RANGES::input_range auto& values)
        {
            using StoreKeyType = std::conditional_t<
                std::is_lvalue_reference_v<RANGES::range_value_t<decltype(keys)>>,
                std::reference_wrapper<KeyType>, KeyType>;

            std::vector<std::pair<StoreKeyType, std::reference_wrapper<std::optional<ValueType>>>>
                missingKeyValues;
            for (auto&& [key, value] :
                RANGES::views::zip(std::forward<decltype(keys)>(keys), values))
            {
                if (!value)
                {
                    missingKeyValues.emplace_back(
                        std::forward<decltype(key)>(key), std::ref(value));
                }
            }
            auto gotValues =
                co_await storage2::readSome(storage, missingKeyValues | RANGES::views::keys);

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

    public:
        using MutableStorage = MutableStorageType;

        friend auto tag_invoke(storage2::tag_t<storage2::readSome> /*unused*/, View& storage,
            RANGES::input_range auto&& keys)
            -> task::Task<task::AwaitableReturnType<
                std::invoke_result_t<storage2::ReadSome, MutableStorageType&, decltype(keys)>>>
            requires RANGES::sized_range<decltype(keys)> &&
                     RANGES::sized_range<task::AwaitableReturnType<std::invoke_result_t<
                         storage2::ReadSome, MutableStorageType&, decltype(keys)>>>
        {
            task::AwaitableReturnType<decltype(storage2::readSome(*storage.m_mutableStorage, keys))>
                values(RANGES::size(keys));
            if (storage.m_mutableStorage &&
                co_await fillMissingValues(*storage.m_mutableStorage, keys, values))
            {
                co_return values;
            }
            else
            {
                values.resize(RANGES::size(keys));
            }

            for (auto& immutableStorage : storage.m_immutableStorages)
            {
                if (co_await fillMissingValues(*immutableStorage, keys, values))
                {
                    co_return values;
                }
            }

            if constexpr (withCacheStorage)
            {
                if (co_await fillMissingValues(storage.m_cacheStorage.get(), keys, values))
                {
                    co_return values;
                }
            }

            co_await fillMissingValues(storage.m_backendStorage.get(), keys, values);
            co_return values;
        }

        friend auto tag_invoke(storage2::tag_t<storage2::readSome> /*unused*/, View& storage,
            RANGES::input_range auto&& keys, storage2::DIRECT_TYPE /*unused*/)
            -> task::Task<task::AwaitableReturnType<
                std::invoke_result_t<storage2::ReadSome, MutableStorageType&, decltype(keys)>>>
        {
            if (storage.m_mutableStorage)
            {
                co_return co_await storage2::readSome(
                    *storage.m_mutableStorage, std::forward<decltype(keys)>(keys));
            }

            for (auto& immutableStorage : storage.m_immutableStorages)
            {
                co_return co_await storage2::readSome(
                    *immutableStorage, std::forward<decltype(keys)>(keys));
            }

            if constexpr (withCacheStorage)
            {
                co_return co_await storage2::readSome(
                    storage.m_cacheStorage.get(), std::forward<decltype(keys)>(keys));
            }

            co_return co_await storage2::readSome(
                storage.m_backendStorage.get(), std::forward<decltype(keys)>(keys));
        }

        friend auto tag_invoke(
            storage2::tag_t<storage2::readOne> /*unused*/, View& storage, auto&& key)
            -> task::Task<task::AwaitableReturnType<
                std::invoke_result_t<storage2::ReadOne, MutableStorageType&, decltype(key)>>>
        {
            if (storage.m_mutableStorage)
            {
                if (auto value = co_await storage2::readOne(*storage.m_mutableStorage, key))
                {
                    co_return value;
                }
            }

            for (auto& immutableStorage : storage.m_immutableStorages)
            {
                if (auto value = co_await storage2::readOne(*immutableStorage, key))
                {
                    co_return value;
                }
            }

            if constexpr (withCacheStorage)
            {
                if (auto value = co_await storage2::readOne(storage.m_cacheStorage.get(), key))
                {
                    co_return value;
                }
            }

            co_return co_await storage2::readOne(storage.m_backendStorage.get(), key);
        }

        friend auto tag_invoke(storage2::tag_t<storage2::readOne> /*unused*/, View& storage,
            auto&& key, storage2::DIRECT_TYPE /*unused*/)
            -> task::Task<task::AwaitableReturnType<
                std::invoke_result_t<storage2::ReadOne, MutableStorageType&, decltype(key)>>>
        {
            if (storage.m_mutableStorage)
            {
                co_return co_await storage2::readOne(
                    *storage.m_mutableStorage, std::forward<decltype(key)>(key));
            }

            for (auto& immutableStorage : storage.m_immutableStorages)
            {
                co_return co_await storage2::readOne(
                    *immutableStorage, std::forward<decltype(key)>(key));
            }

            if constexpr (withCacheStorage)
            {
                co_return co_await storage2::readOne(
                    storage.m_cacheStorage.get(), std::forward<decltype(key)>(key));
            }

            co_return co_await storage2::readOne(
                storage.m_backendStorage.get(), std::forward<decltype(key)>(key));
        }

        friend task::Task<void> tag_invoke(storage2::tag_t<storage2::writeSome> /*unused*/,
            View& storage, RANGES::input_range auto&& keys, RANGES::input_range auto&& values)
        {
            co_await storage2::writeSome(storage.mutableStorage(),
                std::forward<decltype(keys)>(keys), std::forward<decltype(values)>(values));
        }

        friend auto tag_invoke(storage2::tag_t<storage2::writeOne> /*unused*/, View& storage,
            auto&& key, auto&& value) -> task::Task<void>
        {
            co_await storage2::writeOne(storage.mutableStorage(), std::forward<decltype(key)>(key),
                std::forward<decltype(value)>(value));
        }

        friend task::Task<void> tag_invoke(
            storage2::tag_t<storage2::merge> /*unused*/, View& toStorage, auto&& fromStorage)
        {
            co_await storage2::merge(
                toStorage.mutableStorage(), std::forward<decltype(fromStorage)>(fromStorage));
        }

        friend task::Task<void> tag_invoke(storage2::tag_t<storage2::removeSome> /*unused*/,
            View& storage, RANGES::input_range auto&& keys, auto&&... args)
        {
            co_await storage2::removeSome(storage.mutableStorage(),
                std::forward<decltype(keys)>(keys), std::forward<decltype(args)>(args)...);
        }

        class Iterator
        {
        private:
            using StorageIterator =
                std::variant<task::AwaitableReturnType<std::invoke_result_t<storage2::Range,
                                 std::add_lvalue_reference_t<MutableStorage>>>,
                    task::AwaitableReturnType<std::invoke_result_t<storage2::Range,
                        std::add_lvalue_reference_t<BackendStorage>>>>;
            using RangeValue = std::optional<
                std::tuple<typename MutableStorage::Key, typename MutableStorage::Value>>;
            std::vector<std::tuple<StorageIterator, RangeValue>> m_iterators;

            task::Task<void> forwardIterators(auto&& iterators)
            {
                for (auto& it : iterators)
                {
                    auto& [variantIterator, item] = it;
                    item = co_await std::visit(
                        [](auto& input) -> task::Task<RangeValue> {
                            RangeValue item;
                            auto rangeValue = co_await input.next();
                            if (rangeValue)
                            {
                                auto&& [key, value] = *rangeValue;
                                if constexpr (std::is_pointer_v<decltype(value)>)
                                {
                                    item.emplace(key, *value);
                                }
                                else
                                {
                                    item = std::move(rangeValue);
                                }
                            }
                            co_return item;
                        },
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

        friend task::Task<Iterator> tag_invoke(
            bcos::storage2::tag_t<storage2::range> /*unused*/, View& storage, auto&&... args)
        {
            Iterator iterator;
            co_await iterator.init(storage, std::forward<decltype(args)>(args)...);
            co_return iterator;
        }

        MutableStorageType& mutableStorage()
        {
            if (!m_mutableStorage)
            {
                BOOST_THROW_EXCEPTION(NotExistsMutableStorageError{});
            }
            return *m_mutableStorage;
        }

        View(const View&) = delete;
        View& operator=(const View&) = delete;
        View(View&&) noexcept = default;
        View& operator=(View&&) noexcept = default;
        ~View() noexcept { release(); }

        using Key = KeyType;
        using Value = ValueType;

        void release()
        {
            if (m_mutableLock.owns_lock())
            {
                m_mutableLock.unlock();
            }
        }

        template <class... Args>
        void newTemporaryMutable(Args... args)
        {
            if (m_mutableStorage)
            {
                BOOST_THROW_EXCEPTION(DuplicateMutableStorageError{});
            }

            m_mutableStorage = std::make_shared<MutableStorageType>(args...);
        }

        BackendStorage& backendStorage() & { return m_backendStorage; }
    };

    using Key = KeyType;
    using Value = ValueType;

    explicit MultiLayerStorage(BackendStorage& backendStorage)
        requires(!withCacheStorage)
      : m_backendStorage(backendStorage)
    {}

    MultiLayerStorage(BackendStorage& backendStorage,
        std::conditional_t<withCacheStorage, std::add_lvalue_reference_t<CachedStorage>,
            std::monostate>
            cacheStorage)
        requires(withCacheStorage)
      : m_backendStorage(backendStorage), m_cacheStorage(cacheStorage)
    {}
    MultiLayerStorage(const MultiLayerStorage&) = delete;
    MultiLayerStorage(MultiLayerStorage&&) noexcept = delete;
    MultiLayerStorage& operator=(const MultiLayerStorage&) = delete;
    MultiLayerStorage& operator=(MultiLayerStorage&&) noexcept = delete;
    ~MultiLayerStorage() noexcept = default;

    View fork(bool withMutable)
    {
        std::unique_lock lock(m_listMutex);
        if constexpr (withCacheStorage)
        {
            View view(m_backendStorage, m_cacheStorage);
            if (withMutable)
            {
                view.m_mutableLock = {m_mutableMutex, std::try_to_lock};
                if (!view.m_mutableLock.owns_lock())
                {
                    BOOST_THROW_EXCEPTION(DuplicateMutableViewError{});
                }
                view.m_mutableStorage = m_mutableStorage;
            }
            view.m_immutableStorages = m_immutableStorages;

            return view;
        }
        else
        {
            View view(m_backendStorage);
            if (withMutable)
            {
                view.m_mutableLock = {m_mutableMutex, std::try_to_lock};
                if (!view.m_mutableLock.owns_lock())
                {
                    BOOST_THROW_EXCEPTION(DuplicateMutableViewError{});
                }
                view.m_mutableStorage = m_mutableStorage;
            }
            view.m_immutableStorages = m_immutableStorages;

            return view;
        }
    }

    template <class... Args>
    void newMutable(Args... args)
    {
        std::unique_lock lock(m_listMutex);
        if (m_mutableStorage)
        {
            BOOST_THROW_EXCEPTION(DuplicateMutableStorageError{});
        }

        m_mutableStorage = std::make_shared<MutableStorageType>(args...);
    }

    void removeMutable()
    {
        std::unique_lock lock(m_listMutex);
        m_mutableStorage.reset();
    }

    void pushMutableToImmutableFront()
    {
        if (!m_mutableStorage)
        {
            BOOST_THROW_EXCEPTION(NotExistsMutableStorageError{});
        }
        std::unique_lock lock(m_listMutex);
        m_immutableStorages.push_front(std::move(m_mutableStorage));
        m_mutableStorage.reset();
    }

    task::Task<std::shared_ptr<MutableStorage>> mergeAndPopImmutableBack()
    {
        std::unique_lock mergeLock(m_mergeMutex);
        std::unique_lock immutablesLock(m_listMutex);
        if (m_immutableStorages.empty())
        {
            BOOST_THROW_EXCEPTION(NotExistsImmutableStorageError{});
        }
        auto immutableStorage = m_immutableStorages.back();
        immutablesLock.unlock();

        if constexpr (withCacheStorage)
        {
            tbb::parallel_invoke(
                [&]() {
                    task::syncWait(storage2::merge(m_backendStorage.get(), *immutableStorage));
                },
                [&]() {
                    task::syncWait(storage2::merge(m_cacheStorage.get(), *immutableStorage));
                });
        }
        else
        {
            co_await storage2::merge(m_backendStorage.get(), *immutableStorage);
        }

        immutablesLock.lock();
        m_immutableStorages.pop_back();

        co_return immutableStorage;
    }

    std::shared_ptr<MutableStorageType> frontImmutableStorage()
    {
        std::unique_lock immutablesLock(m_listMutex);
        if (m_immutableStorages.empty())
        {
            BOOST_THROW_EXCEPTION(NotExistsImmutableStorageError{});
        }

        return m_immutableStorages.front();
    }
    std::shared_ptr<MutableStorageType> lastImmutableStorage()
    {
        std::unique_lock immutablesLock(m_listMutex);
        if (m_immutableStorages.empty())
        {
            BOOST_THROW_EXCEPTION(NotExistsImmutableStorageError{});
        }

        return m_immutableStorages.back();
    }

    MutableStorageType& mutableStorage()
    {
        if (!m_mutableStorage)
        {
            BOOST_THROW_EXCEPTION(NotExistsMutableStorageError{});
        }
        return *m_mutableStorage;
    }
    BackendStorage& backendStorage() { return m_backendStorage; }
};

}  // namespace bcos::transaction_scheduler
