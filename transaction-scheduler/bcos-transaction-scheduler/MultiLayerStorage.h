#pragma once
#include "bcos-framework/storage2/Storage.h"
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-utilities/AnyHolder.h>
#include <boost/container/small_vector.hpp>
#include <boost/throw_exception.hpp>
#include <iterator>
#include <range/v3/range/access.hpp>
#include <range/v3/range_fwd.hpp>
#include <stdexcept>
#include <type_traits>
#include <variant>

namespace bcos::transaction_scheduler
{

// clang-format off
struct DuplicateMutableStorageError : public bcos::Error {};
struct NonExistsKeyIteratorError: public bcos::Error {};
struct NotExistsMutableStorageError : public bcos::Error {};
struct NotExistsImmutableStorageError : public bcos::Error {};
// clang-format on

template <transaction_executor::StateStorage MutableStorageType, class CachedStorage,
    transaction_executor::StateStorage BackendStorage>
requires((std::is_void_v<CachedStorage> || (transaction_executor::StateStorage<CachedStorage>)) &&
         storage2::SeekableStorage<MutableStorageType>) class MultiLayerStorage
{
public:
    constexpr static bool withCacheStorage = !std::is_void_v<CachedStorage>;
    using Key = std::conditional_t<withCacheStorage,
        std::common_type_t<typename MutableStorageType::Key, typename CachedStorage::Key,
            typename BackendStorage::Key>,
        std::common_type_t<typename MutableStorageType::Key, typename BackendStorage::Key>>;
    using Value = std::conditional_t<withCacheStorage,
        std::common_type_t<typename MutableStorageType::Value, typename CachedStorage::Value,
            typename BackendStorage::Value>,
        std::common_type_t<typename MutableStorageType::Value, typename BackendStorage::Value>>;

private:
    static_assert(std::same_as<typename MutableStorageType::Key, typename BackendStorage::Key>);
    static_assert(std::same_as<typename MutableStorageType::Value, typename BackendStorage::Value>);

    std::unique_ptr<MutableStorageType> m_mutableStorage;
    std::list<std::unique_ptr<MutableStorageType>> m_immutableStorages;  // Ledger read data from
                                                                         // here
    [[no_unique_address]] std::conditional_t<withCacheStorage,
        std::add_lvalue_reference_t<CachedStorage>, std::monostate>
        m_cacheStorage;
    BackendStorage& m_backendStorage;

    std::mutex m_immutablesMutex;
    std::mutex m_mergeMutex;

    task::Task<void> readBatch(auto& storage, RANGES::input_range auto const& inputKeys,
        RANGES::output_iterator<utilities::AnyHolder<Value>> auto& output,
        RANGES::output_iterator<std::tuple<RANGES::iterator_t<decltype(inputKeys)>,
            RANGES::iterator_t<decltype(output)>>> auto& missing)
    {
        auto it = co_await storage->read(inputKeys);
        auto keyIt = RANGES::begin(inputKeys);
        while (co_await it.next())
        {
            if (co_await it.hasValue())
            {
                *output = co_await it.value();
            }
            else
            {
                *(missing++) = {keyIt, output};
            }

            RANGES::advance(keyIt, 1);
            RANGES::advance(output, 1);
        }
    }

public:
    using MutableStorage = MutableStorageType;

    class ReadIterator
    {
        friend class MultiLayerStorage;

    private:
        boost::container::small_vector<utilities::AnyHolder<Value>, 1> m_values;
        int64_t m_index = -1;

    public:
        using Key = std::remove_cvref_t<typename MultiLayerStorage::Key> const&;
        using Value = std::remove_cvref_t<typename MultiLayerStorage::Value> const&;

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
        task::AwaitableValue<Key> key() const { static_assert(!sizeof(this)); }
        task::AwaitableValue<Value> value() const { return {m_values[m_index].get()}; }
        task::AwaitableValue<bool> hasValue() const { return {m_values[m_index].hasValue()}; }
    };

    MultiLayerStorage(BackendStorage& backendStorage) requires(!withCacheStorage)
      : m_backendStorage(backendStorage)
    {}

    MultiLayerStorage(BackendStorage& backendStorage,
        std::conditional_t<withCacheStorage, std::add_lvalue_reference_t<CachedStorage>,
            std::monostate>
            cacheStorage) requires(withCacheStorage)
      : m_backendStorage(backendStorage), m_cacheStorage(cacheStorage)
    {}
    MultiLayerStorage(const MultiLayerStorage&) = delete;
    MultiLayerStorage(MultiLayerStorage&&) = delete;
    MultiLayerStorage& operator=(const MultiLayerStorage&) = delete;
    MultiLayerStorage& operator=(MultiLayerStorage&&) = delete;

    task::Task<ReadIterator> read(RANGES::input_range auto const& keys)
    {
        ReadIterator iterator;
        if constexpr (RANGES::sized_range<decltype(keys)>)
        {
            iterator.m_values.reserve(RANGES::size(keys));
        }

        boost::container::small_vector<std::tuple<RANGES::iterator_t<decltype(keys)>, size_t>, 1>
            queryMissingIterators;
        boost::container::small_vector<std::tuple<RANGES::iterator_t<decltype(keys)>, size_t>, 1>
            missingIterators;

        auto view = RANGES::join_view(RANGES::single_view(m_mutableStorage.get()),
            m_immutableStorages | RANGES::views::transform([](auto& ptr) { return ptr.get(); }),
            RANGES::single_view(std::addressof(m_backendStorage)));
        auto& values = iterator.m_values;
        for (auto& storagePtr : view)
        {
            if (!storagePtr)
            {
                continue;
            }

            if (!RANGES::empty(missingIterators))
            {
                queryMissingIterators.swap(missingIterators);
                auto outputRange = queryMissingIterators | RANGES::views::values;
                readBatch(*storagePtr, queryMissingIterators | RANGES::views::keys,
                    RANGES::begin(outputRange), std::back_inserter(missingIterators));
                continue;
            }

            if (RANGES::empty(values))
            {
                readbatch(*storagePtr, keys, std::back_inserter(values),
                    std::back_inserter(missingIterators));
            }
            else
            {
                break;
            }
        }
    }

    task::Task<void> write(RANGES::input_range auto&& keys, RANGES::input_range auto&& values)
    {
        if (!m_mutableStorage)
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

    std::unique_ptr<MultiLayerStorage> fork()
    {
        std::scoped_lock lock(m_mergeMutex, m_immutablesMutex);
        auto newMultiLayerStorage = std::make_unique<MultiLayerStorage>(m_backendStorage);

        return newMultiLayerStorage;
    }

    template <class... Args>
    void newMutable(Args... args)
    {
        std::unique_lock lock(m_immutablesMutex);
        if (m_mutableStorage)
        {
            BOOST_THROW_EXCEPTION(DuplicateMutableStorageError{});
        }

        m_mutableStorage = std::make_unique<MutableStorageType>(args...);
    }

    void dropMutable() { m_mutableStorage.reset(); }

    void pushMutableToImmutableFront()
    {
        if (!m_mutableStorage)
        {
            BOOST_THROW_EXCEPTION(NotExistsMutableStorageError{});
        }
        std::unique_lock lock(m_immutablesMutex);
        m_immutableStorages.push_front(std::move(m_mutableStorage));
        m_mutableStorage.reset();
    }

    void popImmutableFront()
    {
        std::unique_lock lock(m_immutablesMutex);
        if (m_immutableStorages.empty())
        {
            BOOST_THROW_EXCEPTION(NotExistsImmutableStorageError{});
        }
        m_immutableStorages.pop_front();
    }

    task::Task<void> mergeAndPopImmutableBack()
    {
        std::unique_lock mergeLock(m_mergeMutex);
        std::unique_lock immutablesLock(m_immutablesMutex);
        if (m_immutableStorages.empty())
        {
            BOOST_THROW_EXCEPTION(NotExistsImmutableStorageError{});
        }
        auto immutableStorage = std::move(m_immutableStorages.back());
        m_immutableStorages.pop_back();
        immutablesLock.unlock();

        // TODO: Transactional
        auto it = co_await immutableStorage->seek(transaction_executor::EMPTY_STATE_KEY);
        while (co_await it.next())
        {
            if (co_await it.hasValue())
            {
                co_await storage2::writeOne(
                    m_backendStorage, co_await it.key(), co_await it.value());
            }
            else
            {
                co_await storage2::removeOne(m_backendStorage, co_await it.key());
            }
        }
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