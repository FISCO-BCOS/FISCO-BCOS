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
struct UnsupportedMethod : public bcos::Error {};
// clang-format on

template <transaction_executor::StateStorage MutableStorageType, class CachedStorage,
    transaction_executor::StateStorage BackendStorage>
requires((std::is_void_v<CachedStorage> || (transaction_executor::StateStorage<CachedStorage>)) &&
         storage2::SeekableStorage<MutableStorageType>) class MultiLayerStorage
{
private:
    constexpr static bool withCacheStorage = !std::is_void_v<CachedStorage>;
    using KeyType = std::remove_cvref_t<typename MutableStorageType::Key>;
    using ValueType = std::remove_cvref_t<typename MutableStorageType::Value>;
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
        RANGES::range auto& outputValues, RANGES::range auto& outputMissingIndexes)
    {
        auto it = co_await storage.read(inputKeys);

        while (co_await it.next())
        {
            if (co_await it.hasValue())
            {
                outputValues.emplace_back(co_await it.value());
            }
            else
            {
                outputMissingIndexes.emplace_back(RANGES::size(outputValues));
                auto emptyValue = utilities::AnyHolder<ValueType>();
                outputValues.emplace_back(std::move(emptyValue));
            }
        }
        co_return;
    }

    task::Task<void> readMissingBatch(auto& storage, RANGES::input_range auto const& inputKeys,
        RANGES::range auto& outputValues, RANGES::range auto const& missingIndexes,
        RANGES::range auto& outputMissingIndexes)
    {
        auto it = co_await storage.read(
            missingIndexes |
            RANGES::views::transform([&inputKeys](auto&& index) { return inputKeys[index]; }));
        auto missingIt = RANGES::begin(missingIndexes);

        while (co_await it.next())
        {
            if (co_await it.hasValue())
            {
                outputValues[*missingIt] = co_await it.value();
            }
            else
            {
                outputMissingIndexes.emplace_back(*missingIt);
            }
            RANGES::advance(missingIt, 1);
        }
        co_return;
    }

public:
    using MutableStorage = MutableStorageType;

    class ReadIterator
    {
        friend class MultiLayerStorage;

    private:
        boost::container::small_vector<utilities::AnyHolder<ValueType>, 1> m_values;
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
        task::AwaitableValue<Key> key() const { BOOST_THROW_EXCEPTION(UnsupportedMethod{}); }
        task::AwaitableValue<Value> value() const { return {m_values[m_index].get()}; }
        task::AwaitableValue<bool> hasValue() const { return {m_values[m_index].hasValue()}; }
    };

    using Key = KeyType;
    using Value = ValueType;

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
        auto& values = iterator.m_values;

        boost::container::small_vector<size_t, 1> missingIndexes;
        decltype(missingIndexes) outputMissingIndexes;

        if (m_mutableStorage)
        {
            co_await readBatch(*m_mutableStorage, keys, values, outputMissingIndexes);
            if (RANGES::empty(outputMissingIndexes))
            {
                co_return iterator;
            }
        }

        for (auto& immutableStorage : m_immutableStorages)
        {
            if (RANGES::empty(values))
            {
                co_await readBatch(*immutableStorage, keys, values, outputMissingIndexes);
            }
            else
            {
                missingIndexes.swap(outputMissingIndexes);
                outputMissingIndexes.clear();
                co_await readMissingBatch(
                    *immutableStorage, keys, values, missingIndexes, outputMissingIndexes);
            }
            if (RANGES::empty(outputMissingIndexes))
            {
                co_return iterator;
            }
        }

        if constexpr (withCacheStorage)
        {
            if (RANGES::empty(values))
            {
                co_await readBatch(m_cacheStorage, keys, values, outputMissingIndexes);
            }
            else
            {
                missingIndexes.swap(outputMissingIndexes);
                outputMissingIndexes.clear();
                co_await readMissingBatch(
                    m_cacheStorage, keys, values, missingIndexes, outputMissingIndexes);
            }
            if (RANGES::empty(outputMissingIndexes))
            {
                co_return iterator;
            }
        }

        if (RANGES::empty(values))
        {
            co_await readBatch(m_backendStorage, keys, values, outputMissingIndexes);
        }
        else
        {
            missingIndexes.swap(outputMissingIndexes);
            outputMissingIndexes.clear();
            co_await readMissingBatch(
                m_backendStorage, keys, values, missingIndexes, outputMissingIndexes);
        }

        co_return iterator;
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