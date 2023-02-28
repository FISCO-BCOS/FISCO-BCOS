#pragma once
#include "bcos-framework/storage2/Storage.h"
#include "bcos-task/Trait.h"
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-utilities/AnyHolder.h>
#include <boost/container/small_vector.hpp>
#include <boost/throw_exception.hpp>
#include <iterator>
#include <range/v3/range/access.hpp>
#include <range/v3/range_fwd.hpp>
#include <range/v3/view/any_view.hpp>
#include <range/v3/view/transform.hpp>
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


    task::Task<boost::container::small_vector<int64_t, 1>> readStorage(
        RANGES::input_range auto const& keys, RANGES::input_range auto& outputValues, auto& storage)
    {
        auto it = co_await storage.read(keys);
        boost::container::small_vector<int64_t, 1> missingIndexes;

        int64_t index = 0;
        while (co_await it.next())
        {
            if (co_await it.hasValue())
            {
                outputValues[index] = co_await it.value();
            }
            else
            {
                missingIndexes.emplace_back(index);
            }
            ++index;
        }
        co_return missingIndexes;
    }

    task::Task<boost::container::small_vector<int64_t, 1>> readStorageRange(
        RANGES::input_range auto const& keys, RANGES::input_range auto& values,
        RANGES::range auto& storages)
    {
        auto& storage = RANGES::front(storages);
        auto nextRange = storages | RANGES::views::drop(1);

        auto missingIndexes = co_await readStorage(keys, values, storage);
        if (RANGES::empty(missingIndexes) || RANGES::empty(nextRange))
        {
            co_return missingIndexes;
        }

        auto missingKeys = missingIndexes |
                           RANGES::views::transform([&keys](int64_t index) { return keys[index]; });
        auto missingValues =
            missingIndexes | RANGES::views::transform([&values](int64_t index) mutable -> auto& {
                return values[index];
            });

        co_return co_await readStorageRange(missingKeys, missingValues, nextRange);
    }

    // TODO: Solve the oom of compiler
    //  task::Task<void> readMultiStorage([[maybe_unused]] RANGES::input_range auto const& keys,
    //      [[maybe_unused]] RANGES::input_range auto& values)
    //  {
    //      co_return;
    //  }
    //  template <class Storage, class... Storages>
    //  task::Task<void> readMultiStorage(RANGES::input_range auto const& keys,
    //      RANGES::input_range auto& values, Storage& storage, Storages&... storages)
    //  {
    //      using StorageType = std::remove_cvref_t<Storage>;

    //     boost::container::small_vector<int64_t, 1> missingIndexes;
    //     if constexpr (std::is_same_v<StorageType, decltype(m_immutableStorages)>)
    //     {
    //         auto immutableStorages =
    //             storage | RANGES::views::transform([](auto& ptr) -> auto& { return *ptr; });
    //         missingIndexes = co_await readStorageRange(keys, values, immutableStorages);
    //     }
    //     else
    //     {
    //         missingIndexes = co_await readStorage(keys, values, storage);
    //     }

    //     if (RANGES::empty(missingIndexes))
    //     {
    //         co_return;
    //     }

    //     auto missingKeys = missingIndexes |
    //                        RANGES::views::transform([&keys](int64_t index) { return keys[index];
    //                        });
    //     auto missingValues =
    //         missingIndexes | RANGES::views::transform([&values](int64_t index) mutable -> auto& {
    //             return values[index];
    //         });
    //     co_await readMultiStorage(missingKeys, missingValues, storages...);
    // }

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
        iterator.m_values.resize(RANGES::size(keys));
        auto& values = iterator.m_values;

        boost::container::small_vector<int64_t, 1> missingIndexes;
        if (m_mutableStorage)
        {
            missingIndexes = co_await readStorage(keys, values, *m_mutableStorage);
            if (RANGES::empty(missingIndexes))
            {
                co_return iterator;
            }
        }

        auto missingKeys = missingIndexes |
                           RANGES::views::transform([&keys](int64_t index) { return keys[index]; });
        auto missingValues =
            missingIndexes | RANGES::views::transform([&values](int64_t index) mutable -> auto& {
                return values[index];
            });
        if (!m_immutableStorages.empty())
        {
            auto immutableStorages = m_immutableStorages | RANGES::views::transform([
            ](auto& ptr) -> auto& { return *ptr; });
            if (!m_mutableStorage)
            {
                missingIndexes = co_await readStorageRange(keys, values, immutableStorages);
            }
            else
            {
                missingIndexes =
                    co_await readStorageRange(missingKeys, missingValues, immutableStorages);
            }

            if (RANGES::empty(missingIndexes))
            {
                co_return iterator;
            }
        }

        auto missingKeys2 =
            missingIndexes |
            RANGES::views::transform([&missingKeys](int64_t index) { return missingKeys[index]; });
        auto missingValues2 =
            missingIndexes |
            RANGES::views::transform([&missingValues](int64_t index) mutable -> auto& {
                return missingValues[index];
            });
        if (m_immutableStorages.empty())
        {
            missingIndexes = co_await readStorage(keys, values, m_backendStorage);
        }
        else
        {
            auto missingKeys = missingIndexes | RANGES::views::transform(
                                                    [&keys](int64_t index) { return keys[index]; });
            auto missingValues =
                missingIndexes |
                RANGES::views::transform([&values](int64_t index) mutable -> auto& {
                    return values[index];
                });
            missingIndexes = co_await readStorage(missingKeys2, missingValues2, m_backendStorage);
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
        auto it = co_await immutableStorage->seek(storage2::STORAGE_BEGIN);
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