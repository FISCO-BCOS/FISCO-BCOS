#pragma once
#include "bcos-framework/storage2/Storage.h"
#include "bcos-task/Trait.h"
#include "bcos-task/Wait.h"
#include <bcos-concepts/Basic.h>
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-task/AwaitableValue.h>
#include <oneapi/tbb/parallel_invoke.h>
#include <oneapi/tbb/task_group.h>
#include <boost/container/small_vector.hpp>
#include <boost/throw_exception.hpp>
#include <iterator>
#include <stdexcept>
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
    requires((std::is_void_v<CachedStorage> || (!std::is_void_v<CachedStorage>)) &&
             storage2::SeekableStorage<MutableStorageType>)
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

    BackendStorage& m_backendStorage;
    [[no_unique_address]] std::conditional_t<withCacheStorage,
        std::add_lvalue_reference_t<CachedStorage>, std::monostate>
        m_cacheStorage;

    std::mutex m_mutableMutex;

public:
    using MutableStorage = MutableStorageType;

    class View
    {
        friend class MultiLayerStorage;

    private:
        std::shared_ptr<MutableStorageType> m_mutableStorage;
        std::deque<std::shared_ptr<MutableStorageType>> m_immutableStorages;
        BackendStorage& m_backendStorage;
        [[no_unique_address]] std::conditional_t<withCacheStorage,
            std::add_lvalue_reference_t<CachedStorage>, std::monostate>
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

        static auto readStorage(auto& storage,
            boost::container::small_vector<std::tuple<KeyType, std::optional<ValueType>>, 1>&
                keyValues) -> task::Task<bool>
        {
            auto keyIndexes =
                RANGES::views::enumerate(keyValues) | RANGES::views::filter([](auto&& tuple) {
                    return !std::get<1>(std::get<1>(tuple));
                }) |
                RANGES::views::transform([](auto&& tuple) -> auto{ return std::get<0>(tuple); }) |
                RANGES::to<boost::container::small_vector<size_t, 1>>();
            auto it = co_await storage.read(RANGES::views::transform(
                keyIndexes, [&](auto& index) -> auto& { return std::get<0>(keyValues[index]); }));

            bool finished = true;
            auto indexIt = RANGES::begin(keyIndexes);
            while (co_await it.next())
            {
                if (co_await it.hasValue())
                {
                    std::get<1>(keyValues[*indexIt]).emplace(co_await it.value());
                }
                else
                {
                    finished = false;
                }
                RANGES::advance(indexIt, 1);
            }
            co_return finished;
        }

    public:
        View(const View&) = delete;
        View& operator=(const View&) = delete;
        View(View&&) noexcept = default;
        View& operator=(View&&) noexcept = default;
        ~View() noexcept = default;

        void release() { m_mutableLock.unlock(); }

        class ReadIterator
        {
            friend class View;

        private:
            boost::container::small_vector<std::tuple<KeyType, std::optional<ValueType>>, 1>
                m_keyValues;
            int64_t m_index = -1;

        public:
            using Key = std::remove_cvref_t<typename MultiLayerStorage::KeyType> const&;
            using Value = std::remove_cvref_t<typename MultiLayerStorage::ValueType> const&;

            ReadIterator() = default;

            ReadIterator(const ReadIterator&) = delete;
            ReadIterator(ReadIterator&& rhs) noexcept = default;
            ReadIterator& operator=(const ReadIterator&) = delete;
            ReadIterator& operator=(ReadIterator&&) noexcept = default;
            ~ReadIterator() noexcept = default;

            task::AwaitableValue<bool> next()
            {
                return {static_cast<size_t>(++m_index) != m_keyValues.size()};
            }
            task::AwaitableValue<Key> key() const { return {std::get<0>(m_keyValues[m_index])}; }
            task::AwaitableValue<Value> value() const
            {
                return {*(std::get<1>(m_keyValues[m_index]))};
            }
            task::AwaitableValue<bool> hasValue() const
            {
                return {std::get<1>(m_keyValues[m_index]).has_value()};
            }
        };

        using Key = KeyType;
        using Value = ValueType;

        template <class... Args>
        void newTemporaryMutable(Args... args)
        {
            if (m_mutableStorage)
            {
                BOOST_THROW_EXCEPTION(DuplicateMutableStorageError{});
            }

            m_mutableStorage = std::make_shared<MutableStorageType>(args...);
        }

        task::Task<ReadIterator> read(RANGES::input_range auto&& keys)
        {
            ReadIterator iterator;
            iterator.m_keyValues = RANGES::views::transform(keys, [](auto&& key) {
                return std::tuple<KeyType, std::optional<ValueType>>(
                    std::forward<decltype(key)>(key), std::optional<ValueType>{});
            }) | RANGES::to<decltype(iterator.m_keyValues)>();

            if (m_mutableStorage)
            {
                if (co_await readStorage(*m_mutableStorage, iterator.m_keyValues))
                {
                    co_return iterator;
                }
            }

            if (!RANGES::empty(m_immutableStorages))
            {
                for (auto& immutableStorage : m_immutableStorages)
                {
                    if (co_await readStorage(*immutableStorage, iterator.m_keyValues))
                    {
                        co_return iterator;
                    }
                }
            }

            if constexpr (withCacheStorage)
            {
                if (co_await readStorage(m_cacheStorage, iterator.m_keyValues))
                {
                    co_return iterator;
                }
            }

            auto missingKeyIndexes =
                RANGES::views::enumerate(iterator.m_keyValues) |
                RANGES::views::filter(
                    [](auto&& tuple) { return !std::get<1>(std::get<1>(tuple)); }) |
                RANGES::views::transform([](auto&& tuple) -> auto{ return std::get<0>(tuple); }) |
                RANGES::to<boost::container::small_vector<size_t, 1>>();
            co_await readStorage(m_backendStorage, iterator.m_keyValues);
            // Write data into cache
            if constexpr (withCacheStorage)
            {
                for (auto index : missingKeyIndexes)
                {
                    if (std::get<1>(iterator.m_keyValues[index]))
                    {
                        co_await storage2::writeOne(m_cacheStorage,
                            std::get<0>(iterator.m_keyValues[index]),
                            *std::get<1>(iterator.m_keyValues[index]));
                    }
                }
            }

            co_return iterator;
        }

        task::Task<void> write(RANGES::input_range auto&& keys, RANGES::input_range auto&& values)
        {
            if (!m_mutableStorage) [[unlikely]]
            {
                BOOST_THROW_EXCEPTION(NotExistsMutableStorageError{});
            }

            co_await m_mutableStorage->write(
                std::forward<decltype(keys)>(keys), std::forward<decltype(values)>(values));
            co_return;
        }

        task::Task<void> remove(RANGES::input_range auto const& keys)
        {
            if (!m_mutableStorage)
            {
                BOOST_THROW_EXCEPTION(NotExistsMutableStorageError{});
            }

            co_await m_mutableStorage->remove(keys);
            co_return;
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

    task::Task<void> mergeAndPopImmutableBack()
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
                [&]() { task::syncWait(storage2::merge(*immutableStorage, m_backendStorage)); },
                [&]() { task::syncWait(storage2::merge(*immutableStorage, m_cacheStorage)); });
        }
        else
        {
            co_await storage2::merge(*immutableStorage, m_backendStorage);
        }

        immutablesLock.lock();
        m_immutableStorages.pop_back();
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