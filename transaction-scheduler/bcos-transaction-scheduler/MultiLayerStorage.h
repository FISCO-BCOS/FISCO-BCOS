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

template <transaction_executor::StateStorage MutableStorageType, class CachedStorage,
    transaction_executor::StateStorage BackendStorage>
    requires(
        (std::is_void_v<CachedStorage> || (transaction_executor::StateStorage<CachedStorage>)) &&
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

        template <RANGES::input_range Keys, RANGES::input_range Values>
        using ReadStorageReturnType = boost::container::small_vector<
            std::tuple<std::add_pointer_t<std::add_const_t<RANGES::range_value_t<Keys>>>,
                std::add_pointer_t<RANGES::range_value_t<Values>>>,
            1>;
        static auto readStorage(
            RANGES::input_range auto const& keys, RANGES::input_range auto& values, auto& storage)
            -> task::Task<ReadStorageReturnType<decltype(keys), decltype(values)>>
        {
            ReadStorageReturnType<decltype(keys), decltype(values)> missings;

            auto keyIt = RANGES::begin(keys);
            auto valueIt = RANGES::begin(values);
            auto it = co_await storage.read(keys);
            while (co_await it.next())
            {
                if (co_await it.hasValue())
                {
                    (*valueIt).emplace(co_await it.value());
                }
                else
                {
                    missings.emplace_back(
                        std::make_tuple(std::addressof(*keyIt), std::addressof(*valueIt)));
                }
                RANGES::advance(keyIt, 1);
                RANGES::advance(valueIt, 1);
            }
            co_return missings;
        }

    public:
        View(const View&) = delete;
        View& operator=(const View&) = delete;
        View(View&&) noexcept = default;
        View& operator=(View&&) noexcept = default;
        ~View() noexcept = default;

        class ReadIterator
        {
            friend class View;

        private:
            boost::container::small_vector<KeyType, 1> m_keys;
            boost::container::small_vector<std::optional<ValueType>, 1> m_values;
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
                return {static_cast<size_t>(++m_index) != m_values.size()};
            }
            task::AwaitableValue<Key> key() const { return {m_keys[m_index]}; }
            task::AwaitableValue<Value> value() const { return {*(m_values[m_index])}; }
            task::AwaitableValue<bool> hasValue() const { return {m_values[m_index].has_value()}; }
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

        task::Task<ReadIterator> read(RANGES::input_range auto const& keys)
        {
            ReadIterator iterator;
            iterator.m_keys = keys | RANGES::to<decltype(iterator.m_keys)>();
            iterator.m_values.resize(RANGES::size(iterator.m_keys));
            const auto& myKeys = iterator.m_keys;
            auto& myValues = iterator.m_values;

            ReadStorageReturnType<decltype(myKeys), decltype(myValues)> missing;

            bool started = false;
            if (m_mutableStorage)
            {
                started = true;
                missing = co_await readStorage(myKeys, myValues, *m_mutableStorage);
                if (RANGES::empty(missing))
                {
                    co_return iterator;
                }
            }

            if (!RANGES::empty(m_immutableStorages))
            {
                for (auto& immutableStorage : m_immutableStorages)
                {
                    if (!started)
                    {
                        started = true;
                        missing = co_await readStorage(myKeys, myValues, *immutableStorage);
                    }
                    else
                    {
                        auto keysView = missing | RANGES::views::transform([
                        ](auto& tuple) -> auto const& { return *std::get<0>(tuple); });
                        auto valuesView = missing | RANGES::views::transform([
                        ](auto& tuple) -> auto& { return *std::get<1>(tuple); });
                        missing = co_await readStorage(keysView, valuesView, *immutableStorage);
                    }

                    if (RANGES::empty(missing))
                    {
                        co_return iterator;
                    }
                }
            }

            if constexpr (withCacheStorage)
            {
                if (!started)
                {
                    missing = co_await readStorage(myKeys, myValues, m_cacheStorage);
                }
                else
                {
                    auto keysView = missing | RANGES::views::transform([
                    ](auto& tuple) -> auto const& { return *std::get<0>(tuple); });
                    auto valuesView = missing | RANGES::views::transform([
                    ](auto& tuple) -> auto& { return *std::get<1>(tuple); });
                    missing = co_await readStorage(keysView, valuesView, m_cacheStorage);
                }

                if (RANGES::empty(missing))
                {
                    co_return iterator;
                }
            }

            if (!started)
            {
                co_await readStorage(myKeys, myValues, m_backendStorage);
            }
            else
            {
                auto keysView = missing | RANGES::views::transform([
                ](auto& tuple) -> auto const& { return *std::get<0>(tuple); });
                auto valuesView = missing | RANGES::views::transform([
                ](auto& tuple) -> auto& { return *std::get<1>(tuple); });
                co_await readStorage(keysView, valuesView, m_backendStorage);

                // Write data into cache
                if constexpr (withCacheStorage)
                {
                    for (auto&& [key, value] : RANGES::views::zip(keysView, valuesView))
                    {
                        if (value)
                        {
                            co_await storage2::writeOne(m_cacheStorage, key, *value);
                        }
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